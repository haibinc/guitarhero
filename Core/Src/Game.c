#include "game.h"
#include "max7219.h"
#include "buttons.h"
#include "audio.h"

// ---------------------------------------------------------------------------
// TRACK LAYOUT
//
// Device 3 (top chip) is reserved entirely for the audio VU meter.
// Falling notes live on devices 0, 1, 2 (24 positions: 3 devices x 8
// rows). Top row of device 2 - directly under the VU meter - is the
// spawn point, so notes visibly originate right where the meter lives.
// Bottom row of device 0 is the hit zone.
// ---------------------------------------------------------------------------

#if FLIP_TRACK_DIRECTION
  #define HIT_POS       (TRACK_LENGTH - 1)
  #define SPAWN_POS     0
  #define STEP_DIR      (+1)
  #define PAST_HIT(p)   ((p) > HIT_POS + HIT_TOLERANCE)
#else
  #define HIT_POS       0                    // device 0, row 0 = hit zone
  #define SPAWN_POS     (TRACK_LENGTH - 1)    // device 2, row 7 = spawn, under the VU meter
  #define STEP_DIR      (-1)
  #define PAST_HIT(p)   ((p) < HIT_POS - HIT_TOLERANCE)
#endif

#define STEP_MS  ((NOTE_TRAVEL_BEATS * MS_PER_BEAT) / TRACK_LENGTH)

#define LANE_SPAWN_COOLDOWN_MS  300

// Device 3 (top) is the VU meter's dedicated device - the note track
// (below) never writes here, and this constant is what Render() uses
// to target it directly.
#define VU_DEVICE  3

static void PlotCell(uint8_t *frame, uint8_t lane, int8_t pos)
{
    if (pos < 0 || pos >= TRACK_LENGTH) return;
    if (lane >= NUM_BUTTONS) return;

    uint8_t device = (uint8_t)pos / 8;   // devices 0, 1, 2 only
    uint8_t row    = (uint8_t)pos % 8;
    uint8_t absCol = device * 8 + lane;

    if (absCol < MAX7219_TOTAL_COLS)
    {
        frame[absCol] |= (1 << row);
    }
}

// ---------------------------------------------------------------------------
// Notes used to come from a fixed chart array timed by GAME_BPM. Now they
// spawn directly from Audio_PeakDetected() in Game_Update - a note in a
// lane only appears when that lane's own frequency band actually spikes
// in the real audio, so gameplay follows the music instead of a script.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

typedef struct {
    uint8_t active;
    uint8_t lane;
    int8_t  pos;
} Note;

static Note     notes[MAX_NOTES];

static uint32_t score;
static uint32_t streak;   // current consecutive hits - resets to 0 on a miss, used for score bonus
static uint32_t hits;     // TOTAL hits ever - never resets during gameplay, unlike streak
static uint32_t misses;

static uint32_t lastStepTick;
static uint32_t lastSpawnTick[NUM_BUTTONS];

static uint32_t flashUntilTick;
static uint8_t  flashLane;

// Timed flag for UI feedback on a dead-center hit - no LED effect,
// just something the display code can check.
static uint32_t perfectFlashUntilTick;
#define PERFECT_FLASH_DURATION_MS  600

// Full-width "score sweep" - a bar spanning every lane that races from
// the hit zone up to the top of the track on any successful hit. This
// is independent of flashLane (which only lights the one lane that was
// hit) and uses its own fast timing, not the note-scroll STEP_MS.
static uint8_t  sweepActive;
static uint32_t sweepStartTick;
#define SWEEP_STEP_MS  15   // fast - much quicker than normal note scrolling

static void SpawnNote(uint8_t lane, uint32_t now)
{
    if (lane >= NUM_BUTTONS) return;

    // Per-lane cooldown - if this lane already spawned recently, skip it
    // even if the chart asks for it again right away.
    if ((now - lastSpawnTick[lane]) < LANE_SPAWN_COOLDOWN_MS) return;

    for (uint8_t i = 0; i < MAX_NOTES; i++)
    {
        if (!notes[i].active)
        {
            notes[i].active = 1;
            notes[i].lane   = lane;
            notes[i].pos    = SPAWN_POS;
            lastSpawnTick[lane] = now;
            return;
        }
    }
    // No free slot - drop the note rather than overwrite a live one.
}

static void AdvanceNotes(void)
{
    for (uint8_t i = 0; i < MAX_NOTES; i++)
    {
        if (!notes[i].active) continue;

        notes[i].pos += STEP_DIR;

        if (PAST_HIT(notes[i].pos))
        {
            notes[i].active = 0;
            misses++;
            streak = 0;
        }
    }
}

// Returns 0 = miss (nothing there), 1 = hit, 2 = PERFECT (dead center).
static uint8_t TryHit(uint8_t lane)
{
    for (uint8_t i = 0; i < MAX_NOTES; i++)
    {
        if (!notes[i].active) continue;
        if (notes[i].lane != lane) continue;

        int16_t distance = (int16_t)notes[i].pos - (int16_t)HIT_POS;
        if (distance < 0) distance = -distance;

        if (distance <= HIT_TOLERANCE)
        {
            notes[i].active = 0;
            score += 10 + (streak * 2);
            streak++;
            hits++;
            return (distance == 0) ? 2 : 1;
        }
    }

    streak = 0;
    return 0;
}

