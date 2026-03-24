#include <stdlib.h>
#include <sys/timers.h>
#include <sys/rtc.h>
#include "game.h"

#define TILE_COLOR_BASE 16

static const uint8_t TILE_COLORS[10] = {
    TILE_COLOR_BASE + 0,
    TILE_COLOR_BASE + 1,
    TILE_COLOR_BASE + 2,
    TILE_COLOR_BASE + 3,
    TILE_COLOR_BASE + 4,
    TILE_COLOR_BASE + 5,
    TILE_COLOR_BASE + 6,
    TILE_COLOR_BASE + 7,
    TILE_COLOR_BASE + 8,
    TILE_COLOR_BASE + 9
};

static Ball balls[MAX_BALLS];
static uint8_t ball_count = 0;
static uint8_t timer_id;
static Tile tiles[ROWS][COLS];
static uint8_t tile_level_health = 1;
static uint8_t tiles_alive = 0;

static int rand_range(int min, int max)
{
    return min + (int)(random() % (unsigned)(max - min + 1));
}

static int32_t tile_collision(int dx, int dy, Ball *b)
{
    return (int32_t)dx * dx + (int32_t)dy * dy <= (int32_t)b->radius * b->radius;
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

                /* nearest point on tile rect to ball centre */
                int tx = c * TILE_W;
                int ty = r * TILE_H;
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

                //reduce health
                tiles[r][c].health--;
                if (tiles[r][c].health == 0) tiles_alive--;

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

static void spawn_ball(void)
{
    //don't spawn more than MAX_BALLS
    if (ball_count >= MAX_BALLS) return;

    //create new ball in center of screen with random velocity
    Ball *b = &balls[ball_count];
    b->radius = (uint8_t)rand_range(MIN_RADIUS, MAX_RADIUS);
    b->x = (SCREEN_W / 2) * FP_SCALE;
    b->y = (SCREEN_H / 2) * FP_SCALE;
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

    int speed_pps = rand_range(MIN_SPEED_PPS, MAX_SPEED_PPS);

    //scale: vx_fp = raw_vx / mag * speed_pps * FP_SCALE
    b->vx = ((int32_t)raw_vx * speed_pps * FP_SCALE) / mag;
    b->vy = ((int32_t)raw_vy * speed_pps * FP_SCALE) / mag;

    ball_count++;
}

void game_init(void)
{
    srandom((unsigned)rtc_Time());

    //gradient from cyan to red
    gfx_palette[TILE_COLOR_BASE + 0] = gfx_RGBTo1555(0x65, 0xFF, 0xFF); //light blue
    gfx_palette[TILE_COLOR_BASE + 1] = gfx_RGBTo1555(0x3B, 0xFF, 0xAA); //mint
    gfx_palette[TILE_COLOR_BASE + 2] = gfx_RGBTo1555(0x17, 0xFF, 0x45); //light green
    gfx_palette[TILE_COLOR_BASE + 3] = gfx_RGBTo1555(0x57, 0xFF, 0x1F); //greenish cyan
    gfx_palette[TILE_COLOR_BASE + 4] = gfx_RGBTo1555(0xA6, 0xFF, 0x00); //lime green
    gfx_palette[TILE_COLOR_BASE + 5] = gfx_RGBTo1555(0xE5, 0xFF, 0x00); //yellow
    gfx_palette[TILE_COLOR_BASE + 6] = gfx_RGBTo1555(0xFF, 0xD9, 0x00); //orangeish yellow
    gfx_palette[TILE_COLOR_BASE + 7] = gfx_RGBTo1555(0xFF, 0x8C, 0x00); //orange
    gfx_palette[TILE_COLOR_BASE + 8] = gfx_RGBTo1555(0xFF, 0x40, 0x00); //red-orange
    gfx_palette[TILE_COLOR_BASE + 9] = gfx_RGBTo1555(0xFF, 0x00, 0x00); //red

    timer_id = 1;
    timer_Set(timer_id, 0xFFFFFFFF);
    timer_Enable(timer_id, TIMER_32K, TIMER_0INT, TIMER_DOWN);

    spawn_ball();
    spawn_tiles();
}

void game_update(void)
{
    static uint32_t last_ticks = 0xFFFFFFFF;
    uint32_t now = timer_Get(timer_id);
    uint32_t ticks = last_ticks - now;
    last_ticks = now;

    if (ticks > 3276) ticks = 3276;

    //if enter pressed, spawn ball
    {
        static bool enter_was_down = false;
        if (kb_IsDown(kb_KeyEnter)) {
            if (!enter_was_down) {
                spawn_ball();
                enter_was_down = true;
            }
        } else {
            enter_was_down = false;
        }
    }

    //move and bounce each ball
    for (uint8_t i = 0; i < ball_count; i++) {
        Ball *b = &balls[i];
        b->x += b->vx * (int32_t)ticks / 32768;
        b->y += b->vy * (int32_t)ticks / 32768;

        int32_t x_min = (int32_t)b->radius * FP_SCALE;
        int32_t x_max = (int32_t)(SCREEN_W - b->radius) * FP_SCALE;
        int32_t y_min = (int32_t)b->radius * FP_SCALE;
        int32_t y_max = (int32_t)(SCREEN_H - b->radius) * FP_SCALE;

        if (b->x < x_min) { b->x = x_min; b->vx = (b->vx < 0) ? -b->vx : b->vx; }
        if (b->x > x_max) { b->x = x_max; b->vx = (b->vx > 0) ? -b->vx : b->vx; }
        if (b->y < y_min) { b->y = y_min; b->vy = (b->vy < 0) ? -b->vy : b->vy; }
        if (b->y > y_max) { b->y = y_max; b->vy = (b->vy > 0) ? -b->vy : b->vy; }
    }

    update_tiles();
}

void game_draw(void)
{
    //update game values
    game_update();

    //render background
    gfx_FillScreen(0xFF);

    //render tiles
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            //don't render dead tiles
            if (tiles[r][c].health == 0) continue;

            int tx = c * TILE_W;
            int ty = r * TILE_H;

            //tile
            gfx_SetColor(TILE_COLORS[(tiles[r][c].health - 1) % 10]);
            gfx_FillRectangle(tx, ty, TILE_W, TILE_H);

            //border
            gfx_SetColor(0xFF);
            gfx_Rectangle(tx, ty, TILE_W, TILE_H);

            //health
            gfx_SetTextXY(tx + TILE_W / 2 - 3, ty + TILE_H / 2 - 4);
            gfx_SetTextFGColor(0x00);
            gfx_SetTextTransparentColor(0xFF);
            gfx_PrintUInt(tiles[r][c].health, 1);
        }
    }

    //render balls
    gfx_SetColor(0x10);
    for (uint8_t i = 0; i < ball_count; i++) {
        Ball *b = &balls[i];
        int px = (int)(b->x / FP_SCALE);
        int py = (int)(b->y / FP_SCALE);
        gfx_FillCircle(px, py, b->radius);
    }
}
