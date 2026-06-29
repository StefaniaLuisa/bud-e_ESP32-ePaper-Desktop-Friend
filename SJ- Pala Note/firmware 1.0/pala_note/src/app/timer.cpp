#include "Arduino.h"
#include "timer.h"

const int TIMER_PRESETS[]   = { 1, 5, 10, 15, 25, 45, 60 };   // TEMP: 1 = quick test preset (remove later)
const int TIMER_PRESET_COUNT = sizeof(TIMER_PRESETS) / sizeof(TIMER_PRESETS[0]);

static TimerMode mode  = TM_NONE;
static PomoPhase phase = PP_FOCUS;
static int       block = 1;

static uint32_t durationMs  = 0;
static uint32_t startMs     = 0;
static bool     paused      = false;
static uint32_t frozenRemMs = 0;   // remaining at the moment we paused

static void beginPhase(uint32_t ms) {
  durationMs = ms;
  startMs    = millis();
  paused     = false;
}

void timerStartSimple(int minutes) {
  mode = TM_SIMPLE;
  beginPhase((uint32_t)minutes * 60000UL);
}

void timerStartPomodoro() {
  mode  = TM_POMODORO;
  phase = PP_FOCUS;
  block = 1;
  beginPhase((uint32_t)POMO_FOCUS_MIN * 60000UL);
}

void timerCancel() {
  mode   = TM_NONE;
  paused = false;
}

void timerPauseToggle() {
  if (mode == TM_NONE) return;
  if (paused) {
    // Resume: shift the start so the remaining time is preserved.
    startMs = millis() - (durationMs - frozenRemMs);
    paused  = false;
  } else {
    uint32_t elapsed = millis() - startMs;
    frozenRemMs = (elapsed >= durationMs) ? 0 : (durationMs - elapsed);
    paused = true;
  }
}

bool timerIsPaused() { return paused; }

int timerRemainingSec() {
  if (mode == TM_NONE) return 0;
  uint32_t remMs;
  if (paused) {
    remMs = frozenRemMs;
  } else {
    uint32_t elapsed = millis() - startMs;
    remMs = (elapsed >= durationMs) ? 0 : (durationMs - elapsed);
  }
  return (int)((remMs + 999) / 1000);   // round up so it reads 0 only at the end
}

int timerTotalSec() { return (int)(durationMs / 1000); }

bool timerExpired() {
  if (mode == TM_NONE || paused) return false;
  return (millis() - startMs) >= durationMs;
}

TimerMode timerMode() { return mode; }
PomoPhase pomoPhase() { return phase; }
int       pomoBlock() { return block; }

PomoAdvance pomoAdvance() {
  if (phase == PP_FOCUS) {
    phase = PP_BREAK;
    // The break after the final focus block is a longer one (classic pomodoro).
    uint32_t mins = (block >= POMO_BLOCKS) ? POMO_LONG_BREAK_MIN : POMO_BREAK_MIN;
    beginPhase(mins * 60000UL);
    return PA_NOW_BREAK;
  }
  // A break just finished.
  if (block >= POMO_BLOCKS) {
    mode = TM_NONE;
    return PA_ALL_DONE;
  }
  block++;
  phase = PP_FOCUS;
  beginPhase((uint32_t)POMO_FOCUS_MIN * 60000UL);
  return PA_NOW_FOCUS;
}
