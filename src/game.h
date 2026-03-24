#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>
#include <graphx.h>
#include <keypadc.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define MAX_BALLS 100
#define FP_SCALE 256
#define MIN_SPEED_PPS 40
#define MAX_SPEED_PPS 100
#define MIN_RADIUS 4
#define MAX_RADIUS 6

#define ROWS 16
#define COLS 12
#define TILE_W (SCREEN_W / COLS)
#define TILE_H (SCREEN_H / ROWS)
#define TILE_SPAWN_PCNT 45

typedef struct {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint8_t radius;
} Ball;

typedef struct {
    uint8_t health;
} Tile;

void game_init(void);
void game_update(void);
void game_draw(void);

#endif