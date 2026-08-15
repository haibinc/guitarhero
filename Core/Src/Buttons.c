#include "buttons.h"

#define DEBOUNCE_MS 5

GPIO_TypeDef* const BUTTON_PORTS[NUM_BUTTONS] = {
    GPIOA, GPIOA, GPIOA, GPIOA,
    GPIOB, GPIOB, GPIOB, GPIOB
};

const uint16_t BUTTON_PINS[NUM_BUTTONS] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_5, GPIO_PIN_6
};

static uint8_t  stableState[NUM_BUTTONS];    // debounced: 1 = pressed
static uint8_t  lastRawState[NUM_BUTTONS];
static uint32_t lastChangeTime[NUM_BUTTONS];
static uint8_t  justPressed[NUM_BUTTONS];

void Buttons_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    {
        GPIO_InitStruct.Pin = BUTTON_PINS[i];
        HAL_GPIO_Init(BUTTON_PORTS[i], &GPIO_InitStruct);

        stableState[i]    = 0;
        lastRawState[i]   = 0;
        lastChangeTime[i] = 0;
        justPressed[i]    = 0;
    }
}

// Instead of blocking with HAL_Delay to ride out contact bounce, this
// timestamps every raw change and only trusts a reading once it has held
// steady for DEBOUNCE_MS. Nothing blocks, so the game loop keeps its timing.
void Buttons_Update(void)
{
    uint32_t now = HAL_GetTick();

    for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    {
        justPressed[i] = 0;

        uint8_t raw = (HAL_GPIO_ReadPin(BUTTON_PORTS[i], BUTTON_PINS[i]) == GPIO_PIN_RESET) ? 1 : 0;

        if (raw != lastRawState[i])
        {
            // Still bouncing - restart the settle timer.
            lastRawState[i]   = raw;
            lastChangeTime[i] = now;
        }
        else if ((now - lastChangeTime[i]) >= DEBOUNCE_MS)
        {
            if (stableState[i] != raw)
            {
                stableState[i] = raw;
                if (raw)
                {
                    justPressed[i] = 1;   // rising edge of a real press
                }
            }
        }
    }
}

uint8_t Buttons_IsDown(uint8_t index)
{
    if (index >= NUM_BUTTONS) return 0;
    return stableState[index];
}

uint8_t Buttons_WasJustPressed(uint8_t index)
{
    if (index >= NUM_BUTTONS) return 0;
    return justPressed[index];
}
