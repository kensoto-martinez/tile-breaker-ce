#ifndef GAME_H
#define GAME_H

#include <graphx.h>
#include <keypadc.h>

#define MENU_BACKGROUND_MARGIN 30
#define COLOR_BASE 16
#define MARGIN_TEXT_PADDING 3
#define FP_SCALE 256
#define DEFAULT_SPEED_PPS 20
#define ADDITIONAL_SPEED_PPS 20
#define MAX_BALLS 20
#define ROWS 11
#define COLS 9
#define TILE_MARGIN_Y 15
#define TILE_AREA_Y (GFX_LCD_HEIGHT - TILE_MARGIN_Y * 2) //TILE_MARGIN_Y * 2 for the top and bottom margins
#define TILE_W (GFX_LCD_WIDTH / COLS)
#define TILE_H (TILE_AREA_Y / ROWS)
#define TILE_SPAWN_PCNT 40

typedef enum {
    BALL_BASIC,
    BALL_PLASMA,
    BALL_SNIPER,
    BALL_HEAT,
    BALL_CANNONBALL
} BallType;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint8_t radius;
    uint16_t color;
    BallType type;
} Ball;

typedef struct {
    uint8_t health;
} Tile;

typedef struct {
    int x;
    int y;
} Cursor;

typedef enum {
    MENU_NONE,
    MENU_UPGRADE
} Menu;

typedef enum {
    COLOR_BASIC_BALL        = COLOR_BASE + 10,
    COLOR_PLASMA_BALL       = COLOR_BASE + 11,
    COLOR_SNIPER_BALL       = COLOR_BASE + 12,
    COLOR_HEAT_BALL         = COLOR_BASE + 13,
    COLOR_CANNONBALL        = COLOR_BASE + 14,
    BACKGROUND_COLOR_GAME   = COLOR_BASE + 15,
    BACKGROUND_COLOR_MARGIN = COLOR_BASE + 16,
    BACKGROUND_COLOR_MENU   = COLOR_BASE + 17
} Colors;

void game_init(void);
void game_draw(void);

#endif
