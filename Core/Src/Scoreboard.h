#ifndef SCOREBOARD_H
#define SCOREBOARD_H

#include "stm32f1xx_hal.h"

void Scoreboard_Init(I2C_HandleTypeDef *hi2c);

// Call once per game loop, after Game_Update(). Reads score/streak/misses
// directly from game.c's Game_Get*() functions and redraws the OLED.
void Scoreboard_Update(I2C_HandleTypeDef *hi2c);

#endif
