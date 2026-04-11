#include <stdint.h>
#include <stdlib.h>
#include <fileioc.h>
#include <sys/timers.h>
#include <sys/rtc.h>
#include <math.h>
#include <debug.h>
#include "game.h"

#define MENU_INNER_Y    (MENU_BACKGROUND_MARGIN + 1)
#define MENU_INNER_H    (GFX_LCD_HEIGHT - MENU_BACKGROUND_MARGIN * 2 - 2)
#define MENU_COL_W      ((GFX_LCD_WIDTH - MENU_BACKGROUND_MARGIN * 2 - 2) / 5)
#define MENU_BTN_PAD    3
#define MENU_BTN_W      (MENU_COL_W - MENU_BTN_PAD * 2)
#define MENU_BTN_H      13
#define ROW_BALL_Y      10
#define ROW_NAME_Y      30
#define ROW_SPD_BTN_Y   44
#define ROW_SPD_LVL_Y   (ROW_SPD_BTN_Y + MENU_BTN_H + 2)
#define ROW_PWR_BTN_Y   (ROW_SPD_LVL_Y + 10)
#define ROW_PWR_LVL_Y   (ROW_PWR_BTN_Y + MENU_BTN_H + 2)
#define ROW_BUY_Y       (ROW_PWR_LVL_Y + 10)
#define ROW_SELL_Y      (ROW_BUY_Y + MENU_BTN_H + 4)

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
static uint8_t ball_type_counts[5] = {0, 0, 0, 0, 0};
static uint8_t speed_levels[5] = {1, 1, 1, 1, 1};
static uint8_t power_levels[5] = {1, 1, 1, 1, 1};
static uint8_t timer_id;
static Tile tiles[ROWS][COLS];
static uint16_t tile_level_health = 1;
static uint8_t tiles_alive = 0;
static Cursor cursor;
static uint64_t money = 0;
static uint8_t menu = MENU_NONE;
static int cursor_on_any_button = 0;

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

static void plasma_damage_tile(int condition, int r, int c, int power)
{  
    if (condition && tiles[r][c].health != 0) {
        dbg_printf("1\n");
        if (power >= tiles[r][c].health) {
            tiles[r][c].health = 0;
            tiles_alive -= 1;
            dbg_printf("KILLED\n");
        }
        else {
            tiles[r][c].health -= power;
            dbg_printf("2\n");
        }
    }
}

static void damage_tile(int r, int c, BallType type, uint8_t power)
{
    uint8_t killed_tile = 0;

    //collision kills tile
    if (power >= tiles[r][c].health) {
        killed_tile = 1;
        tiles[r][c].health = 0;
        tiles_alive -= 1;

        //if not plasma or heat, get your money and leave
        if (type != BALL_PLASMA && type != BALL_HEAT) {
            money += power;
            return;
        }
    }

    //collision does not kill tile (or plasma or heat)
    switch (type) {
        case BALL_CURSOR:
        case BALL_SNIPER:
        case BALL_CANNONBALL:
        case BALL_BASIC:
            //do damage to tile
            tiles[r][c].health -= power;
            break;
        case BALL_PLASMA:
            //do damage to tile and tiles orthogonal to the original
            if (!killed_tile) tiles[r][c].health -= power;
            
            plasma_damage_tile(r>0,        r-1, c,   power);
            plasma_damage_tile(r<ROWS - 1, r+1, c,   power);
            plasma_damage_tile(c>0,        r,   c-1, power);
            plasma_damage_tile(c<COLS-1,   r,   c+1, power);

            break;
        case BALL_HEAT:
            //do damage to tile and put it on fire
            if (!killed_tile) tiles[r][c].health -= power;
            tiles[r][c].on_fire = 1;

            break;
    }
    
    money += power;
}

