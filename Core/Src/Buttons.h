#ifndef BUTTONS_H
#define BUTTONS_H

#include "stm32f1xx_hal.h"

#define NUM_BUTTONS 8

// Buttons wired between the pin and GND, using internal pull-ups.
// Not pressed = HIGH, pressed = LOW.
// Pins avoid SPI1 (PA4/5/7), SWD (PA13/14), LED (PC13),
// BOOT1-shared PB2, and JTAG-shared PA15/PB3/PB4.
//   0:PA0  1:PA1  2:PA2  3:PA3  4:PB0  5:PB1  6:PB10  7:PB5

extern GPIO_TypeDef* const BUTTON_PORTS[NUM_BUTTONS];
extern const uint16_t BUTTON_PINS[NUM_BUTTONS];

void Buttons_Init(void);

// Call once per game loop. Non-blocking - no delays inside.
void Buttons_Update(void);

// Held down right now.
uint8_t Buttons_IsDown(uint8_t index);

// True only on the loop iteration where the press first registered.
// This is what you want for hit detection - holding a button shouldn't
// count as repeated hits.
uint8_t Buttons_WasJustPressed(uint8_t index);

#endif
