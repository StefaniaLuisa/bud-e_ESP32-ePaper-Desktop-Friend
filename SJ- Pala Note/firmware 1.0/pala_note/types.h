#pragma once
#include <vector>

typedef enum {
  STATE_IDLE,
  STATE_RECORDING,
  STATE_SAVED,
  STATE_TAG_SELECT,
  STATE_MENU,
  STATE_TAG_BROWSER,
  STATE_NOTE_LIST,
  STATE_NOTE_DETAIL,
  STATE_DELETE_CONFIRM,
  STATE_SETTINGS,
  STATE_DEVICE_INFO,
  STATE_TRANSFER,
  STATE_TIMER_SET,    // picking a duration for the simple timer
  STATE_TIMER_RUN,    // simple countdown timer running
  STATE_POMO_RUN,     // pomodoro running (focus/break phases)
  STATE_NOTE_READY,   // "hold REC to record a note" (notes are now menu-launched)
  STATE_ERROR
} AppState;

enum ButtonEvent { EV_NONE, EV_SINGLE, EV_LONG, EV_DOUBLE };

struct NoteEntry { int num; char tag[32]; bool hasText; };

// Content array sizes — used across notes, ui, and main loop.
#define DEFAULT_TAG_COUNT 5
#define MENU_COUNT        7
#define SETTINGS_COUNT    4   // Sounds · Desk Mode · Transfer · Device

extern const char* DEFAULT_TAGS[];
extern const char* MENU_ITEMS[];
extern const char* SETTINGS_ITEMS[];