static void update_fire(void)
{
    //store tiles that will catch fire separately so they don't spread on same tick
    static uint8_t will_catch_fire[ROWS][COLS];
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            will_catch_fire[r][c] = 0;
        }
    }

    //orthogonal neighbors have a FIRE_SPREAD_CHANCE_PCNT% chance to catch fire
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            if (tiles[r][c].health == 0 || !tiles[r][c].on_fire) continue;

            if (r > 0 && tiles[r-1][c].health > 0 && !tiles[r-1][c].on_fire && rand_range(1, 100) <= FIRE_SPREAD_CHANCE_PCNT)        will_catch_fire[r-1][c] = 1;
            if (r < ROWS - 1 && tiles[r+1][c].health > 0 && !tiles[r+1][c].on_fire && rand_range(1, 100) <= FIRE_SPREAD_CHANCE_PCNT) will_catch_fire[r+1][c] = 1;
            if (c > 0 && tiles[r][c-1].health > 0 && !tiles[r][c-1].on_fire && rand_range(1, 100) <= FIRE_SPREAD_CHANCE_PCNT)        will_catch_fire[r][c-1] = 1;
            if (c < COLS - 1 && tiles[r][c+1].health > 0 && !tiles[r][c+1].on_fire && rand_range(1, 100) <= FIRE_SPREAD_CHANCE_PCNT) will_catch_fire[r][c+1] = 1;
        }
    }

    //apply spread
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            if (will_catch_fire[r][c]) tiles[r][c].on_fire = 1;
        }
    }

    //burning tiles take fire damage
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            if (tiles[r][c].on_fire && tiles[r][c].health > 0) damage_tile(r, c, BALL_HEAT, power_levels[BALL_HEAT]);
        }
    }
}

static void spawn_tiles(void)
{
    //generate tiles
    tiles_alive = 0;
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            tiles[r][c].on_fire = 0;

            if (rand_range(1, 100) <= TILE_SPAWN_PCNT) {
                tiles[r][c].health = tile_level_health;
                tiles_alive++;
            } 
            else {
                tiles[r][c].health = 0;
            }
        }
    }

    //guarantees at least 1 tile spawns
    if (tiles_alive == 0) {
        uint8_t r = (uint8_t)rand_range(0, ROWS - 1);
        uint8_t c = (uint8_t)rand_range(0, COLS - 1);
        tiles[r][c].health = tile_level_health;
        tiles[r][c].on_fire = 0;
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
                damage_tile(r, c, b->type, power_levels[b->type]);

                //if a cannonball kills a tile, it doesn't bounce off it
                if (b->type == BALL_CANNONBALL && tiles[r][c].health == 0) continue;


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
    //don't spawn more than the maximum ball amount
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

    //integer magnitude via babylonian square root (computationally cheaper sqrt())
    int32_t mag2 = (int32_t)raw_vx * raw_vx + (int32_t)raw_vy * raw_vy;
    int32_t mag  = 1;
    {
        int32_t s = mag2;
        while (s > 0) {s >>= 2; mag <<= 1;}
        if (mag > 0) mag = (mag + mag2 / mag) >> 1;
        if (mag > 0) mag = (mag + mag2 / mag) >> 1;
        if (mag == 0) mag = 1;
    }

    //set velocity
    int speed_pps = DEFAULT_SPEED_PPS + (speed_levels[type] - 1) * ADDITIONAL_SPEED_PPS;
    //scale: vx_fp = raw_vx / mag * speed_pps * FP_SCALE
    b->vx = ((int32_t)raw_vx * speed_pps * FP_SCALE) / mag;
    b->vy = ((int32_t)raw_vy * speed_pps * FP_SCALE) / mag;

    ball_count++;
    ball_type_counts[type]++;
}

static void remove_ball(BallType type)
{
    for (int i = ball_count - 1; i >= 0; i--) {
        if (balls[i].type == type) {
            balls[i] = balls[ball_count - 1];
            ball_count--;
            ball_type_counts[type]--;
            return;
        }
    }
}

