#ifndef SSD1306_H
#define SSD1306_H

#include "stm32f1xx_hal.h"

#define SSD1306_I2C_ADDR   0x3C
#define SSD1306_WIDTH      128
#define SSD1306_HEIGHT     64
#define SSD1306_PAGES      (SSD1306_HEIGHT / 8)

void SSD1306_Init(I2C_HandleTypeDef *hi2c);
void SSD1306_Clear(void);
void SSD1306_Display(I2C_HandleTypeDef *hi2c);

// Draws one character at pixel column x, page row (0-7). Supports
// digits 0-9, letters S C O R E H I T M P F, '!', and space (space
// just advances the cursor, draws nothing). Unsupported characters
// are silently skipped.
void SSD1306_DrawChar(uint8_t x, uint8_t page, char ch, uint8_t scale);

// Draws a null-terminated string left to right starting at (x, page).
// Returns the x position just past the last character drawn, so you
// can chain a DrawNumber() right after a label without recalculating
// the offset by hand.
uint8_t SSD1306_DrawText(uint8_t x, uint8_t page, const char *text, uint8_t scale);

// Draws an unsigned integer left to right starting at (x, page).
void SSD1306_DrawNumber(uint8_t x, uint8_t page, uint32_t value, uint8_t scale);

#endif
