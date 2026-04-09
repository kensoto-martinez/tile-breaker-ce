#include <stdint.h>
#include <stdlib.h>
#include <sys/timers.h>
#include <sys/rtc.h>
#include <math.h>
#include <debug.h>
#include "game.h"

static const uint8_t TILE_COLORS[10] = {
    COLOR_BASE,
    COLOR_BASE + 1,
    COLOR_BASE + 2,
    COLOR_BASE + 3,
    COLOR_BASE + 4,
    COLOR_BASE + 5,
    COLOR_BASE + 6,
    COLOR_BASE + 7,
    COLOR_BASE + 8,
    COLOR_BASE + 9
};

static const uint8_t BALL_RADII[5] = {
    5, //basic ball
    5, //plasma ball
    3, //sniper ball
    5, //heat ball
    8  //cannonball
};

static const uint16_t BALL_COLORS[5] = {
    COLOR_BASIC_BALL,
    COLOR_PLASMA_BALL,
    COLOR_SNIPER_BALL,
    COLOR_HEAT_BALL,
    COLOR_CANNONBALL
};

static Ball balls[MAX_BALLS];
static uint8_t ball_count = 0;
static uint8_t timer_id;
static Tile tiles[ROWS][COLS];
static uint8_t tile_level_health = 1;
static uint8_t tiles_alive = 0;
static Cursor cursor;
static uint64_t money = 0;
static uint8_t menu = MENU_NONE;

//game functions
static int rand_range(int min, int max)
{
    return min + (int)(random() % (unsigned)(max - min + 1));
}

static int32_t tile_collision(int dx, int dy, Ball *b)
{
    return (int32_t)dx * dx + (int32_t)dy * dy <= (int32_t)b->radius * b->radius;
}

static int cursor_on_tile(int row, int col)
{
    return row >= 0 && row < ROWS && col >= 0 && col < COLS && tiles[row][col].health > 0;
}

static void damage_tile(int r, int c)
{
    //damage tile
    tiles[r][c].health--;
    if (tiles[r][c].health == 0) tiles_alive--;

    //give money
    money++;
}

static void spawn_tiles(void)
{
    //generate tiles
    tiles_alive = 0;
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            if (rand_range(1, 100) <= TILE_SPAWN_PCNT) {
                tiles[r][c].health = tile_level_health;
                tiles_alive++;
            } else {
                tiles[r][c].health = 0;
            }
        }
    }

    //guarantees at least 1 tile spawns
    if (tiles_alive == 0) {
        uint8_t r = (uint8_t)rand_range(0, ROWS - 1);
        uint8_t c = (uint8_t)rand_range(0, COLS - 1);
        tiles[r][c].health = tile_level_health;
        tiles_alive = 1;
    }
}

static void update_tiles(void)
{
    for (uint8_t i = 0; i < ball_count; i++) {
        Ball *b = &balls[i];
        int bx = (int)(b->x / FP_SCALE);
        int by = (int)(b->y / FP_SCALE);

        for (uint8_t r = 0; r < ROWS; r++) {
            for (uint8_t c = 0; c < COLS; c++) {
                //don't register collision if tile is dead
                if (tiles[r][c].health == 0) continue;

                //nearest point on tile rect to ball center
                int tx = c * TILE_W;
                int ty = TILE_MARGIN_Y + r * TILE_H;
                int nx = bx < tx          ? tx
                       : bx > tx + TILE_W ? tx + TILE_W
                       :                    bx;
                int ny = by < ty          ? ty
                       : by > ty + TILE_H ? ty + TILE_H
                       :                    by;
                int dx = bx - nx;
                int dy = by - ny;

                //stop if no collision
                if (!tile_collision(dx, dy, b))
                    continue;

                //collision, damage tile
                damage_tile(r, c);

                if (nx == bx) {
                    //ball center is horizontally inside the tile meaning vertical face hit
                    if (by < ty + TILE_H / 2) {
                        b->vy = (b->vy > 0) ? -b->vy : b->vy;  //hit top face, ensure going up
                    } else {
                        b->vy = (b->vy < 0) ? -b->vy : b->vy;  //hit bottom face, ensure going down
                    }
                } else if (ny == by) {
                    //ball center is vertically inside the tile meaning horizontal face hit
                    if (bx < tx + TILE_W / 2) {
                        b->vx = (b->vx > 0) ? -b->vx : b->vx;  //hit left face, ensure going left
                    } else {
                        b->vx = (b->vx < 0) ? -b->vx : b->vx;  //hit right face, ensure going right
                    }
                } else {
                    //corner hit meaning reflect both axes
                    if (bx < tx + TILE_W / 2) {
                        b->vx = (b->vx > 0) ? -b->vx : b->vx;
                    } else {
                        b->vx = (b->vx < 0) ? -b->vx : b->vx;
                    }
                    if (by < ty + TILE_H / 2) {
                        b->vy = (b->vy > 0) ? -b->vy : b->vy;
                    } else {
                        b->vy = (b->vy < 0) ? -b->vy : b->vy;
                    }
                }
            }
        }
    }

    if (tiles_alive == 0) {
        tile_level_health++;
        spawn_tiles();
    }
}