static void Render(SPI_HandleTypeDef *hspi)
{
    uint8_t frame[MAX7219_TOTAL_COLS];
    for (uint8_t i = 0; i < MAX7219_TOTAL_COLS; i++)
    {
        frame[i] = 0x00;
    }

    for (uint8_t i = 0; i < MAX_NOTES; i++)
    {
        if (!notes[i].active) continue;
        PlotCell(frame, notes[i].lane, notes[i].pos);
    }

    for (uint8_t lane = 0; lane < NUM_BUTTONS; lane++)
    {
        if (Buttons_IsDown(lane))
        {
            PlotCell(frame, lane, HIT_POS);
        }
    }

    if (HAL_GetTick() < flashUntilTick)
    {
        for (int8_t p = 0; p < TRACK_LENGTH; p++)
        {
            PlotCell(frame, flashLane, p);
        }
    }

    // Score sweep - a bar spanning every lane, racing from the hit zone
    // up to the top of the track. Position is derived from elapsed time,
    // not the note-scroll tick, so it moves independently and fast.
    if (sweepActive)
    {
        uint32_t elapsed  = HAL_GetTick() - sweepStartTick;
        int32_t  sweepPos = elapsed / SWEEP_STEP_MS; // counts up from 0

        if (sweepPos >= TRACK_LENGTH)
        {
            sweepActive = 0;
        }
        else
        {
            // Always travel from HIT_POS toward SPAWN_POS (bottom to top,
            // regardless of which way FLIP_TRACK_DIRECTION points).
            int8_t pos = HIT_POS + (sweepPos * STEP_DIR * -1);
            for (uint8_t lane = 0; lane < NUM_BUTTONS; lane++)
            {
                PlotCell(frame, lane, pos);
            }
        }
    }

    // Audio VU meter - device 3 (top) exclusively, smoothed heights.
    // Fills from the TOP row downward as volume increases (row 7 = top,
    // same convention the note track already uses), so a louder band
    // visually "drops" further down from the top edge.
    for (uint8_t lane = 0; lane < NUM_BUTTONS; lane++)
    {
        uint8_t bandHeight = Audio_GetBandLevel(lane);
        for (uint8_t row = 0; row < bandHeight; row++)
        {
            uint8_t absCol = VU_DEVICE * 8 + lane;
            if (absCol < MAX7219_TOTAL_COLS)
            {
                frame[absCol] |= (1 << (7 - row));
            }
        }
    }

    MAX7219_DrawFrame(hspi, frame);
}

void Game_Init(SPI_HandleTypeDef *hspi, ADC_HandleTypeDef *hadc)
{
    for (uint8_t i = 0; i < MAX_NOTES; i++) notes[i].active = 0;

    score  = 0;
    streak = 0;
    hits   = 0;
    misses = 0;

    flashUntilTick = 0;
    flashLane      = 0;
    perfectFlashUntilTick = 0;

    sweepActive    = 0;
    sweepStartTick = 0;

    uint32_t now = HAL_GetTick();
    lastStepTick = now;

    for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    {
        lastSpawnTick[i] = 0;
    }

    Audio_Init(hadc);
    MAX7219_Clear(hspi);
}

void Game_Update(SPI_HandleTypeDef *hspi)
{
    uint32_t now = HAL_GetTick();

    // 0. Audio
    Audio_Update();
    uint8_t level = Audio_GetLevel();
    MAX7219_SetIntensity(hspi, (level * 15) / 100);

    // 1. Input
    Buttons_Update();

    for (uint8_t lane = 0; lane < NUM_BUTTONS; lane++)
    {
        if (Buttons_WasJustPressed(lane))
        {
            uint8_t result = TryHit(lane);
            if (result > 0)
            {
                flashLane      = lane;
                flashUntilTick = now + 60;

                sweepActive    = 1;
                sweepStartTick = now;
            }
            if (result == 2)
            {
                perfectFlashUntilTick = now + PERFECT_FLASH_DURATION_MS;
            }
        }
    }

    // 2. Spawn from real VU peaks - a note drops in a lane exactly when
    // that lane's own frequency band spikes in the actual audio.
    for (uint8_t lane = 0; lane < NUM_BUTTONS; lane++)
    {
        if (Audio_PeakDetected(lane))
        {
            SpawnNote(lane, now);
        }
    }

    // 3. Scroll
    if ((now - lastStepTick) >= STEP_MS)
    {
        lastStepTick += STEP_MS;
        AdvanceNotes();
    }

    // 4. Draw
    Render(hspi);
}

uint32_t Game_GetScore(void)  { return score;  }
uint32_t Game_GetStreak(void) { return streak; }
uint32_t Game_GetHits(void)   { return hits;   }
uint32_t Game_GetMisses(void) { return misses; }

uint8_t Game_IsPerfectFlashActive(void)
{
    return (HAL_GetTick() < perfectFlashUntilTick) ? 1 : 0;
}