static void sniper_seek_closest_tile(Ball *b)
{
    int bx = (int)(b->x / FP_SCALE);
    int by = (int)(b->y / FP_SCALE);

    int best_r = -1, best_c = -1;
    int32_t best_dist2 = 0x7FFFFFFF;

    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            if (tiles[r][c].health == 0) continue;

            int tx = c * TILE_W + TILE_W / 2;
            int ty = TILE_MARGIN_Y + r * TILE_H + TILE_H / 2;
            int dx = bx - tx;
            int dy = by - ty;
            int32_t dist2 = (int32_t)dx * dx + (int32_t)dy * dy;

            if (dist2 < best_dist2) {
                best_dist2 = dist2;
                best_r = r;
                best_c = c;
            }
        }
    }

    if (best_r == -1) return; //no alive tiles, leave velocity unchanged

    //direction vector from ball to closest tile center
    int raw_vx = (best_c * TILE_W + TILE_W / 2) - bx;
    int raw_vy = (TILE_MARGIN_Y + best_r * TILE_H + TILE_H / 2) - by;

    //babylonian sqrt to normalize
    int32_t mag2 = (int32_t)raw_vx * raw_vx + (int32_t)raw_vy * raw_vy;
    int32_t mag = 1;
    {
        int32_t s = mag2;
        while (s > 0) { s >>= 2; mag <<= 1; }
        if (mag > 0) mag = (mag + mag2 / mag) >> 1;
        if (mag > 0) mag = (mag + mag2 / mag) >> 1;
        if (mag == 0) mag = 1;
    }

    int speed_pps = DEFAULT_SPEED_PPS + (speed_levels[BALL_SNIPER] - 1) * ADDITIONAL_SPEED_PPS;
    b->vx = ((int32_t)raw_vx * speed_pps * FP_SCALE) / mag;
    b->vy = ((int32_t)raw_vy * speed_pps * FP_SCALE) / mag;
}

static void game_update(void)
{
    static uint32_t last_ticks = 0xFFFFFFFF;
    int now = timer_Get(timer_id);
    int ticks = last_ticks - now;
    last_ticks = now;

    if (ticks > 3276) ticks = 3276;

    //fire tick
    {
        static uint32_t fire_tick_accum = 0;
        fire_tick_accum += ticks;
        if (fire_tick_accum >= 32768) {
            fire_tick_accum = 0;
            update_fire();
        }
    }

    //move cursor
    {
        const uint8_t CURSOR_SPEED = menu == MENU_NONE ? CURSOR_SPEED_PPS_GAME : CURSOR_SPEED_PPS_MENU;
        if (kb_IsDown(kb_KeyUp)) cursor.y -= CURSOR_SPEED;
        if (kb_IsDown(kb_KeyDown)) cursor.y += CURSOR_SPEED;
        if (kb_IsDown(kb_KeyLeft)) cursor.x -= CURSOR_SPEED;
        if (kb_IsDown(kb_KeyRight)) cursor.x += CURSOR_SPEED;
    }

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
            if (cursor_on_tile(r, c)) damage_tile(r, c, BALL_CURSOR, 1);
            enter_is_down = 1;
        }
    } 
    else {
        enter_is_down = 0;
    }

    //if MODE pressed, toggle upgrade menu
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

    //move and bounce each ball
    for (uint8_t i = 0; i < ball_count; i++) {
        Ball *b = &balls[i];
        b->x += b->vx * (int32_t)ticks / 32768;
        b->y += b->vy * (int32_t)ticks / 32768;

        int32_t x_min = (int32_t)b->radius * FP_SCALE;
        int32_t x_max = (int32_t)(GFX_LCD_WIDTH - b->radius) * FP_SCALE;
        int32_t y_min = (TILE_MARGIN_Y + (int32_t)b->radius) * FP_SCALE;
        int32_t y_max = (int32_t)(GFX_LCD_HEIGHT - TILE_MARGIN_Y - b->radius) * FP_SCALE;

        //wall bounce
        int wall_hit = 0;
        if (b->x < x_min) { b->x = x_min; b->vx = (b->vx < 0) ? -b->vx : b->vx; wall_hit = 1; }
        if (b->x > x_max) { b->x = x_max; b->vx = (b->vx > 0) ? -b->vx : b->vx; wall_hit = 1; }
        if (b->y < y_min) { b->y = y_min; b->vy = (b->vy < 0) ? -b->vy : b->vy; wall_hit = 1; }
        if (b->y > y_max) { b->y = y_max; b->vy = (b->vy > 0) ? -b->vy : b->vy; wall_hit = 1; }

        if (wall_hit && b->type == BALL_SNIPER) sniper_seek_closest_tile(b);
    }

    //register tile collisions
    update_tiles();
}
//game functions


