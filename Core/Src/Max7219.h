#ifndef MAX7219_H
#define MAX7219_H

#include "stm32f1xx_hal.h"

// ---- Wiring ----
// SPI1_SCK  -> PA5
// SPI1_MOSI -> PA7
// CS        -> PA4 (manual GPIO)

#define MAX7219_CS_PORT      GPIOA
#define MAX7219_CS_PIN       GPIO_PIN_4

#define MAX7219_NUM_DEVICES  4
#define MAX7219_TOTAL_COLS   (MAX7219_NUM_DEVICES * 8)  // 32 columns across the whole strip

// NOTE ON THIS BOARD'S ORIENTATION:
// Register number (1-8) selects a COLUMN.
// Each bit inside the byte selects a ROW within that column (bit 0 = row 0).
// This was verified on the actual hardware - some MAX7219 modules are wired
// the other way around, so don't assume this holds for a different panel.

#define MAX7219_REG_NOOP        0x00
#define MAX7219_REG_DECODEMODE  0x09
#define MAX7219_REG_INTENSITY   0x0A
#define MAX7219_REG_SCANLIMIT   0x0B
#define MAX7219_REG_SHUTDOWN    0x0C
#define MAX7219_REG_DISPLAYTEST 0x0F

void MAX7219_Init(SPI_HandleTypeDef *hspi);
void MAX7219_WriteAll(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t data);
void MAX7219_SetIntensity(SPI_HandleTypeDef *hspi, uint8_t intensity); // 0x00 - 0x0F
void MAX7219_Clear(SPI_HandleTypeDef *hspi);

// Writes one column register (1-8) across every chained device.
// values[0] = first device in the chain.
void MAX7219_SetColumn(SPI_HandleTypeDef *hspi, uint8_t column, const uint8_t *values);

// Pushes a whole 32-column framebuffer to the display in one pass.
// frame[i] is the byte for absolute column i (0..31), where bit n = row n.
void MAX7219_DrawFrame(SPI_HandleTypeDef *hspi, const uint8_t *frame);

#endif
