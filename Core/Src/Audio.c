#include "audio.h"
#include <math.h>

// Target bin index k for each band. freq = k * (SAMPLE_RATE_HZ / WINDOW_SIZE).
static const uint16_t bandBin[NUM_BANDS] = { 2, 4, 8, 16, 32, 64, 96, 120 };

// Raw signal energy is naturally bass-heavy for almost all music. These
// weights boost higher bands so the display reflects "what's audible"
// rather than "what's loudest in raw volts". Tune by ear.
static const float bandWeight[NUM_BANDS] = {
    1.0f, 1.0f, 1.3f, 1.7f, 2.2f, 3.0f, 4.0f, 5.0f
};

// DMA fills this in the background at a fixed rate (paced by TIM3), forever.
static volatile uint16_t adcBuffer[WINDOW_SIZE];

static float   goertzelCoeff[NUM_BANDS];
static uint8_t bandLevel[NUM_BANDS];
static uint8_t overallLevel = 0;

// Smoothed (eased) height per band, kept as float so it can move in
// fractional steps between frames instead of jumping in whole-row jumps.
// This is what actually fixes "too twitchy" - each frame nudges toward
// the target instead of snapping to it.
static float smoothedLevel[NUM_BANDS];

// Peak detection state - runs on the raw target BEFORE smoothing, since
// smoothing lags too much for short percussive hits to ever reach a high
// threshold in time.
static uint8_t  wasAbovePeak[NUM_BANDS];
static uint32_t lastPeakTick[NUM_BANDS];
static uint8_t  peakFlag[NUM_BANDS];

// Asymmetric smoothing - the bar RISES fast so real peaks actually reach
// the top of the meter and are visible, but FALLS slowly so the display
// stays calm instead of twitching. This is how real VU meters behave.
// Raise ATTACK for snappier rises, lower RELEASE for a longer smooth decay.
#define ATTACK_FACTOR   0.75f
#define RELEASE_FACTOR  0.12f

void Audio_Init(ADC_HandleTypeDef *hadc)
{
    for (uint8_t b = 0; b < NUM_BANDS; b++)
    {
        goertzelCoeff[b]  = 2.0f * cosf(2.0f * (float)M_PI * (float)bandBin[b] / (float)WINDOW_SIZE);
        smoothedLevel[b]  = 0.0f;
        wasAbovePeak[b]   = 0;
        lastPeakTick[b]   = 0;
        peakFlag[b]       = 0;
    }

    HAL_ADC_Start_DMA(hadc, (uint32_t *)adcBuffer, WINDOW_SIZE);
}

// Standard Goertzel 3-term recurrence for one target bin across the window.
static float GoertzelPower(float coeff, uint16_t dcOffset)
{
    float s_prev  = 0.0f;
    float s_prev2 = 0.0f;

    for (uint16_t n = 0; n < WINDOW_SIZE; n++)
    {
        float sample = (float)((int32_t)adcBuffer[n] - (int32_t)dcOffset);

        float s = sample + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev  = s;
    }

    return (s_prev2 * s_prev2) + (s_prev * s_prev) - (coeff * s_prev * s_prev2);
}

void Audio_Update(void)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < WINDOW_SIZE; i++)
    {
        sum += adcBuffer[i];
    }
    uint16_t dcOffset = (uint16_t)(sum / WINDOW_SIZE);

    uint16_t peakDeviation = 0;
    for (uint16_t i = 0; i < WINDOW_SIZE; i++)
    {
        int32_t deviation = (int32_t)adcBuffer[i] - (int32_t)dcOffset;
        uint16_t absDeviation = (deviation < 0) ? (uint16_t)(-deviation) : (uint16_t)deviation;
        if (absDeviation > peakDeviation) peakDeviation = absDeviation;
    }
    uint32_t level = (peakDeviation * 100UL) / 600UL;
    if (level > 100) level = 100;
    overallLevel = (uint8_t)level;

    #define SILENCE_FLOOR  50000.0f
    #define CONTRAST_MULT  2.0f

    float powers[NUM_BANDS];
    float maxPower = 1.0f;

    for (uint8_t b = 0; b < NUM_BANDS; b++)
    {
        powers[b] = GoertzelPower(goertzelCoeff[b], dcOffset) * bandWeight[b];
        if (powers[b] > maxPower) maxPower = powers[b];
    }

    // Compute a TARGET height per band (0-7) - this is the raw, un-smoothed
    // reading. It's allowed to be twitchy on its own; the smoothing step
    // below is what actually stabilizes what gets displayed.
    uint8_t target[NUM_BANDS];

    if (maxPower < SILENCE_FLOOR)
    {
        for (uint8_t b = 0; b < NUM_BANDS; b++)
        {
            target[b] = 0;
        }
    }
    else
    {
        float avgPower = 0.0f;
        for (uint8_t b = 0; b < NUM_BANDS; b++)
        {
            avgPower += powers[b];
        }
        avgPower /= NUM_BANDS;

        float threshold = avgPower * CONTRAST_MULT;
        float span = maxPower - threshold;
        if (span < 1.0f) span = 1.0f;

        for (uint8_t b = 0; b < NUM_BANDS; b++)
        {
            if (powers[b] <= threshold)
            {
                target[b] = 0;
                continue;
            }

            float ratio = (powers[b] - threshold) / span;
            int level7 = (int)(ratio * 7.0f) + 1;
            if (level7 > 7) level7 = 7;
            target[b] = (uint8_t)level7;
        }
    }

    // Ease each band toward its target - fast on the way up so peaks
    // visibly reach the top, slow on the way down so it stays calm.
    for (uint8_t b = 0; b < NUM_BANDS; b++)
    {
        float factor = ((float)target[b] > smoothedLevel[b]) ? ATTACK_FACTOR : RELEASE_FACTOR;
        smoothedLevel[b] += ((float)target[b] - smoothedLevel[b]) * factor;

        int rounded = (int)(smoothedLevel[b] + 0.5f);
        if (rounded < 0) rounded = 0;
        if (rounded > 7) rounded = 7;
        bandLevel[b] = (uint8_t)rounded;
    }

    // Peak detection runs on the SMOOTHED value - the same number that
    // gets drawn on the meter. This is deliberate: previously it ran on
    // the raw reading, so notes could spawn while the visible bar was
    // only 2 rows tall. Now a note only drops when the bar you can
    // actually see climbs to PEAK_THRESHOLD_LEVEL.
    uint32_t nowTick = HAL_GetTick();
    for (uint8_t b = 0; b < NUM_BANDS; b++)
    {
        peakFlag[b] = 0;

        uint8_t above = (bandLevel[b] >= PEAK_THRESHOLD_LEVEL);
        if (above && !wasAbovePeak[b] && (nowTick - lastPeakTick[b]) >= PEAK_REFRACTORY_MS)
        {
            peakFlag[b]     = 1;
            lastPeakTick[b] = nowTick;
        }
        wasAbovePeak[b] = above;
    }
}

uint8_t Audio_GetLevel(void)
{
    return overallLevel;
}

uint8_t Audio_GetBandLevel(uint8_t band)
{
    if (band >= NUM_BANDS) return 0;
    return bandLevel[band];
}

uint8_t Audio_PeakDetected(uint8_t band)
{
    if (band >= NUM_BANDS) return 0;
    return peakFlag[band];
}
