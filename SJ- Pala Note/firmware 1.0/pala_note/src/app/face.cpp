#include "Arduino.h"
#include "draw.h"
#include "face.h"

// ─── Eye geometry (tuned for the 200x200 screen) ────────────────────────────
// All values are easy to tweak — change these to make the eyes bigger,
// rounder, or closer together.
static const int EYE_W   = 52;   // eye width
static const int EYE_H   = 64;   // eye height (AWAKE)
static const int EYE_R   = 18;   // corner radius (roundness)
static const int EYE_GAP = 28;   // space between the two eyes
static const int EYE_TOP = 60;   // y of the eye top (AWAKE)

static const int FACE_W  = EYE_W * 2 + EYE_GAP;        // 132
static const int LEFT_X  = (W - FACE_W) / 2;           // 34
static const int RIGHT_X = LEFT_X + EYE_W + EYE_GAP;   // 114

// Colors: black eyes on a white background (best for e-paper).
// Swap these two values for the inverted "glowing eyes in the dark" look.
static const uint8_t BG = WHITE;
static const uint8_t FG = BLACK;

// One open rounded-square eye.
static void eye(int x, int y, int w, int h, int r) {
  fillRoundRect(x, y, w, h, r, FG);
}

// A "happy" eye: draw a full eye, then carve the top with the background
// color, leaving a smiling upward crescent.
static void happyEye(int x) {
  fillRoundRect(x, EYE_TOP, EYE_W, EYE_H, EYE_R, FG);
  fillCircle(x + EYE_W / 2, EYE_TOP - 6, EYE_W, BG);
}

// A heart centered at (cx, cy); `s` controls its size.
static void heart(int cx, int cy, int s) {
  fillCircle(cx - s / 2, cy - s / 2, s / 2 + 1, FG);
  fillCircle(cx + s / 2, cy - s / 2, s / 2 + 1, FG);
  fillTriangle(cx - s, cy - s / 2, cx + s, cy - s / 2, cx, cy + s, FG);
}

void drawBudeEyes(BudeMood mood) {
  switch (mood) {
    case MOOD_AWAKE:
      eye(LEFT_X,  EYE_TOP, EYE_W, EYE_H, EYE_R);
      eye(RIGHT_X, EYE_TOP, EYE_W, EYE_H, EYE_R);
      break;

    case MOOD_BLINK: {
      int by = EYE_TOP + EYE_H / 2 - 5;
      eye(LEFT_X,  by, EYE_W, 10, 5);
      eye(RIGHT_X, by, EYE_W, 10, 5);
      break;
    }

    case MOOD_HAPPY:
      happyEye(LEFT_X);
      happyEye(RIGHT_X);
      break;

    case MOOD_SLEEPY: {
      int sy = EYE_TOP + EYE_H - 16;
      eye(LEFT_X,  sy, EYE_W, 12, 6);
      eye(RIGHT_X, sy, EYE_W, 12, 6);
      break;
    }

    case MOOD_LISTENING: {
      int w = EYE_W + 6, h = EYE_H + 10, top = EYE_TOP - 6;
      int lx = (W - (w * 2 + EYE_GAP)) / 2;
      int rx = lx + w + EYE_GAP;
      eye(lx, top, w, h, EYE_R + 2);
      eye(rx, top, w, h, EYE_R + 2);
      break;
    }

    case MOOD_THINKING: {
      int top = EYE_TOP - 12, h = EYE_H - 14;   // glance up + slightly shorter
      eye(LEFT_X,  top, EYE_W, h, EYE_R);
      eye(RIGHT_X, top, EYE_W, h, EYE_R);
      break;
    }

    case MOOD_LOVE: {
      int cy = EYE_TOP + EYE_H / 2;
      heart(LEFT_X  + EYE_W / 2, cy, 26);
      heart(RIGHT_X + EYE_W / 2, cy, 26);
      break;
    }

    case MOOD_TIRED: {
      // RoboEyes "tired": open eyes, then droop the OUTER top corners with
      // background-colored triangles.
      eye(LEFT_X,  EYE_TOP, EYE_W, EYE_H, EYE_R);
      eye(RIGHT_X, EYE_TOP, EYE_W, EYE_H, EYE_R);
      int lid = EYE_H / 2;
      fillTriangle(LEFT_X,  EYE_TOP - 1, LEFT_X  + EYE_W, EYE_TOP - 1, LEFT_X,          EYE_TOP + lid - 1, BG);
      fillTriangle(RIGHT_X, EYE_TOP - 1, RIGHT_X + EYE_W, EYE_TOP - 1, RIGHT_X + EYE_W, EYE_TOP + lid - 1, BG);
      break;
    }

    case MOOD_ANGRY: {
      // RoboEyes "angry": open eyes, then droop the INNER top corners.
      eye(LEFT_X,  EYE_TOP, EYE_W, EYE_H, EYE_R);
      eye(RIGHT_X, EYE_TOP, EYE_W, EYE_H, EYE_R);
      int lid = EYE_H / 2;
      fillTriangle(LEFT_X,  EYE_TOP - 1, LEFT_X  + EYE_W, EYE_TOP - 1, LEFT_X  + EYE_W, EYE_TOP + lid - 1, BG);
      fillTriangle(RIGHT_X, EYE_TOP - 1, RIGHT_X + EYE_W, EYE_TOP - 1, RIGHT_X,         EYE_TOP + lid - 1, BG);
      break;
    }
  }
}

void drawBudeFace(BudeMood mood) {
  fillRect(0, 0, W, H, BG);   // clean white background (e-paper friendly)
  drawBudeEyes(mood);
  refresh();
}
