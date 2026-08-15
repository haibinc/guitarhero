#include "ssd1306.h"
#include <stddef.h>

// Column-major, hand-designed and round-trip verified 5x7 font.
// bit0 = top row of the glyph, bit6 = bottom row.
static const uint8_t digitFont[10][5] = {
    { 0x7F, 0x41, 0x41, 0x41, 0x7F }, // '0'
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, // '1'
    { 0x79, 0x49, 0x49, 0x49, 0x4F }, // '2'
    { 0x49, 0x49, 0x49, 0x49, 0x7F }, // '3'
    { 0x0F, 0x08, 0x08, 0x08, 0x7F }, // '4'
    { 0x4F, 0x49, 0x49, 0x49, 0x79 }, // '5'
    { 0x7F, 0x49, 0x49, 0x49, 0x79 }, // '6'
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, // '7'
    { 0x7F, 0x49, 0x49, 0x49, 0x7F }, // '8'
    { 0x4F, 0x49, 0x49, 0x49, 0x7F }, // '9'
};

// Letters needed for "SCORE", "HIT", "MISS", "PERFECT!" labels.
// Only these specific characters are supported - add more here if you
// need other labels later, following the same hand-verify process.
typedef struct { char ch; uint8_t cols[5]; } LetterGlyph;

static const LetterGlyph letterFont[] = {
    { 'S', { 0x07, 0x49, 0x49, 0x49, 0x70 } },
    { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x41 } },
    { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
    { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
    { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
    { 'I', { 0x41, 0x41, 0x7F, 0x41, 0x41 } },
    { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
    { 'M', { 0x7F, 0x02, 0x04, 0x02, 0x7F } },
    { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
    { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
    { '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } },
};
#define NUM_LETTER_GLYPHS (sizeof(letterFont) / sizeof(letterFont[0]))

static uint8_t framebuffer[SSD1306_WIDTH * SSD1306_PAGES];

static void WriteCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    HAL_I2C_Master_Transmit(hi2c, SSD1306_I2C_ADDR << 1, buf, 2, HAL_MAX_DELAY);
}

void SSD1306_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_Delay(100);

    WriteCmd(hi2c, 0xAE);
    WriteCmd(hi2c, 0xD5); WriteCmd(hi2c, 0x80);
    WriteCmd(hi2c, 0xA8); WriteCmd(hi2c, 0x3F);
    WriteCmd(hi2c, 0xD3); WriteCmd(hi2c, 0x00);
    WriteCmd(hi2c, 0x40);
    WriteCmd(hi2c, 0x8D); WriteCmd(hi2c, 0x14);
    WriteCmd(hi2c, 0x20); WriteCmd(hi2c, 0x00);
    WriteCmd(hi2c, 0xA1);
    WriteCmd(hi2c, 0xC8);
    WriteCmd(hi2c, 0xDA); WriteCmd(hi2c, 0x12);
    WriteCmd(hi2c, 0x81); WriteCmd(hi2c, 0xCF);
    WriteCmd(hi2c, 0xD9); WriteCmd(hi2c, 0xF1);
    WriteCmd(hi2c, 0xDB); WriteCmd(hi2c, 0x40);
    WriteCmd(hi2c, 0xA4);
    WriteCmd(hi2c, 0xA6);
    WriteCmd(hi2c, 0xAF);

    SSD1306_Clear();
}

void SSD1306_Clear(void)
{
    for (uint16_t i = 0; i < sizeof(framebuffer); i++)
    {
        framebuffer[i] = 0x00;
    }
}

static void SetPixel(uint8_t x, uint8_t y)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
    uint8_t page = y / 8;
    uint8_t bit  = y % 8;
    framebuffer[page * SSD1306_WIDTH + x] |= (1 << bit);
}

// Returns a pointer to a glyph's 5 column bytes, or NULL if unsupported.
static const uint8_t *FindGlyph(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return digitFont[ch - '0'];
    }
    for (uint8_t i = 0; i < NUM_LETTER_GLYPHS; i++)
    {
        if (letterFont[i].ch == ch)
        {
            return letterFont[i].cols;
        }
    }
    return NULL; // includes space, and anything not in the font
}

static void DrawGlyph(uint8_t x, uint8_t page, const uint8_t *glyph, uint8_t scale)
{
    uint8_t baseY = page * 8;
    for (uint8_t col = 0; col < 5; col++)
    {
        uint8_t colBits = glyph[col];
        for (uint8_t row = 0; row < 7; row++)
        {
            if (colBits & (1 << row))
            {
                for (uint8_t sy = 0; sy < scale; sy++)
                {
                    for (uint8_t sx = 0; sx < scale; sx++)
                    {
                        SetPixel(x + col * scale + sx, baseY + row * scale + sy);
                    }
                }
            }
        }
    }
}

void SSD1306_DrawChar(uint8_t x, uint8_t page, char ch, uint8_t scale)
{
    if (scale == 0) scale = 1;
    const uint8_t *glyph = FindGlyph(ch);
    if (glyph != NULL)
    {
        DrawGlyph(x, page, glyph, scale);
    }
    // space, or unsupported char: draw nothing - caller still advances cursor
}

uint8_t SSD1306_DrawText(uint8_t x, uint8_t page, const char *text, uint8_t scale)
{
    if (scale == 0) scale = 1;
    uint8_t glyphAdvance = (5 * scale) + scale; // glyph width + 1-scaled-px gap
    uint8_t cursorX = x;

    while (*text != '\0')
    {
        SSD1306_DrawChar(cursorX, page, *text, scale);
        cursorX += glyphAdvance;
        text++;
    }
    return cursorX;
}

void SSD1306_DrawNumber(uint8_t x, uint8_t page, uint32_t value, uint8_t scale)
{
    char digits[10];
    uint8_t count = 0;

    if (value == 0)
    {
        digits[count++] = '0';
    }
    else
    {
        while (value > 0 && count < 10)
        {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }

    uint8_t glyphWidth = (5 * scale) + scale;
    uint8_t cursorX = x;

    for (int8_t i = count - 1; i >= 0; i--)
    {
        SSD1306_DrawChar(cursorX, page, digits[i], scale);
        cursorX += glyphWidth;
    }
}

void SSD1306_Display(I2C_HandleTypeDef *hi2c)
{
    WriteCmd(hi2c, 0x21); WriteCmd(hi2c, 0); WriteCmd(hi2c, SSD1306_WIDTH - 1);
    WriteCmd(hi2c, 0x22); WriteCmd(hi2c, 0); WriteCmd(hi2c, SSD1306_PAGES - 1);

    uint8_t txBuf[SSD1306_WIDTH + 1];
    txBuf[0] = 0x40;

    for (uint8_t page = 0; page < SSD1306_PAGES; page++)
    {
        for (uint8_t x = 0; x < SSD1306_WIDTH; x++)
        {
            txBuf[1 + x] = framebuffer[page * SSD1306_WIDTH + x];
        }
        HAL_I2C_Master_Transmit(hi2c, SSD1306_I2C_ADDR << 1, txBuf, SSD1306_WIDTH + 1, HAL_MAX_DELAY);
    }
}
