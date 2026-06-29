#include "Arduino.h"
#include "../../config.h"
#include "ask.h"
#include "SD_MMC.h"
#include "WiFi.h"

String askReplyText = "";
String askTranscriptText = "";
String nudgeText = "";
String nudgeMood = "";

// Read one CRLF-terminated line from the client (without the CRLF).
static String readHttpLine(WiFiClient& c, uint32_t deadline) {
  String s = "";
  while (millis() < deadline) {
    if (c.available()) {
      char ch = (char)c.read();
      if (ch == '\n') break;
      if (ch != '\r') s += ch;
    } else if (!c.connected()) {
      break;
    } else {
      delay(1);
    }
  }
  return s;
}

bool askBude(const char* uploadPath, const char* replyPath) {
  askReplyText = "";
  askTranscriptText = "";

  File up = SD_MMC.open(uploadPath, FILE_READ);
  if (!up) { Serial.println("[Ask] no upload file"); return false; }
  size_t len = up.size();
  if (len <= 44) { up.close(); Serial.println("[Ask] upload too small"); return false; }

  WiFiClient client;
  if (!client.connect(BUDE_HOST, BUDE_PORT)) {
    Serial.println("[Ask] connect to Mac failed");
    up.close();
    return false;
  }

  // ── Request: POST the raw WAV as the body ──
  client.printf("POST /ask HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Content-Type: audio/wav\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                BUDE_HOST, (int)BUDE_PORT, (unsigned)len);

  uint8_t buf[1024];
  while (up.available()) {
    int n = up.read(buf, sizeof(buf));
    if (n <= 0) break;
    client.write(buf, n);
  }
  up.close();

  // ── Response ──
  uint32_t deadline = millis() + 30000;

  String status = readHttpLine(client, deadline);
  if (status.indexOf("200") < 0) {
    Serial.printf("[Ask] bad status: %s\n", status.c_str());
    client.stop();
    return false;
  }

  long contentLen = -1;
  while (true) {
    String h = readHttpLine(client, deadline);
    if (h.length() == 0) break;            // blank line = end of headers
    int colon = h.indexOf(':');
    if (colon <= 0) continue;
    String key = h.substring(0, colon); key.toLowerCase(); key.trim();
    String val = h.substring(colon + 1);  val.trim();
    if (key == "content-length")        contentLen = val.toInt();
    else if (key == "x-bude-reply")      askReplyText = val;
    else if (key == "x-bude-transcript") askTranscriptText = val;
  }
  if (contentLen <= 0) { Serial.println("[Ask] no reply audio"); client.stop(); return false; }

  // ── Body → reply WAV on SD ──
  File rf = SD_MMC.open(replyPath, FILE_WRITE);
  if (!rf) { client.stop(); return false; }

  long remaining = contentLen;
  while (remaining > 0 && (client.connected() || client.available()) && millis() < deadline) {
    if (client.available()) {
      int want = (int)((long)sizeof(buf) < remaining ? sizeof(buf) : remaining);
      int n = client.read(buf, want);
      if (n > 0) { rf.write(buf, n); remaining -= n; }
    } else {
      delay(2);
    }
  }
  rf.close();
  client.stop();

  Serial.printf("[Ask] heard:\"%s\" reply:\"%s\"\n",
                askTranscriptText.c_str(), askReplyText.c_str());
  return remaining <= 0;
}

// ─── Desk-mode nudge poll ────────────────────────────────────────────────────
// GET /api/device/nudge. 204 → nothing pending. 200 → the nudge's spoken WAV in
// the body, with X-Bude-Reply (text) + X-Bude-Mood headers. Mirrors askBude()'s
// response handling but as a quick GET (empty polls return almost instantly).
bool pollNudge(const char* replyPath) {
  nudgeText = "";
  nudgeMood = "";

  WiFiClient client;
  if (!client.connect(BUDE_HOST, BUDE_PORT)) return false;   // Mac unreachable

  client.printf("GET /api/device/nudge HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Connection: close\r\n\r\n",
                BUDE_HOST, (int)BUDE_PORT);

  uint32_t deadline = millis() + 10000;

  String status = readHttpLine(client, deadline);
  if (status.indexOf("200") < 0) {        // 204 (nothing) or an error → no nudge
    client.stop();
    return false;
  }

  long contentLen = -1;
  while (true) {
    String h = readHttpLine(client, deadline);
    if (h.length() == 0) break;
    int colon = h.indexOf(':');
    if (colon <= 0) continue;
    String key = h.substring(0, colon); key.toLowerCase(); key.trim();
    String val = h.substring(colon + 1);  val.trim();
    if (key == "content-length")   contentLen = val.toInt();
    else if (key == "x-bude-reply") nudgeText = val;
    else if (key == "x-bude-mood")  nudgeMood = val;
  }
  if (contentLen <= 0) { client.stop(); return false; }

  File rf = SD_MMC.open(replyPath, FILE_WRITE);
  if (!rf) { client.stop(); return false; }

  uint8_t buf[1024];
  long remaining = contentLen;
  while (remaining > 0 && (client.connected() || client.available()) && millis() < deadline) {
    if (client.available()) {
      int want = (int)((long)sizeof(buf) < remaining ? sizeof(buf) : remaining);
      int n = client.read(buf, want);
      if (n > 0) { rf.write(buf, n); remaining -= n; }
    } else {
      delay(2);
    }
  }
  rf.close();
  client.stop();

  Serial.printf("[Nudge] \"%s\" (mood %s)\n", nudgeText.c_str(), nudgeMood.c_str());
  return remaining <= 0;
}