//upgrade menu functions
static void render_menu_background(void)
{
    gfx_SetColor(BACKGROUND_COLOR_MENU);
    gfx_FillRectangle_NoClip(MENU_BACKGROUND_MARGIN, MENU_BACKGROUND_MARGIN, GFX_LCD_WIDTH - MENU_BACKGROUND_MARGIN * 2, GFX_LCD_HEIGHT - MENU_BACKGROUND_MARGIN * 2);
}

static uint8_t u32_to_str(uint32_t v, char *out)
{
    char tmp[12];
    uint8_t len = 0;

    do {
        tmp[len++] = '0' + (v % 10);
        v /= 10;
    } while (v);

    for (uint8_t i = 0; i < len; i++) {
        out[i] = tmp[len - 1 - i];
    }

    out[len] = '\0';
    return len;
}

static int cursor_in_rect(int x, int y, int w, int h)
{
    return cursor.x >= x && cursor.x < x + w && cursor.y >= y && cursor.y < y + h;
}

static uint64_t buy_price(BallType type, uint8_t next_count)
{
    static const double BASE[5]  = {25.0, 200.0, 1500.0, 10000.0, 75000.0};
    static const double MULT[5]  = {1.5,  1.4,   1.35,   1.35,    1.3};
    return (uint64_t)ceil(BASE[type] * pow(MULT[type], (double)(next_count - 1)));
}

static uint64_t speed_price(BallType type, uint8_t next_level)
{
    static const double BASE[5]  = {100.0, 1000.0, 7500.0, 75000.0, 100000.0};
    static const double MULT[5]  = {2.0,   2.5,    1.75,   2.5,     1.225};
    return (uint64_t)ceil(BASE[type] * pow(MULT[type], (double)(next_level - 1)));
}

static uint64_t power_price(BallType type, uint8_t next_level)
{
    static const double BASE[5]  = {250.0, 1250.0, 8000.0, 100000.0, 20000.0};
    static const double MULT[5]  = {1.5,   1.15,   1.06,   1.02,     1.0045};
    return (uint64_t)ceil(BASE[type] * pow(MULT[type], (double)(next_level - 1)));
}

static void format_price(uint64_t value, char *out)
{
    if (value < 100000) {
        // normal number
        uint8_t len = 0;
        char tmp[21];
        do {
            tmp[len++] = '0' + (value % 10);
            value /= 10;
        } while (value);

        for (uint8_t i = 0; i < len; i++) {
            out[i] = tmp[len - 1 - i];
        }
        out[len] = '\0';
    }
    else if (value < 1000000) {
        // thousands (K)
        uint32_t v = value / 1000;
        uint8_t len = 0;
        char tmp[10];

        do {
            tmp[len++] = '0' + (v % 10);
            v /= 10;
        } while (v);

        for (uint8_t i = 0; i < len; i++) {
            out[i] = tmp[len - 1 - i];
        }

        out[len++] = 'K';
        out[len] = '\0';
    }
    else if (value < 1000000000ULL) {
        // millions (M) with 1 decimal
        uint32_t whole = value / 1000000;
        uint32_t decimal = (value / 100000) % 10; // 1 decimal digit

        uint8_t len = 0;
        char tmp[10];

        do {
            tmp[len++] = '0' + (whole % 10);
            whole /= 10;
        } while (whole);

        for (uint8_t i = 0; i < len; i++) {
            out[i] = tmp[len - 1 - i];
        }

        if (decimal > 0) {
            out[len++] = '.';
            out[len++] = '0' + decimal;
        }

        out[len++] = 'M';
        out[len] = '\0';
    }
    else {
        // billions (B) with 1 decimal
        uint32_t whole = value / 1000000000ULL;
        uint32_t decimal = (value / 100000000ULL) % 10;

        uint8_t len = 0;
        char tmp[10];

        do {
            tmp[len++] = '0' + (whole % 10);
            whole /= 10;
        } while (whole);

        for (uint8_t i = 0; i < len; i++) {
            out[i] = tmp[len - 1 - i];
        }

        if (decimal > 0) {
            out[len++] = '.';
            out[len++] = '0' + decimal;
        }

        out[len++] = 'B';
        out[len] = '\0';
    }
}

