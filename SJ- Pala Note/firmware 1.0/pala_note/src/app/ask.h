#pragma once
#include <Arduino.h>

// Bud-E "ask" mode (Phase 2): upload a recorded WAV to the Mac brain over plain
// HTTP and save the spoken reply WAV. Requires WiFi to already be connected.
//
//   uploadPath : the recorded question (e.g. ASK_WAV_PATH)
//   replyPath  : where to save Bud-E's spoken answer (e.g. REPLY_WAV_PATH)
// Returns true if a reply WAV was received and written.
bool askBude(const char* uploadPath, const char* replyPath);

// Populated by askBude() from the server's X-Bude-* response headers.
extern String askReplyText;       // what Bud-E said (for on-screen display)
extern String askTranscriptText;  // what the server heard

// Desk mode: poll the Mac for a pending proactive nudge. Requires WiFi up.
//   replyPath : where to save the nudge's spoken audio (e.g. NUDGE_WAV_PATH)
// Returns true if a nudge is waiting (audio saved; nudgeText/nudgeMood set);
// false if there's nothing pending (HTTP 204) or on any error.
bool pollNudge(const char* replyPath);

extern String nudgeText;   // the nudge's text (X-Bude-Reply), for on-screen display
extern String nudgeMood;   // suggested face mood (X-Bude-Mood)