static void spawn_ball(BallType type)
{
    //don't spawn more than MAX_BALLS
    if (ball_count >= MAX_BALLS) return;

    //create new ball in center of screen
    Ball *b = &balls[ball_count];
    b->type   = type;
    b->radius = BALL_RADII[type];
    b->color  = BALL_COLORS[type];
    b->x = GFX_LCD_WIDTH / 2 * FP_SCALE;
    b->y = GFX_LCD_HEIGHT / 2 * FP_SCALE;
    int raw_vx, raw_vy;
    do {
        raw_vx = rand_range(-100, 100);
        raw_vy = rand_range(-100, 100);
    } while (raw_vx == 0 && raw_vy == 0);

    //integer magnitude via babylonian square root
    int32_t mag2 = (int32_t)raw_vx * raw_vx + (int32_t)raw_vy * raw_vy;
    int32_t mag  = 1;
    {
        int32_t s = mag2;
        while (s > 0) {s >>= 2; mag <<= 1;}
        //two newton iterations
        if (mag > 0) mag = (mag + mag2 / mag) >> 1;
        if (mag > 0) mag = (mag + mag2 / mag) >> 1;
        if (mag == 0) mag = 1;
    }

    int speed_pps = DEFAULT_SPEED_PPS;

    //scale: vx_fp = raw_vx / mag * speed_pps * FP_SCALE
    b->vx = ((int32_t)raw_vx * speed_pps * FP_SCALE) / mag;
    b->vy = ((int32_t)raw_vy * speed_pps * FP_SCALE) / mag;

    ball_count++;
}

static void remove_ball(BallType type)
{
    for (int i = ball_count - 1; i >= 0; i--) {
        if (balls[i].type == type) {
            balls[i] = balls[ball_count - 1];
            ball_count--;
            return;
        }
    }
}