static int render_button(int x, int y, int w, int h, const char *label, uint8_t hover_color)
{
    int hovered = cursor_in_rect(x, y, w, h);
    gfx_SetColor(hovered ? hover_color : BACKGROUND_COLOR_GAME);
    gfx_FillRectangle(x, y, w, h);
    gfx_SetColor(0x00);
    gfx_Rectangle(x, y, w, h);
    gfx_SetTextFGColor(0x00);
    int label_x = x + (w - gfx_GetStringWidth(label)) / 2;
    gfx_PrintStringXY(label, label_x, y + 3);
    return hovered;
}

static void render_upgrade_btn(int btn_x, int btn_y, int lv_text_x, int lv_text_y, uint8_t current_level, uint8_t capped, uint64_t cost, const char *default_label)
{
    //renders an upgrade button (speed or power) with hover-price, affordability color, level text below it, and handles the purchase on enter. returns 1 if hovered
    uint8_t color = (!capped && money >= cost) ? COLOR_BTN_AFFORD : COLOR_BTN_CANT;
    char label[21];

    if (capped) {
        label[0] = 'M'; label[1] = 'A'; label[2] = 'X'; label[3] = '\0';
    } 
    else if (cursor_in_rect(btn_x, btn_y, MENU_BTN_W, MENU_BTN_H)) {
        format_price(cost, label);
    } 
    else {
        uint8_t k = 0;
        while (default_label[k]) { label[k] = default_label[k]; k++; }
        label[k] = '\0';
    }
    if (render_button(btn_x, btn_y, MENU_BTN_W, MENU_BTN_H, label, color)) cursor_on_any_button = 1;

    gfx_SetTextFGColor(0x00);
    gfx_SetTextXY(lv_text_x, lv_text_y);
    gfx_PrintString("Lv:");
    gfx_PrintUInt(current_level, 1);
}

static void render_buy_btn(int btn_x, int btn_y, uint64_t cost, int enter_pressed, BallType type)
{
    //renders a buy button with hover-price and affordability color, handles the purchase on enter. returns 1 if hovered
    uint8_t color = (money >= cost) ? COLOR_BTN_AFFORD : COLOR_BTN_CANT;
    char label[21];

    if (cursor_in_rect(btn_x, btn_y, MENU_BTN_W, MENU_BTN_H)) {
        format_price(cost, label);
    } 
    else {
        label[0]='B'; label[1]='u'; label[2]='y';
        label[3]=' '; label[4]='x'; label[5]='1'; label[6]='\0';
    }
    if (render_button(btn_x, btn_y, MENU_BTN_W, MENU_BTN_H, label, color)) {
        cursor_on_any_button = 1;
        if (enter_pressed && money >= cost) {
            money -= cost;
            spawn_ball(type);
        }
    }
}

static int speed_upgrade_purchased(int capped, int btn_x, int enter_pressed, uint64_t cost)
{
    return !capped && cursor_in_rect(btn_x, MENU_INNER_Y + ROW_SPD_BTN_Y, MENU_BTN_W, MENU_BTN_H) && enter_pressed && money >= cost;
}

static int power_upgrade_purchased(int btn_x, int enter_pressed, uint64_t cost)
{
    return cursor_in_rect(btn_x, MENU_INNER_Y + ROW_PWR_BTN_Y, MENU_BTN_W, MENU_BTN_H) && enter_pressed && money >= cost;
}

