#pragma once
#include <Arduino.h>

// Icons
void iconMicWhite(int cx, int cy);
void iconRecordBig(int cx, int cy);
void iconCheck(int cx, int cy, bool filled);
void iconError(int cx, int cy);
void iconThinking(int cx, int cy);
void iconTag(int cx, int cy);
void iconSync(int cx, int cy);
void iconWifi(int cx, int cy);
void iconNoteLines(int cx, int cy);

// Layout helpers
void drawHeader(const char* title, const char* rightInfo = nullptr);
void drawHints(const char* recLabel, const char* pwrLabel);
void drawBadge(int cx, int cy, const char* text, bool filled);
void drawPageDots(int cur, int total);
void drawChevronRight(int x, int cy, uint8_t c);
void drawTinyHint(const char* left, const char* right);
void drawKicker(const char* txt, int y);
void drawSoftFrame();
void drawProductWordmark(int cx, int y, uint8_t color);
void drawModernPill(int x, int y, int w, int h, const char* label, bool active);
void drawDotSelector(int cur, int total, int y);
void drawCheckSmall(int cx, int cy, uint8_t color);
void drawMinimalDocIcon(int cx, int cy, uint8_t color);
void drawMinimalTagIcon(int cx, int cy, uint8_t color);
void drawMinimalCloudIcon(int cx, int cy, uint8_t color);
void drawMenuTile(int x, int y, int w, int h, const char* label, int icon, bool active);
void drawNoteCard(int y, int idx, bool active);
void drawListMenuCard(int y, const char* title, const char* meta, bool active);

// Screens
void showIdle();
void showIdleBlink();   // home screen with eyes closed (one frame of a blink)
void showBatteryLow(int pct);
void showRecording();
void showSaved(int num);
void showTagSelect(int cursor);
void showMenu(int cursor);
void showTagBrowser(int cursor);
void showNoteList(int cursor);
void showNoteDetail(int cursor);
void showDeleteConfirm(int noteNum);
void showTranscribing(int done, int total);
void showWifiConnecting(int attempt, int maxA);
void showDone();
void showError(const char* msg);
void showUltraSleepScreen();
void showPlaybackOverlay();
void showTransferConnecting();
void showTransferMode(const char* ip);
void showSettings(int cursor);
void showDeviceInfo();
void showDeskMode(bool on);          // desk-mode toggle confirmation
void showNudge(const char* text);    // proactive nudge banner + text

// Timer / Pomodoro screens
void showTimerSet(int presetIdx);                 // duration picker
void showTimerRun(int remSec, int totalSec, bool paused);
void showPomoRun(int remSec, int totalSec, bool isBreak, int block, bool paused);
void showTimerDone(const char* big, const char* sub, bool love);

// Bud-E ask (push-to-talk) screens
void showAskThinking();               // Bud-E is working on it
void showAskSpeaking(const char* reply);   // shows what Bud-E said while it speaks
void showNoteReady();                 // "hold rec to record a note" (menu-launched)
