#include "audio.h"
#include <math.h>

// Target bin index k for each band. freq = k * (SAMPLE_RATE_HZ / WINDOW_SIZE).
static const uint16_t bandBin[NUM_BANDS] = { 2, 4, 8, 16, 32, 64, 96, 120 };

// Raw signal energy is naturally bass-heavy for almost all music - in
// theory. In practice this assumption depends heavily on your specific
// mic/gain setup, and boosting the wrong direction will make genuinely
// louder bass lose to artificially-boosted treble. Flattened to neutral
// (no compensation) for now - re-introduce a gentle boost only if testing
// with real bass-heavy audio shows treble is UNDER-represented once this
// is neutral, not over-represented like before.
static const float bandWeight[NUM_BANDS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
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

// Per-band resting/idle noise floor, measured automatically for a brief
// window right after boot (see NOISE_CALIBRATION_MS in Audio_Update). A
// band only counts as "real" once it clears ITS OWN baseline by
// BAND_NOISE_MARGIN - this is frequency-specific, unlike a single
// overall-loudness gate, which can't tell hum sitting inside one band's
// range apart from that band's frequency actually getting louder.
static float    bandNoiseFloor[NUM_BANDS];
static float    calibAccum[NUM_BANDS];
static uint16_t calibFrameCount;
static uint32_t calibEndTick;
static float    lastRawPower[NUM_BANDS];   // last computed powers[b], for debug readout
#define NOISE_CALIBRATION_MS  500
#define BAND_NOISE_MARGIN     4.0f

// A fixed narrowband interferer (mains hum, PSU switching noise, etc.)
// sits almost exactly on top of specific bands - typically the low ones -
// and can inflate THAT band's calibrated floor far above every other
// band's. Since the gate above is multiplicative, a hugely inflated floor
// makes the pass bar (floor * MARGIN) essentially unreachable by real
// signal, even though nearby "clean" bands pass easily. Capping every
// band's floor to a small multiple of the cleanest (lowest) band's floor
// stops one contaminated band from silently requiring 10-20x more real
// energy than its neighbors just because of where the hum happens to fall.
#define FLOOR_CAP_MULTIPLIER  3.0f

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
        bandNoiseFloor[b] = 0.0f;
        calibAccum[b]     = 0.0f;
    }

    calibFrameCount = 0;
    calibEndTick    = HAL_GetTick() + NOISE_CALIBRATION_MS;

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

    // Absolute loudness gate for the per-band shape below. The relative
    // ratio/span math further down always stretches whichever band is
    // momentarily loudest up toward the top of the 0-7 scale, EVEN when
    // every band's absolute energy is just noise - it only ever compares
    // bands against each other, never against a real "how loud is this"
    // reference. overallLevel (0-100) IS an absolute measure (raw ADC
    // swing), so we use it here to scale the relative shape down toward 0
    // as true volume drops, and back up to full strength once real audio
    // is actually present. Tune by ear against your own noise floor.
    #define OVERALL_GATE_MIN   15.0f
    #define OVERALL_GATE_FULL  16.0f

    float gateScale = ((float)overallLevel - OVERALL_GATE_MIN) / (OVERALL_GATE_FULL - OVERALL_GATE_MIN);
    if (gateScale < 0.0f) gateScale = 0.0f;
    if (gateScale > 1.0f) gateScale = 1.0f;

    float powers[NUM_BANDS];
    float maxPower = 1.0f;

    for (uint8_t b = 0; b < NUM_BANDS; b++)
    {
        powers[b] = GoertzelPower(goertzelCoeff[b], dcOffset) * bandWeight[b];
        if (powers[b] > maxPower) maxPower = powers[b];
        lastRawPower[b] = powers[b];   // exposed via Audio_GetBandRawPower for debugging
    }

    // Compute a TARGET height per band (0-7) - this is the raw, un-smoothed
    // reading. It's allowed to be twitchy on its own; the smoothing step
    // below is what actually stabilizes what gets displayed.
    uint8_t target[NUM_BANDS];

    // ---- Boot-time per-band noise calibration ----
    // For the first NOISE_CALIBRATION_MS after power-on (assumed quiet -
    // no song playing yet), every band's raw power is averaged into
    // bandNoiseFloor[b] instead of being displayed/used for peaks at all.
    // After that window, bandNoiseFloor[] never changes again.
    if (HAL_GetTick() < calibEndTick)
    {
        for (uint8_t b = 0; b < NUM_BANDS; b++)
        {
            calibAccum[b] += powers[b];
            target[b] = 0;   // stay dark while calibrating
        }
        calibFrameCount++;
    }
    else
    {
        if (calibFrameCount > 0)
        {
            for (uint8_t b = 0; b < NUM_BANDS; b++)
            {
                bandNoiseFloor[b] = calibAccum[b] / (float)calibFrameCount;
            }

            // Find the cleanest (lowest) floor and clamp every band to a
            // small multiple of it, so a hum-contaminated band can't end
            // up needing wildly more real signal than the rest.
            float minFloor = bandNoiseFloor[0];
            for (uint8_t b = 1; b < NUM_BANDS; b++)
            {
                if (bandNoiseFloor[b] < minFloor) minFloor = bandNoiseFloor[b];
            }
            if (minFloor < 1.0f) minFloor = 1.0f;   // avoid a degenerate near-zero cap

            float cap = minFloor * FLOOR_CAP_MULTIPLIER;
            for (uint8_t b = 0; b < NUM_BANDS; b++)
            {
                if (bandNoiseFloor[b] > cap) bandNoiseFloor[b] = cap;
            }

            calibFrameCount = 0;   // one-shot: never recompute after this
        }

        if (maxPower < SILENCE_FLOOR || gateScale <= 0.0f)
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
                // Frequency-specific gate: this band's OWN energy must clear
                // ITS OWN calibrated resting baseline by a solid margin -
                // regardless of how loud it looks next to the other bands,
                // and regardless of overall volume. This is what actually
                // answers "is THIS frequency being played right now."
                if (powers[b] < bandNoiseFloor[b] * BAND_NOISE_MARGIN)
                {
                    target[b] = 0;
                    continue;
                }

                if (powers[b] <= threshold)
                {
                    target[b] = 0;
                    continue;
                }

                // Relative shape (0-1), then pulled down by the absolute gate
                // so a quiet frame can't stretch its "loudest" band to the top.
                float ratio = ((powers[b] - threshold) / span) * gateScale;
                if (ratio <= 0.0f)
                {
                    target[b] = 0;
                    continue;
                }

                int level7 = (int)(ratio * 7.0f) + 1;
                if (level7 > 7) level7 = 7;
                target[b] = (uint8_t)level7;
            }
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

// ---- Debug-only getters ----
// Expose the raw numbers behind the gating decisions so thresholds can be
// picked from real hardware readings instead of guessed blind. Not used by
// gameplay code - only by the temporary OLED debug readout in Scoreboard.c.

uint32_t Audio_GetBandRawPower(uint8_t band)
{
    if (band >= NUM_BANDS) return 0;
    return (uint32_t)lastRawPower[band];
}

uint32_t Audio_GetBandNoiseFloor(uint8_t band)
{
    if (band >= NUM_BANDS) return 0;
    return (uint32_t)bandNoiseFloor[band];
}