static void render_upgrade_menu(void)
{
    render_menu_background();

    static const char *BALL_NAMES[5] = { "Basic", "Plasma", "Sniper", "Heat", "Cannon" };

    static int enter_is_down = 0;
    int enter_pressed = kb_IsDown(kb_KeyEnter) && !enter_is_down;
    enter_is_down = kb_IsDown(kb_KeyEnter);

    cursor_on_any_button = 0;

    for (uint8_t i = 0; i < 5; i++) {
        int col_x  = (MENU_BACKGROUND_MARGIN + 1) + i * MENU_COL_W;
        int col_cx = col_x + MENU_COL_W / 2;
        int btn_x  = col_x + MENU_BTN_PAD;
        int lv_x   = col_x + (MENU_COL_W - gfx_GetStringWidth("Lv:00")) / 2;

        //vertical divider
        if (i > 0) {
            gfx_SetColor(0x00);
            gfx_VertLine_NoClip(col_x, MENU_INNER_Y, MENU_INNER_H);
        }

        //ball preview and name
        gfx_SetColor(COLOR_BASIC_BALL + i);
        gfx_FillCircle(col_cx, MENU_INNER_Y + ROW_BALL_Y, BALL_RADII[i]);
        gfx_SetTextFGColor(0x00);
        int name_x = col_x + (MENU_COL_W - gfx_GetStringWidth(BALL_NAMES[i])) / 2;
        gfx_PrintStringXY(BALL_NAMES[i], name_x, MENU_INNER_Y + ROW_NAME_Y);

        //speed upgrade button
        {
            int capped = speed_levels[i] >= SPEED_LEVEL_MAX;
            uint64_t cost = capped ? 0 : speed_price((BallType)i, speed_levels[i] + 1);
            render_upgrade_btn(btn_x, MENU_INNER_Y + ROW_SPD_BTN_Y, lv_x,  MENU_INNER_Y + ROW_SPD_LVL_Y, speed_levels[i], capped, cost, "Speed");

            if (speed_upgrade_purchased(capped, btn_x, enter_pressed, cost)) {
                //reduce money by price and apply speed to current balls and future balls
                money -= cost;
                speed_levels[i]++;
                int new_spd = DEFAULT_SPEED_PPS + (speed_levels[i] - 1) * ADDITIONAL_SPEED_PPS;
                int old_spd = DEFAULT_SPEED_PPS + (speed_levels[i] - 2) * ADDITIONAL_SPEED_PPS;
                for (uint8_t b = 0; b < ball_count; b++) {
                    if (balls[b].type == (BallType)i) {
                        balls[b].vx = balls[b].vx / old_spd * new_spd;
                        balls[b].vy = balls[b].vy / old_spd * new_spd;
                    }
                }
            }
        }

        //power upgrade button
        uint64_t cost = power_price((BallType)i, power_levels[i] + 1);
        render_upgrade_btn(btn_x, MENU_INNER_Y + ROW_PWR_BTN_Y, lv_x,  MENU_INNER_Y + ROW_PWR_LVL_Y, power_levels[i], 0, cost, "Power");
        if (power_upgrade_purchased(btn_x, enter_pressed, cost)) {
            //reduce money by price and apply power to current balls and future balls
            money -= cost;
            power_levels[i]++;
        }

        //buy button
        render_buy_btn(btn_x, MENU_INNER_Y + ROW_BUY_Y, buy_price((BallType)i, ball_type_counts[i] + 1), enter_pressed, (BallType)i);

        //sell button
        uint8_t has_ball = ball_type_counts[i] > 0;
        if (render_button(btn_x, MENU_INNER_Y + ROW_SELL_Y, MENU_BTN_W, MENU_BTN_H, "Sell x1", has_ball ? COLOR_BTN_AFFORD : COLOR_BTN_CANT)) {
            cursor_on_any_button = 1;
            if (enter_pressed && has_ball) remove_ball((BallType)i);
        }
    }
}
//upgrade menu functions


//main functions
static void game_load(void)
{
    ti_var_t slot = ti_Open(SAVE_NAME, "r");
    if (!slot) return; //no save file, start fresh

    SaveData s;
    if (ti_Read(&s, sizeof(SaveData), 1, slot) == 1) {
        money = s.money;
        tile_level_health = s.tile_level_health;
        for (uint8_t i = 0; i < 5; i++) {
            speed_levels[i] = s.speed_levels[i];
            power_levels[i] = s.power_levels[i];
        }
        //re-spawn saved balls
        for (uint8_t i = 0; i < 5; i++) {
            for (uint8_t j = 0; j < s.ball_type_counts[i]; j++) {
                spawn_ball((BallType)i);
            }
        }
    }

    ti_Close(slot);
}

