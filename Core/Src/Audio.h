#ifndef AUDIO_H
#define AUDIO_H

#include "stm32f1xx_hal.h"

#define NUM_BANDS       8
#define WINDOW_SIZE     256   // samples analyzed per band update

// Sample rate the ADC is actually running at - MUST match whatever
// you configured TIM3/ADC1 to trigger at in CubeMX. If these don't
// match, every band's target frequency is silently wrong.
#define SAMPLE_RATE_HZ  8000

void Audio_Init(ADC_HandleTypeDef *hadc);

// Call once per game loop. Re-runs Goertzel on the latest window.
void Audio_Update(void);

// Overall loudness, 0-100 (same as before - still useful for
// brightness/global effects).
uint8_t Audio_GetLevel(void);

// Energy in band `band` (0-7), scaled to 0-7 so it maps directly
// onto "how many rows to light" for a VU-meter style display.
// Smoothed frame-to-frame so it doesn't flicker.
uint8_t Audio_GetBandLevel(uint8_t band);

// True for exactly one Update() call when band `band`'s VISIBLE bar
// (the same smoothed value Audio_GetBandLevel returns) climbs to
// PEAK_THRESHOLD_LEVEL. What you see on the meter is exactly what
// spawns a note - no hidden separate signal.
uint8_t Audio_PeakDetected(uint8_t band);

// How tall (0-7) the visible bar must get to count as a peak. At 6, the
// bar has to nearly fill the device before a note drops.
#define PEAK_THRESHOLD_LEVEL   6

// Minimum time between two peaks on the SAME band.
#define PEAK_REFRACTORY_MS     400

#endif