static void game_update(void)
{
    static uint32_t last_ticks = 0xFFFFFFFF;
    uint32_t now = timer_Get(timer_id);
    uint32_t ticks = last_ticks - now;
    last_ticks = now;

    if (ticks > 3276) ticks = 3276;

    //move cursor
    if (kb_IsDown(kb_KeyUp)) cursor.y -= 2;
    if (kb_IsDown(kb_KeyDown)) cursor.y += 2;
    if (kb_IsDown(kb_KeyLeft)) cursor.x -= 2;
    if (kb_IsDown(kb_KeyRight)) cursor.x += 2;

    //clamp cursor to screen
    if (cursor.x < 0) cursor.x = 0;
    if (cursor.x >= GFX_LCD_WIDTH) cursor.x = GFX_LCD_WIDTH - 1;
    if (cursor.y < 0) cursor.y = 0;
    if (cursor.y >= GFX_LCD_HEIGHT) cursor.y = GFX_LCD_HEIGHT - 1;

    //if ENTER pressed, damage tile at cursor
    static int enter_is_down = 0;
    if (kb_IsDown(kb_KeyEnter)) {
        if (!enter_is_down) {
            int r = (cursor.y - TILE_MARGIN_Y) / TILE_H;
            int c = cursor.x / TILE_W;
            if (cursor_on_tile(r, c)) damage_tile(r, c);
            enter_is_down = 1;
        }
    } 
    else {
        enter_is_down = 0;
    }

    //if MODE pressed, damage tile at cursor
    static int mode_is_down = 0;
    if (kb_IsDown(kb_KeyMode)) {
        if (!mode_is_down) {
            if (menu != MENU_UPGRADE) {
                menu = MENU_UPGRADE;
            }
            else {
                menu = MENU_NONE;
            }
            mode_is_down = 1;
        }
    } 
    else {
        mode_is_down = 0;
    }

    //if a button is pressed that is supposed to spawn in a ball, spawn the corresponding ball in the game
    {
        static uint8_t spawn_is_down = 0;
        static uint8_t delete_is_down = 0;
        uint8_t spawn_keys =
            (kb_IsDown(kb_KeyRecip)  ? (1 << BALL_BASIC)     : 0) |
            (kb_IsDown(kb_KeySin)    ? (1 << BALL_PLASMA)    : 0) |
            (kb_IsDown(kb_KeyCos)    ? (1 << BALL_SNIPER)    : 0) |
            (kb_IsDown(kb_KeyTan)    ? (1 << BALL_HEAT)      : 0) |
            (kb_IsDown(kb_KeyPower)  ? (1 << BALL_CANNONBALL): 0);
        uint8_t delete_keys =
            (kb_IsDown(kb_KeySquare) ? (1 << BALL_BASIC)     : 0) |
            (kb_IsDown(kb_KeyComma)  ? (1 << BALL_PLASMA)    : 0) |
            (kb_IsDown(kb_KeyLParen) ? (1 << BALL_SNIPER)    : 0) |
            (kb_IsDown(kb_KeyRParen) ? (1 << BALL_HEAT)      : 0) |
            (kb_IsDown(kb_KeyDiv)    ? (1 << BALL_CANNONBALL): 0);

        for (uint8_t t = 0; t < 5; t++) {
            uint8_t mask = 1 << t;
            if ((spawn_keys & mask) && !(spawn_is_down & mask)) {
                spawn_ball((BallType)t);
            }
            if ((delete_keys & mask) && !(delete_is_down & mask)) {
                remove_ball((BallType)t);
            }
        }
        spawn_is_down  = spawn_keys;
        delete_is_down = delete_keys;
    }

    //move and bounce each ball
    for (uint8_t i = 0; i < ball_count; i++) {
        Ball *b = &balls[i];
        b->x += b->vx * (int32_t)ticks / 32768;
        b->y += b->vy * (int32_t)ticks / 32768;

        int32_t x_min = (int32_t)b->radius * FP_SCALE;
        int32_t x_max = (int32_t)(GFX_LCD_WIDTH - b->radius) * FP_SCALE;
        int32_t y_min = (TILE_MARGIN_Y + (int32_t)b->radius) * FP_SCALE;
        int32_t y_max = (int32_t)(GFX_LCD_HEIGHT - TILE_MARGIN_Y - b->radius) * FP_SCALE;

        if (b->x < x_min) { b->x = x_min; b->vx = (b->vx < 0) ? -b->vx : b->vx; }
        if (b->x > x_max) { b->x = x_max; b->vx = (b->vx > 0) ? -b->vx : b->vx; }
        if (b->y < y_min) { b->y = y_min; b->vy = (b->vy < 0) ? -b->vy : b->vy; }
        if (b->y > y_max) { b->y = y_max; b->vy = (b->vy > 0) ? -b->vy : b->vy; }
    }

    //register tile collisions
    update_tiles();
}
//game functions


//ui functions
static void render_menu_background(void)
{
    gfx_SetColor(BACKGROUND_COLOR_MENU);
    gfx_FillRectangle_NoClip(MENU_BACKGROUND_MARGIN, MENU_BACKGROUND_MARGIN, GFX_LCD_WIDTH - MENU_BACKGROUND_MARGIN * 2, GFX_LCD_HEIGHT - MENU_BACKGROUND_MARGIN * 2);
}

static void render_button(int x, int y, int w, const char *label, const int MENU_BTN_H)
{
    //filled background, then border, then centered label
    gfx_SetColor(BACKGROUND_COLOR_GAME);
    gfx_FillRectangle_NoClip(x, y, w, MENU_BTN_H);
    gfx_SetColor(0x00);
    gfx_Rectangle_NoClip(x, y, w, MENU_BTN_H);
    int label_x = x + (w - gfx_GetStringWidth(label)) / 2;
    gfx_PrintStringXY(label, label_x, y + 3);
}