void game_save(void)
{
    SaveData s;
    s.money = money;
    s.tile_level_health = tile_level_health;
    for (uint8_t i = 0; i < 5; i++) {
        s.ball_type_counts[i] = ball_type_counts[i];
        s.speed_levels[i]     = speed_levels[i];
        s.power_levels[i]     = power_levels[i];
    }

    ti_var_t slot = ti_Open(SAVE_NAME, "w");
    if (slot) {
        ti_Write(&s, sizeof(SaveData), 1, slot);
        ti_SetArchiveStatus(1, slot);
        ti_Close(slot);
    }
}

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
    gfx_palette[COLOR_BTN_AFFORD]        = gfx_RGBTo1555(0x00, 0xFF, 0x00); //affordable button
    gfx_palette[COLOR_BTN_CANT]          = gfx_RGBTo1555(0xFF, 0x00, 0x00); //unaffordable button

    //cursor color
    gfx_palette[COLOR_CURSOR] = gfx_RGBTo1555(0x95, 0x00, 0xFF); //vibrant purple

    //set timer
    timer_id = 1;
    timer_Set(timer_id, 0xFFFFFFFF);
    timer_Enable(timer_id, TIMER_32K, TIMER_0INT, TIMER_DOWN);

    //initialize cursor position
    cursor.x = GFX_LCD_WIDTH / 2;
    cursor.y = GFX_LCD_HEIGHT / 2;

    //spawn in initial tiles
    spawn_tiles();

    //load previous game if applicable
    game_load();
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

    //render money, level, and ball amount
    gfx_SetTextFGColor(0x00);
    gfx_PrintStringXY("Money: ", MARGIN_TEXT_PADDING, MARGIN_TEXT_PADDING);
    gfx_PrintUInt(money, 1);
    gfx_PrintString("; Level: ");
    gfx_PrintUInt(tile_level_health, 1);
    gfx_PrintString("; Ball count: ");
    //color ball count red if MAX_BALLS
    if (ball_count == MAX_BALLS) gfx_SetTextFGColor(COLOR_BTN_CANT);
    gfx_PrintUInt(ball_count, 1);

    //render keybinds to help user navigate through the game
    gfx_SetTextFGColor(0x00);
    gfx_PrintStringXY("Upgrade: MODE; Move: Arrows; Quit: CLEAR", MARGIN_TEXT_PADDING, TILE_MARGIN_Y + TILE_AREA_Y + MARGIN_TEXT_PADDING);

    //render tiles
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            //don't render dead tiles
            if (tiles[r][c].health == 0) continue;

            //tile
            int tx = c * TILE_W;
            int ty = TILE_MARGIN_Y + r * TILE_H;
            gfx_SetColor(TILE_COLORS[(tiles[r][c].health - 1) % 10]);
            gfx_FillRectangle(tx, ty, TILE_W, TILE_H);

            //display health on tile (if tile on fire, surround health with '-')
            char health_str[16];
            uint8_t len = u32_to_str(tiles[r][c].health, health_str);

            if (tiles[r][c].on_fire) {
                // shift right and add dashes
                for (int i = len; i >= 0; i--) health_str[i + 1] = health_str[i];
                health_str[0] = '-';
                health_str[len + 1] = '-';
                health_str[len + 2] = '\0';
            }

            int text_x = tx + (TILE_W - gfx_GetStringWidth(health_str)) / 2;
            int text_y = ty + (TILE_H - 8) / 2;
            gfx_PrintStringXY(health_str, text_x, text_y);
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

    //render upgrade menu if toggled on
    if (menu == MENU_UPGRADE) render_upgrade_menu();

    //render cursor
    gfx_SetTextFGColor(COLOR_CURSOR);
    gfx_SetTextXY(cursor.x - 2, cursor.y - 2); // subtract 2 to roughly center cursor on position
    gfx_PrintChar(menu == MENU_UPGRADE && cursor_on_any_button ? '^' : '+');
}
//main functions
