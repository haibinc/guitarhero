#ifndef GAME_H
#define GAME_H

#include "stm32f1xx_hal.h"
#include "max7219.h"

// ---- Tune these to match your song ----

// Beats per minute of whatever you're playing alongside the game.
#define GAME_BPM              25

// How many beats a note spends travelling before it reaches the hit zone.
// Bigger = notes visible longer = easier. This is what makes notes ARRIVE
// on the beat rather than spawn on it.
#define NOTE_TRAVEL_BEATS     4

// How close to the hit column a note must be for a press to count.
// 0 = perfect only, 1 = one column of slack either side.
#define HIT_TOLERANCE         4

// Set to 1 if notes should count DOWN from device 0 to the last device
// instead of counting UP from device 0 to the last device. Flip this if
// notes spawn at the wrong physical end of the chain.
#define FLIP_TRACK_DIRECTION  0

// ---- Derived (don't edit) ----
#define MS_PER_BEAT           (60000 / GAME_BPM)
#define MAX_NOTES             8

// The track walks one ROW at a time, top row of the top device down to
// the bottom row of device 1 (the hit zone) - device 0 is reserved
// entirely for the audio VU meter and never used by the note track.
// 24 steps total = 3 devices x 8 rows each.
#define TRACK_LENGTH           ((MAX7219_NUM_DEVICES - 1) * 8)

void Game_Init(SPI_HandleTypeDef *hspi, ADC_HandleTypeDef *hadc);

// Call this as fast as possible in the main loop. It handles its own
// timing internally off HAL_GetTick(), so calling it more often just
// makes input response snappier.
void Game_Update(SPI_HandleTypeDef *hspi);

uint32_t Game_GetScore(void);
uint32_t Game_GetStreak(void);
uint32_t Game_GetHits(void);
uint32_t Game_GetMisses(void);

// True for a short window after a dead-center ("perfect") hit, meant for
// UI feedback like a flashing "PERFECT!" label - not tied to any LED
// effect, just a timed flag the display code can poll.
uint8_t Game_IsPerfectFlashActive(void);

#endif