static void render_upgrade_menu(void)
{
    render_menu_background();

    const int MENU_INNER_Y = MENU_BACKGROUND_MARGIN + 1;
    const int MENU_INNER_H = (GFX_LCD_HEIGHT - MENU_BACKGROUND_MARGIN * 2 - 2);
    const int MENU_COL_W = (GFX_LCD_WIDTH  - MENU_BACKGROUND_MARGIN * 2 - 2) / 5;
    const int MENU_BTN_PADDING = 3;
    const int MENU_BTN_W = (MENU_COL_W - MENU_BTN_PADDING * 2);
    const int MENU_BTN_H = 13;
    const int MENU_ROW_BALL_Y = 16;
    const int MENU_ROW_NAME_Y = 45;
    const int MENU_ROW_BTN1_Y = 60;
    const int MENU_ROW_BTN2_Y = (MENU_ROW_BTN1_Y + MENU_BTN_H + 4);
    const int MENU_ROW_BTN3_Y = (MENU_ROW_BTN2_Y + MENU_BTN_H + 4);
    const int MENU_ROW_BTN4_Y = (MENU_ROW_BTN3_Y + MENU_BTN_H + 4);

    static const char *BALL_NAMES[5] = {
        "Basic", "Plasma", "Sniper", "Heat", "Cannon"
    };
    static const char *BTN_LABELS[4] = {
        "Speed", "Power", "Buy x1", "Sell x1"
    };
    static const int BTN_ROWS[4] = {
        MENU_ROW_BTN1_Y, MENU_ROW_BTN2_Y, MENU_ROW_BTN3_Y, MENU_ROW_BTN4_Y
    };

    for (uint8_t i = 0; i < 5; i++) {
        int col_x = (MENU_BACKGROUND_MARGIN + 1) + i * MENU_COL_W;
        int col_cx = col_x + MENU_COL_W / 2;

        //vertical divider between columns
        if (i > 0) {
            gfx_SetColor(0x00);
            gfx_VertLine_NoClip(col_x, MENU_INNER_Y, MENU_INNER_H);
        }

        //ball preview centered in column
        gfx_SetColor(COLOR_BASIC_BALL + i);
        gfx_FillCircle_NoClip(col_cx, MENU_INNER_Y + MENU_ROW_BALL_Y, BALL_RADII[i]);

        //ball name centered
        int name_x = col_x + (MENU_COL_W - gfx_GetStringWidth(BALL_NAMES[i])) / 2;
        gfx_SetTextFGColor(0x00);
        gfx_SetTextTransparentColor(BACKGROUND_COLOR_MENU);
        gfx_PrintStringXY(BALL_NAMES[i], name_x, MENU_INNER_Y + MENU_ROW_NAME_Y);

        //four buttons
        for (uint8_t j = 0; j < 4; j++) {
            render_button(col_x + MENU_BTN_PADDING, MENU_INNER_Y + BTN_ROWS[j], MENU_BTN_W, BTN_LABELS[j], MENU_BTN_H);
        }
    }
}
//ui functions


