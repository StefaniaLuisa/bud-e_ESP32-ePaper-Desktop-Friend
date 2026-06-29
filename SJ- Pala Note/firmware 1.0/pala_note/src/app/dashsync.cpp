#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "dashsync.h"
#include "notes.h"
#include "SD_MMC.h"
#include "WiFi.h"

static String pathFor(int num, const char* ext) {
  char p[72];
  snprintf(p, sizeof(p), "%s/note_%03d.%s", NOTES_DIR, num, ext);
  return String(p);
}

// Minimal JSON string escaping (quotes, backslash, control chars).
static String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\t': o += "\\t";  break;
      case '\r': break;
      default:
        if ((uint8_t)c >= 0x20) o += c;   // drop other control chars
    }
  }
  return o;
}

static String readFileText(const String& path, size_t maxLen = 6000) {
  File f = SD_MMC.open(path.c_str(), FILE_READ);
  if (!f) return "";
  String s;
  size_t n = 0;
  while (f.available() && n < maxLen) { s += (char)f.read(); n++; }
  f.close();
  return s;
}

// Read one HTTP status line; true if it reports 200.
static bool statusOk(WiFiClient& c, uint32_t deadline) {
  String s;
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
  return s.indexOf("200") >= 0;
}

static bool postJson(const char* path, const String& body) {
  WiFiClient client;
  if (!client.connect(BUDE_HOST, BUDE_PORT)) return false;
  client.printf("POST %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                path, BUDE_HOST, (int)BUDE_PORT, (unsigned)body.length());
  client.print(body);
  bool ok = statusOk(client, millis() + 10000);
  client.stop();
  return ok;
}

static bool postAudio(int num) {
  File f = SD_MMC.open(pathFor(num, "wav").c_str(), FILE_READ);
  if (!f) return false;
  size_t len = f.size();
  WiFiClient client;
  if (!client.connect(BUDE_HOST, BUDE_PORT)) { f.close(); return false; }
  char path[48];
  snprintf(path, sizeof(path), "/note/audio?id=%d", num);
  client.printf("POST %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Content-Type: audio/wav\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                path, BUDE_HOST, (int)BUDE_PORT, (unsigned)len);
  uint8_t buf[1024];
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    client.write(buf, n);
  }
  f.close();
  bool ok = statusOk(client, millis() + 15000);
  client.stop();
  return ok;
}

// GET a small response body from the Mac (skips headers). For the plan JSON.
static String httpGet(const char* path) {
  WiFiClient client;
  if (!client.connect(BUDE_HOST, BUDE_PORT)) return "";
  client.printf("GET %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Connection: close\r\n\r\n",
                path, BUDE_HOST, (int)BUDE_PORT);
  uint32_t deadline = millis() + 12000;
  bool headersDone = false;
  String head, body;
  while (millis() < deadline && (client.connected() || client.available())) {
    if (client.available()) {
      char ch = (char)client.read();
      if (!headersDone) {
        head += ch;
        if (head.endsWith("\r\n\r\n")) headersDone = true;
      } else {
        body += ch;
      }
    } else {
      delay(2);
    }
  }
  client.stop();
  return body;
}

// Parse the integer array for "key" out of a small JSON object, e.g. "delete":[1,2,3].
static int parseIdArray(const String& json, const char* key, int* out, int maxOut) {
  String k = String("\"") + key + "\"";
  int ki = json.indexOf(k);
  if (ki < 0) return 0;
  int lb = json.indexOf('[', ki);
  int rb = json.indexOf(']', lb);
  if (lb < 0 || rb < 0) return 0;
  String inner = json.substring(lb + 1, rb);
  int count = 0, start = 0;
  while (start <= (int)inner.length() && count < maxOut) {
    int comma = inner.indexOf(',', start);
    String tok = (comma < 0) ? inner.substring(start) : inner.substring(start, comma);
    tok.trim();
    if (tok.length()) out[count++] = tok.toInt();
    if (comma < 0) break;
    start = comma + 1;
  }
  return count;
}

static bool uploadNote(int num, const char* tag) {
  String title   = readNoteAiValue(num, "title");
  String summary = readNoteAiValue(num, "summary");
  String todos   = readNoteAiValue(num, "todos");   // pipe-delimited
  String clean   = readNoteAiClean(num);
  String raw     = readFileText(pathFor(num, "txt"));
  String created = noteCreatedUtc(num);

  String json = "{";
  json += "\"id\":" + String(num);
  json += ",\"created\":\""    + jsonEscape(created)             + "\"";
  json += ",\"tag\":\""        + jsonEscape(tag ? String(tag) : "") + "\"";
  json += ",\"title\":\""      + jsonEscape(title)               + "\"";
  json += ",\"summary\":\""    + jsonEscape(summary)             + "\"";
  json += ",\"transcript\":\"" + jsonEscape(raw)                 + "\"";
  json += ",\"clean\":\""      + jsonEscape(clean)               + "\"";
  json += ",\"todos\":\""      + jsonEscape(todos)               + "\"";
  json += "}";

  if (!postJson("/note", json)) return false;
  return postAudio(num);
}

int syncNotesToDashboard() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int uploaded = 0;
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (!noteIndex[i].hasText) continue;                 // only enriched notes
    int num = noteIndex[i].num;
    String up = pathFor(num, "up");
    if (SD_MMC.exists(up.c_str())) continue;             // already uploaded

    if (uploadNote(num, noteIndex[i].tag)) {
      File m = SD_MMC.open(up.c_str(), FILE_WRITE);
      if (m) { m.print("1"); m.close(); }
      uploaded++;
      Serial.printf("[dash] uploaded note %d\n", num);
    } else {
      Serial.printf("[dash] upload failed for note %d\n", num);
    }
  }
  return uploaded;
}

// Ask the Mac what to do with local notes (retention): delete the ones it has
// archived as dashboard-only or auto-migrated, then confirm. Returns the count
// removed locally. (Restore-to-device is a planned follow-up.)
int reconcileWithDashboard() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  String plan = httpGet("/api/device/plan");
  if (plan.length() == 0) return 0;

  int del[40];
  int n = parseIdArray(plan, "delete", del, 40);
  if (n <= 0) return 0;

  String deleted;
  for (int i = 0; i < n; i++) {
    deleteNote(del[i]);   // safe even if the note is already gone locally
    if (deleted.length()) deleted += ",";
    deleted += String(del[i]);
    Serial.printf("[dash] note %d moved to dashboard-only (removed locally)\n", del[i]);
  }

  String body = String("{\"deleted\":[") + deleted + "],\"restored\":[]}";
  postJson("/api/device/applied", body);
  return n;
}
