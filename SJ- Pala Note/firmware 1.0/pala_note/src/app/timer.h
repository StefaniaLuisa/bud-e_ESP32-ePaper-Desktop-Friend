#pragma once
#include <stdint.h>

// ─── Bud-E timers ───────────────────────────────────────────────────────────
// A simple countdown timer and a Pomodoro cycle. While a timer runs the device
// stays awake (the main loop suppresses ultra-sleep) so it can show a live
// countdown and chime + show a face when a phase finishes.
//
// Time is tracked with millis() — fine here because the device is kept awake
// for the whole run, so the millis clock never sleeps underneath us.

enum TimerMode { TM_NONE, TM_SIMPLE, TM_POMODORO };
enum PomoPhase { PP_FOCUS, PP_BREAK };

// What just happened when a pomodoro phase reached zero (see pomoAdvance()).
enum PomoAdvance { PA_NOW_BREAK, PA_NOW_FOCUS, PA_ALL_DONE };

// Simple-timer presets (minutes), shown in the picker.
extern const int TIMER_PRESETS[];
extern const int TIMER_PRESET_COUNT;

#define POMO_FOCUS_MIN       25
#define POMO_BREAK_MIN        5
#define POMO_LONG_BREAK_MIN  15   // the break after the final focus block
#define POMO_BLOCKS           4   // focus blocks in one pomodoro session

// ─── Lifecycle ──────────────────────────────────────────────────────────────
void timerStartSimple(int minutes);
void timerStartPomodoro();
void timerCancel();

// ─── Pause / resume ─────────────────────────────────────────────────────────
void timerPauseToggle();
bool timerIsPaused();

// ─── Queries ────────────────────────────────────────────────────────────────
int       timerRemainingSec();   // whole seconds left in the current phase (>= 0)
int       timerTotalSec();       // length of the current phase, in seconds
bool      timerExpired();        // true once the current phase reaches zero (and not paused)

TimerMode timerMode();
PomoPhase pomoPhase();
int       pomoBlock();           // 1-based focus block number (1..POMO_BLOCKS)

// Advance a pomodoro to its next phase. Call when timerExpired() is true in
// pomodoro mode. Returns what just happened so the UI can react.
PomoAdvance pomoAdvance();