//main functions
void game_init(void)
{
    srandom((unsigned)rtc_Time());

    //gradient from cyan to red for tiles
    gfx_palette[COLOR_BASE]     = gfx_RGBTo1555(0x65, 0xFF, 0xFF); //light blue
    gfx_palette[COLOR_BASE + 1] = gfx_RGBTo1555(0x3B, 0xFF, 0xAA); //mint
    gfx_palette[COLOR_BASE + 2] = gfx_RGBTo1555(0x17, 0xFF, 0x45); //light green
    gfx_palette[COLOR_BASE + 3] = gfx_RGBTo1555(0x57, 0xFF, 0x1F); //greenish cyan
    gfx_palette[COLOR_BASE + 4] = gfx_RGBTo1555(0xA6, 0xFF, 0x00); //lime green
    gfx_palette[COLOR_BASE + 5] = gfx_RGBTo1555(0xE5, 0xFF, 0x00); //yellow
    gfx_palette[COLOR_BASE + 6] = gfx_RGBTo1555(0xFF, 0xD9, 0x00); //orangeish yellow
    gfx_palette[COLOR_BASE + 7] = gfx_RGBTo1555(0xFF, 0x8C, 0x00); //orange
    gfx_palette[COLOR_BASE + 8] = gfx_RGBTo1555(0xFF, 0x40, 0x00); //red-orange
    gfx_palette[COLOR_BASE + 9] = gfx_RGBTo1555(0xFF, 0x00, 0x00); //red

    //color of balls
    gfx_palette[COLOR_BASIC_BALL]  = gfx_RGBTo1555(0xFF, 0xFF, 0x14); //yellow
    gfx_palette[COLOR_PLASMA_BALL] = gfx_RGBTo1555(0xB2, 0x16, 0xD9); //purple
    gfx_palette[COLOR_SNIPER_BALL] = gfx_RGBTo1555(0xE6, 0xE6, 0xE6); //off white
    gfx_palette[COLOR_HEAT_BALL]   = gfx_RGBTo1555(0xFF, 0x31, 0x0D); //scarlet
    gfx_palette[COLOR_CANNONBALL]  = gfx_RGBTo1555(0x85, 0x85, 0x85); //grey

    //background colors
    gfx_palette[BACKGROUND_COLOR_GAME]   = gfx_RGBTo1555(0xFF, 0xF9, 0xE8); //background of balls and tiles
    gfx_palette[BACKGROUND_COLOR_MARGIN] = gfx_RGBTo1555(0xFF, 0xED, 0xB8); //background of margins
    gfx_palette[BACKGROUND_COLOR_MENU]   = gfx_RGBTo1555(0xFF, 0xD7, 0xA6); //background of menus

    //set timer
    timer_id = 1;
    timer_Set(timer_id, 0xFFFFFFFF);
    timer_Enable(timer_id, TIMER_32K, TIMER_0INT, TIMER_DOWN);

    //initialize cursor position
    cursor.x = GFX_LCD_WIDTH / 2;
    cursor.y = GFX_LCD_HEIGHT / 2;
    
    //spawn in some balls
    for (uint8_t i = 0; i < 10; i++) {
        spawn_ball(BALL_BASIC);
    }

    //spawn in initial tiles
    spawn_tiles();
}

void game_draw(void)
{
    //update game values
    game_update();

    //render background
    gfx_FillScreen(BACKGROUND_COLOR_GAME);

    //render top and bottom margin background
    gfx_SetColor(BACKGROUND_COLOR_MARGIN);
    gfx_FillRectangle_NoClip(0, 0, GFX_LCD_WIDTH, TILE_MARGIN_Y);
    gfx_FillRectangle_NoClip(0, TILE_MARGIN_Y + TILE_AREA_Y, GFX_LCD_WIDTH, TILE_MARGIN_Y);

    //render money
    gfx_SetColor(0x00);
    gfx_PrintStringXY("Money: ", MARGIN_TEXT_PADDING, MARGIN_TEXT_PADDING);
    gfx_PrintUInt(money, 1);

    //render level currently on (level = tile starting health)
    gfx_PrintString(" -- Level: ");
    gfx_PrintUInt(tile_level_health, 1);

    //render keybinds to help user navigate through the game
    gfx_PrintStringXY("Upgrade: MODE -- Stats: STAT -- Quit: CLEAR", MARGIN_TEXT_PADDING, TILE_MARGIN_Y + TILE_AREA_Y + MARGIN_TEXT_PADDING);

    //render tiles
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            //don't render dead tiles
            if (tiles[r][c].health == 0) continue;

            int tx = c * TILE_W;
            int ty = TILE_MARGIN_Y + r * TILE_H;

            //tile
            gfx_SetColor(TILE_COLORS[(tiles[r][c].health - 1) % 10]);
            gfx_FillRectangle(tx, ty, TILE_W, TILE_H);

            //health
            gfx_SetTextXY(tx + TILE_W / 2 - 3, ty + TILE_H / 2 - 4);
            gfx_SetTextFGColor(0x00);
            gfx_SetTextTransparentColor(0xFF);
            gfx_PrintUInt(tiles[r][c].health, 1);
        }
    }

    //render balls
    for (uint8_t i = 0; i < ball_count; i++) {
        Ball *b = &balls[i];

        //render ball
        int px = (int)(b->x / FP_SCALE);
        int py = (int)(b->y / FP_SCALE);
        gfx_SetColor(b->color);
        gfx_FillCircle(px, py, b->radius);
    }

    //render cursor
    gfx_SetTextXY(cursor.x, cursor.y);
    gfx_PrintChar('+');

    //render upgrade menu if toggled on
    if (menu == MENU_UPGRADE)
    {
        render_upgrade_menu();
    }
}
//main functions
