#ifndef GAME_H
#define GAME_H

#include <graphx.h>
#include <keypadc.h>

#define SAVE_NAME "TLEBRKRSAVE"

#define MENU_BACKGROUND_MARGIN 30
#define COLOR_BASE 16
#define MARGIN_TEXT_PADDING 3

#define CURSOR_SPEED_PPS_GAME 4
#define CURSOR_SPEED_PPS_MENU 8
#define FP_SCALE 256
#define DEFAULT_SPEED_PPS 20
#define ADDITIONAL_SPEED_PPS 20
#define SPEED_LEVEL_MAX 10
#define FIRE_TICK_SEC 1
#define FIRE_TICK_TRIGGER (int)32768 / (1 / FIRE_TICK_SEC)
#define FIRE_SPREAD_CHANCE_PCNT 20 //per fire tick
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
    BALL_CANNONBALL,
    BALL_CURSOR
} BallType;

typedef enum {
    MENU_NONE,
    MENU_UPGRADE
} Menu;

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
    uint16_t health;
    int on_fire;
} Tile;

typedef struct {
    int x;
    int y;
} Cursor;

typedef struct {
    uint64_t money;
    uint16_t tile_level_health;
    uint8_t  ball_type_counts[5];
    uint8_t  speed_levels[5];
    uint8_t  power_levels[5];
} SaveData;

typedef enum {
    COLOR_BASIC_BALL        = COLOR_BASE + 10,
    COLOR_PLASMA_BALL       = COLOR_BASE + 11,
    COLOR_SNIPER_BALL       = COLOR_BASE + 12,
    COLOR_HEAT_BALL         = COLOR_BASE + 13,
    COLOR_CANNONBALL        = COLOR_BASE + 14,
    BACKGROUND_COLOR_GAME   = COLOR_BASE + 15,
    BACKGROUND_COLOR_MARGIN = COLOR_BASE + 16,
    BACKGROUND_COLOR_MENU   = COLOR_BASE + 17,
    COLOR_BTN_AFFORD        = COLOR_BASE + 18,
    COLOR_BTN_CANT          = COLOR_BASE + 19,
    COLOR_CURSOR            = COLOR_BASE + 20
} Colors;

void game_save(void);
void game_init(void);
void game_draw(void);

#endif
