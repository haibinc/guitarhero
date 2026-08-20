#include "scoreboard.h"
#include "ssd1306.h"
#include "game.h"

void Scoreboard_Init(I2C_HandleTypeDef *hi2c)
{
    SSD1306_Init(hi2c);
}

// The OLED redraw is a fully-blocking I2C transfer of the whole 128x64
// framebuffer (over 1000 bytes). Doing that every single main-loop
// iteration steals CPU time from button polling for no visible benefit -
// human eyes can't tell 10 updates/sec from 1000 updates/sec, but your
// fingers can absolutely feel the extra latency. Throttling this to a
// fixed rate frees up the rest of the loop (buttons, note scrolling) to
// run essentially as fast as the CPU allows.
#define SCOREBOARD_UPDATE_MS  100

void Scoreboard_Update(I2C_HandleTypeDef *hi2c)
{
    static uint32_t lastUpdateTick = 0;
    uint32_t now = HAL_GetTick();
    if ((now - lastUpdateTick) < SCOREBOARD_UPDATE_MS) return;
    lastUpdateTick = now;

    SSD1306_Clear();

    // Three labeled stats, one per line. DrawText returns the x position
    // right after the label, so the number chains on with no manual
    // offset math.
    uint8_t x;

    x = SSD1306_DrawText(0, 0, "SCORE", 2);
    SSD1306_DrawNumber(x + 4, 0, Game_GetScore(), 2);

    x = SSD1306_DrawText(0, 3, "HIT", 2);
    SSD1306_DrawNumber(x + 4, 3, Game_GetHits(), 2);

    x = SSD1306_DrawText(0, 6, "MISS", 2);
    SSD1306_DrawNumber(x + 4, 6, Game_GetMisses(), 2);

    // "PERFECT!" flashes on and off (not held solid) while the flag is
    // active, so it reads as an alert rather than blending into the
    // rest of the screen.
    if (Game_IsPerfectFlashActive())
    {
        uint8_t blinkOn = (HAL_GetTick() / 150) % 2;
        if (blinkOn)
        {
            SSD1306_DrawText(70, 6, "PERFECT!", 1);
        }
    }

    SSD1306_Display(hi2c);
}
