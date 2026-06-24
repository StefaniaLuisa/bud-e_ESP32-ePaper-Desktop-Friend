#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "network.h"
#include "notes.h"
#include "rtc.h"
#include "ui.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include "../../secrets.h"

static String parseWhisperText(const String& resp) {
  int s = resp.indexOf("\"text\":\"");
  if (s < 0) return "";
  s += 8;
  int e = s;
  while (e < (int)resp.length()) {
    if (resp[e] == '\\' && e + 1 < (int)resp.length()) { e += 2; continue; }
    if (resp[e] == '"') break;
    e++;
  }
  if (e >= (int)resp.length()) return "";
  String text = "";
  for (int i = s; i < e; i++) {
    if (resp[i] == '\\' && i + 1 < e) {
      char nx = resp[++i];
      if      (nx == '"')  text += '"';
      else if (nx == '\\') text += '\\';
      else if (nx == 'n')  text += ' ';
      else                 text += nx;
    } else {
      text += resp[i];
    }
  }
  return text;
}

// ─── Claude enrichment ──────────────────────────────────────────────────────

// JSON-escape a string so it can be embedded in a request body.
static String jsonEscape(const String& s) {
  String out; out.reserve(s.length() + 16);
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += c;
    }
  }
  return out;
}

// Pull the first content text block out of an Anthropic /v1/messages response,
// un-escaping JSON string escapes (newlines preserved so we can parse lines).
static String claudeExtractText(const String& resp) {
  int s = resp.indexOf("\"text\":\"");
  if (s < 0) return "";
  s += 8;
  String out;
  for (int i = s; i < (int)resp.length(); i++) {
    char c = resp[i];
    if (c == '\\' && i + 1 < (int)resp.length()) {
      char nx = resp[++i];
      switch (nx) {
        case 'n': out += '\n'; break;
        case 'r':              break;
        case 't': out += '\t'; break;
        case '"': out += '"';  break;
        case '\\': out += '\\'; break;
        case '/': out += '/';  break;
        case 'u':
          if (i + 4 < (int)resp.length()) {
            long code = strtol(resp.substring(i + 1, i + 5).c_str(), nullptr, 16);
            out += (code > 0 && code < 128) ? (char)code : '?';
            i += 4;
          }
          break;
        default: out += nx;
      }
    } else if (c == '"') {
      break;  // closing quote of the JSON string
    } else {
      out += c;
    }
  }
  return out;
}

// Return the trimmed remainder of the line that begins with `label`.
static String grabField(const String& body, const char* label) {
  int idx = body.indexOf(label);
  if (idx < 0) return "";
  int start = idx + strlen(label);
  int end = body.indexOf('\n', start);
  String v = (end < 0) ? body.substring(start) : body.substring(start, end);
  v.trim();
  return v;
}

// Send a transcript to Claude and store the title/summary/tag/to-dos/cleaned
// text in the note's .ai file. Best-effort: returns false on any failure.
static bool enrichNoteWithClaude(int noteNum, const String& transcript) {
  if (transcript.length() == 0) return false;

  // Allowed-tag list from the device's current tags.
  String tagList;
  for (int i = 0; i < tagCount; i++) { if (i) tagList += ", "; tagList += tags[i]; }
  if (tagList.indexOf("Untagged") < 0) tagList += ", Untagged";

  String sys =
    "You clean up and organise short voice notes. "
    "Reply in EXACTLY this format, nothing before or after, no markdown:\n"
    "TITLE: <a short specific title, max 6 words>\n"
    "SUMMARY: <one sentence summarising the note>\n"
    "TAG: <choose the single best fit from the allowed tags>\n"
    "TODOS: <action items separated by | , or the word none>\n"
    "===CLEAN===\n"
    "<the note rewritten with correct spelling and punctuation, keeping the meaning and wording>";

  String user = "Allowed tags: " + tagList + "\n\nVoice note transcript:\n" + transcript;

  String body =
    String("{\"model\":\"") + CLAUDE_MODEL + "\","
    "\"max_tokens\":" + String(CLAUDE_MAXTOK) + ","
    "\"system\":\"" + jsonEscape(sys) + "\","
    "\"messages\":[{\"role\":\"user\",\"content\":\"" + jsonEscape(user) + "\"}]}";

  WiFiClientSecure client;
  client.setInsecure();  // TODO: pin api.anthropic.com cert for production use
  client.setTimeout(90);
  if (!client.connect(CLAUDE_HOST, 443)) { Serial.println("[Claude] connect failed"); return false; }

  client.printf("POST " CLAUDE_PATH " HTTP/1.1\r\n"
                "Host: " CLAUDE_HOST "\r\n"
                "x-api-key: %s\r\n"
                "anthropic-version: " CLAUDE_VERSION "\r\n"
                "content-type: application/json\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                ANTHROPIC_KEY, (unsigned)body.length());
  client.print(body);

  uint32_t deadline = millis() + 90000;
  while (!client.available() && millis() < deadline) delay(20);

  String resp = "";
  bool inBody = false;
  bool httpOk = true;
  while (client.available() || (client.connected() && millis() < deadline)) {
    if (!client.available()) { delay(10); continue; }
    String line = client.readStringUntil('\n');
    if (!inBody) {
      if (line.startsWith("HTTP/") && line.indexOf(" 200 ") < 0) {
        Serial.printf("[Claude] %s\n", line.c_str());
        httpOk = false;
      }
      if (line == "\r" || line == "") inBody = true;
    } else {
      resp += line; resp += "\n";
      if (resp.length() > 16384) break;
    }
  }
  client.stop();
  if (!httpOk) return false;

  String out = claudeExtractText(resp);
  if (out.length() == 0) { Serial.println("[Claude] empty response"); return false; }

  String title   = grabField(out, "TITLE:");
  String summary = grabField(out, "SUMMARY:");
  String tagSel  = grabField(out, "TAG:");
  String todos   = grabField(out, "TODOS:");
  if (todos.equalsIgnoreCase("none")) todos = "";

  String clean;
  int cidx = out.indexOf("===CLEAN===");
  if (cidx >= 0) { clean = out.substring(cidx + 11); clean.trim(); }
  if (clean.length() == 0) clean = transcript;  // fallback to raw

  // Validate the chosen tag against the real tag list (case-insensitive).
  String validTag = "";
  for (int i = 0; i < tagCount; i++)
    if (strcasecmp(tags[i], tagSel.c_str()) == 0) { validTag = tags[i]; break; }

  writeNoteAi(noteNum, title, summary, validTag, todos, clean);

  // Auto-apply the tag only if the note still has a generic/default tag, so we
  // never override a tag the user deliberately chose after recording.
  if (validTag.length() > 0) {
    for (int i = 0; i < (int)noteIndex.size(); i++) {
      if (noteIndex[i].num == noteNum) {
        const char* cur = noteIndex[i].tag;
        bool generic = (strlen(cur) == 0) ||
                       strcasecmp(cur, "Untagged") == 0 ||
                       strcasecmp(cur, "Note") == 0;
        if (generic) {
          strncpy(noteIndex[i].tag, validTag.c_str(), 31);
          noteIndex[i].tag[31] = '\0';
          saveIndex();
          writeNoteMeta(noteNum, validTag.c_str());
        }
        break;
      }
    }
  }
  Serial.printf("[Claude] note %d enriched: %s\n", noteNum, title.c_str());
  return true;
}

// ─── Transcription ──────────────────────────────────────────────────────────

static bool transcribeOnce(const String& wavPath, int noteNum) {
  File f = SD_MMC.open(wavPath.c_str());
  if (!f) return false;
  size_t fileSize = f.size();

  String bnd = "----PalaBoundary";
  String pre = "--" + bnd + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" STT_MODEL "\r\n"
               "--" + bnd + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String post = "\r\n--" + bnd + "--\r\n";
  size_t totalLen = pre.length() + fileSize + post.length();

  WiFiClientSecure client;
  client.setInsecure();  // TODO: pin the STT host cert for production use
  client.setTimeout(90);

  if (!client.connect(STT_HOST, 443)) { f.close(); return false; }

  client.printf("POST " STT_PATH " HTTP/1.1\r\n"
                "Host: " STT_HOST "\r\n"
                "Authorization: Bearer %s\r\n"
                "Content-Type: multipart/form-data; boundary=%s\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                GROQ_KEY, bnd.c_str(), (unsigned)totalLen);
  client.print(pre);

  uint8_t* chunk = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_8BIT);
  if (!chunk) { f.close(); client.stop(); return false; }
  while (f.available()) {
    int n = f.read(chunk, 4096);
    if (n <= 0) break;
    client.write(chunk, n);
  }
  heap_caps_free(chunk);
  f.close();
  client.print(post);

  uint32_t deadline = millis() + 90000;
  while (!client.available() && millis() < deadline) delay(20);

  String resp = "";
  bool inBody = false;
  while (client.available() || (client.connected() && millis() < deadline)) {
    if (!client.available()) { delay(10); continue; }
    String line = client.readStringUntil('\n');
    if (!inBody) {
      if (line == "\r" || line == "") inBody = true;
      if (line.startsWith("HTTP/") && line.indexOf(" 200 ") < 0) {
        Serial.printf("[Whisper] %s\n", line.c_str());
        client.stop(); return false;
      }
    } else {
      resp += line;
      if (resp.length() > 8192) break;
    }
  }
  client.stop();

  String text = parseWhisperText(resp);
  if (text.length() == 0) { Serial.println("[Whisper] empty response"); return false; }

  String tp = wavPath; tp.replace(".wav", ".txt");
  File tf = SD_MMC.open(tp.c_str(), FILE_WRITE);
  if (tf) { tf.print(text); tf.close(); }

  updateIndexHasText(noteNum);

  // Best-effort enrichment. If Claude fails, the raw transcript is already
  // saved above, so the note still works — we just skip the smart fields.
  enrichNoteWithClaude(noteNum, text);

  return true;
}

bool transcribe(const String& wavPath, int noteNum) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (transcribeOnce(wavPath, noteNum)) return true;
    if (attempt < 2) { Serial.printf("[Whisper] retry %d/2\n", attempt + 1); delay(3000); }
  }
  return false;
}

void transcribeAll() {
  int pending = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) if(!noteIndex[i].hasText) pending++;
  int done = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) {
    if (noteIndex[i].hasText) continue;
    showTranscribing(done, pending);
    char wp[64]; snprintf(wp, sizeof(wp), "%s/note_%03d.wav", NOTES_DIR, noteIndex[i].num);
    if (transcribe(String(wp), noteIndex[i].num)) done++;
  }
}

// ─── Portal helpers ────────────────────────────────────────────────────────

String htmlEscape(const String& s) {
  String out = s;
  out.replace("&", "&amp;"); out.replace("<", "&lt;");
  out.replace(">", "&gt;"); out.replace("\"", "&quot;");
  return out;
}

String readSmallFile(const char* path, size_t maxLen) {
  File f = SD_MMC.open(path);
  if (!f) return "";
  String out;
  while (f.available() && out.length() < maxLen) out += (char)f.read();
  f.close();
  return out;
}

String urlDecodeSimple(String s) {
  s.replace("+", " ");
  String out = "";
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '%' && i + 2 < (int)s.length()) {
      String hex = s.substring(i + 1, i + 3);
      out += (char)strtol(hex.c_str(), nullptr, 16);
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

String portalCss() {
  return String(
    "<style>"
    ":root{font-family:-apple-system,BlinkMacSystemFont,'Inter','Segoe UI',sans-serif;color:#111;background:#f3f0e9;}"
    "body{margin:0;padding:24px;background:#f3f0e9;}"
    ".wrap{max-width:780px;margin:0 auto;}"
    ".top{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:24px;}"
    "h1{font-size:44px;letter-spacing:-.06em;line-height:.9;margin:0;font-weight:800;}"
    ".sub{font-size:13px;text-transform:uppercase;letter-spacing:.12em;color:#6a665f;margin-top:10px;}"
    ".pill{display:inline-flex;border:1px solid #111;border-radius:999px;padding:8px 12px;font-size:13px;background:#fffaf1;}"
    ".grid{display:grid;grid-template-columns:1fr;gap:14px;}"
    ".card{background:#fffaf1;border:1.5px solid #111;border-radius:24px;padding:18px;box-shadow:4px 4px 0 #111;}"
    ".row{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;}"
    ".num{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:#6a665f;margin-bottom:8px;}"
    ".date{font-size:13px;color:#6a665f;margin:-4px 0 12px;}"
    ".title{font-size:24px;line-height:1.05;letter-spacing:-.04em;font-weight:750;margin:0 0 12px;}"
    ".tag{border:1px solid #111;border-radius:999px;padding:5px 9px;font-size:12px;white-space:nowrap;background:#111;color:#fff;}"
    ".text{font-size:15px;line-height:1.45;color:#222;margin:0 0 14px;white-space:pre-wrap;}"
    ".summary{font-size:14px;font-style:italic;color:#555;margin:0 0 10px;}"
    ".todos{margin:10px 0 4px;}"
    ".todos-h{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#6a665f;margin-bottom:4px;}"
    ".todos ul{margin:0;padding-left:18px;}"
    ".todos li{font-size:14px;line-height:1.4;margin:2px 0;}"
    ".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px;}"
    "a.btn{color:#111;text-decoration:none;border:1px solid #111;border-radius:999px;padding:8px 12px;background:#f3f0e9;font-size:13px;}"
    "a.btn.primary{background:#111;color:#fff;}"
    ".empty{border:1.5px dashed #111;border-radius:24px;padding:34px;text-align:center;color:#6a665f;}"
    "audio{width:100%;margin-top:8px;}"
    "@media(max-width:520px){body{padding:16px}h1{font-size:36px}.card{border-radius:20px}.title{font-size:21px}}"
    "</style>"
  );
}

// ─── Portal handlers ───────────────────────────────────────────────────────

void handlePortalRoot() {
  loadIndex();

  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Pala Portal</title>" + portalCss() + "</head><body><div class='wrap'>";

  html += "<div class='top'><div><h1>pala<br>portal</h1>"
          "<div class='sub'>local note transfer · <a href=\"/tags\" style=\"color:inherit\">tags</a></div></div>"
          "<div class='pill'>" + String((int)noteIndex.size()) + " notes</div></div>";

  html += "<div class='actions' style='margin-bottom:18px'>";
  html += "<a class='btn " + String(filter == "All" ? "primary" : "") + "' href='/'>All</a>";
  for (int t = 0; t < tagCount; t++) {
    String tag = String(tags[t]);
    html += "<a class='btn " + String(filter == tag ? "primary" : "") + "' href='/?tag=" + tag + "'>" + htmlEscape(tag) + "</a>";
  }
  html += "</div>";

  html += "<div class='actions' style='margin-bottom:24px'>";
  html += "<a class='btn primary' href='/export.txt'>Download all TXT</a>";
  if (filter != "All")
    html += "<a class='btn' href='/export.txt?tag=" + filter + "'>Download " + htmlEscape(filter) + " TXT</a>";
  html += "</div>";

  int visibleCount = 0;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (filter == "All" || filter == String(noteIndex[i].tag)) visibleCount++;

  if (visibleCount <= 0) {
    html += "<div class='empty'>No notes for this filter.</div>";
  } else {
    html += "<div class='grid'>";
    for (int v = 0; v < (int)noteIndex.size(); v++) {
      int i = (int)noteIndex.size() - 1 - v;
      if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
      int num = noteIndex[i].num;

      char txtPath[64], wavPath[64];
      snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
      snprintf(wavPath, sizeof(wavPath), "%s/note_%03d.wav", NOTES_DIR, num);

      String rawTranscript = readSmallFile(txtPath, 1200);
      if (rawTranscript.length() == 0)
        rawTranscript = noteIndex[i].hasText ? "(empty transcript)" : "Not transcribed yet.";

      // Prefer Claude's enriched fields when present, else fall back to raw.
      String aiTitle     = readNoteAiValue(num, "title");
      String summary     = readNoteAiValue(num, "summary");
      String todosLine   = readNoteAiValue(num, "todos");
      String cleanText   = readNoteAiClean(num);
      String displayText = cleanText.length() ? cleanText : rawTranscript;

      String title = aiTitle;
      if (title.length() == 0) {
        title = rawTranscript; title.replace("\n", " "); title.trim();
        if (title.length() > 58) title = title.substring(0, 58) + "...";
        if (title.length() == 0 || title == "Not transcribed yet.")
          title = String("Voice note ") + String(num);
      }

      html += "<div class='card'>";
      html += "<div class='row'><div><div class='num'>#" + String(num) + "</div>";
      html += "<h2 class='title'>" + htmlEscape(title) + "</h2>";
      String createdUtc = noteCreatedUtc(num);
      if (createdUtc.length() > 0)
        html += "<div class='date' data-utc='" + createdUtc + "'>" + createdUtc + "</div>";
      else
        html += "<div class='date'>time not set</div>";
      html += "</div>";
      html += "<div class='tag'>" + htmlEscape(String(noteIndex[i].tag)) + "</div></div>";
      if (summary.length() > 0)
        html += "<p class='summary'>" + htmlEscape(summary) + "</p>";
      html += "<p class='text'>" + htmlEscape(displayText) + "</p>";
      if (todosLine.length() > 0) {
        html += "<div class='todos'><div class='todos-h'>To-dos</div><ul>";
        int start = 0;
        while (start < (int)todosLine.length()) {
          int bar = todosLine.indexOf('|', start);
          String item = (bar < 0) ? todosLine.substring(start) : todosLine.substring(start, bar);
          item.trim();
          if (item.length() > 0) html += "<li>" + htmlEscape(item) + "</li>";
          if (bar < 0) break;
          start = bar + 1;
        }
        html += "</ul></div>";
      }
      if (SD_MMC.exists(wavPath))
        html += "<audio controls src='/audio?num=" + String(num) + "'></audio>";
      html += "<div class='actions'>";
      html += "<a class='btn primary' href='/txt?num=" + String(num) + "'>Download TXT</a>";
      if (SD_MMC.exists(wavPath))
        html += "<a class='btn' href='/wav?num=" + String(num) + "'>Download WAV</a>";
      html += "<a class='btn' style='margin-left:auto;color:#c0392b;border-color:#c0392b' "
              "href='/note/delete?num=" + String(num) + "' "
              "onclick=\"return confirm('Delete note #" + String(num) + "? This cannot be undone.')\">Delete</a>";
      html += "</div></div>";
    }
    html += "</div>";
  }

  html += "<script>"
          "document.querySelectorAll('[data-utc]').forEach(function(el){"
          "var d=new Date(el.dataset.utc);"
          "if(!isNaN(d)){el.textContent=d.toLocaleString([],{year:'numeric',month:'short',day:'2-digit',hour:'2-digit',minute:'2-digit'});}"
          "});"
          "</script>";
  html += "</div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handlePortalJson() {
  loadIndex();
  String json = "[";
  for (int v = 0; v < (int)noteIndex.size(); v++) {
    int i = (int)noteIndex.size() - 1 - v;
    if (v > 0) json += ",";
    json += "{";
    json += "\"num\":" + String(noteIndex[i].num) + ",";
    json += "\"tag\":\"" + String(noteIndex[i].tag) + "\",";
    json += "\"hasText\":" + String(noteIndex[i].hasText ? "true" : "false");
    json += "}";
  }
  json += "]";
  transferServer.send(200, "application/json", json);
}

void handleExportTxt() {
  loadIndex();
  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  String exportText = "Pala Note Export\nFilter: " + filter + "\n------------------------------\n\n";

  for (int v = 0; v < (int)noteIndex.size(); v++) {
    int i = (int)noteIndex.size() - 1 - v;
    if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
    int num = noteIndex[i].num;
    char txtPath[64]; snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
    String rawTranscript = readSmallFile(txtPath, 4000);
    if (rawTranscript.length() == 0)
      rawTranscript = noteIndex[i].hasText ? "(empty transcript)" : "Not transcribed yet.";

    String exTitle   = readNoteAiValue(num, "title");
    String exSummary = readNoteAiValue(num, "summary");
    String exTodos   = readNoteAiValue(num, "todos");
    String exClean   = readNoteAiClean(num);
    String exBody    = exClean.length() ? exClean : rawTranscript;

    exportText += "#";
    if (num < 100) exportText += "0";
    if (num < 10)  exportText += "0";
    exportText += String(num) + " · " + String(noteIndex[i].tag) + "\n";
    String createdUtc = noteCreatedUtc(num);
    if (createdUtc.length() > 0)   exportText += createdUtc + "\n";
    if (exTitle.length() > 0)      exportText += exTitle + "\n";
    if (exSummary.length() > 0)    exportText += exSummary + "\n";
    exportText += "\n" + exBody + "\n";
    if (exTodos.length() > 0) {
      String t = exTodos; t.replace("|", "\n- ");
      exportText += "\nTo-dos:\n- " + t + "\n";
    }
    exportText += "\n------------------------------\n\n";
    if (exportText.length() > 55000) {
      exportText += "\nExport truncated on device because it became too large.\n";
      break;
    }
  }

  String filename = "pala_notes_export";
  if (filter != "All") filename += "_" + filter;
  filename += ".txt";
  transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  transferServer.send(200, "text/plain", exportText);
}

void sendFileByNum(const char* ext, const char* mime, bool attachment) {
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  char path[64]; snprintf(path, sizeof(path), "%s/note_%03d.%s", NOTES_DIR, num, ext);
  File f = SD_MMC.open(path);
  if (!f) { transferServer.send(404, "text/plain", "File not found"); return; }
  if (attachment) {
    String filename = String("note_") + String(num) + "." + String(ext);
    transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  }
  transferServer.streamFile(f, mime);
  f.close();
}

void handleTagAdd() {
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = urlDecodeSimple(transferServer.arg("name"));
  bool ok = addCustomTag(name.c_str());
  transferServer.sendHeader("Location", ok ? "/tags?msg=added" : "/tags?msg=exists");
  transferServer.send(303);
}

void handleTagDelete() {
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = urlDecodeSimple(transferServer.arg("name"));
  bool hadNotes = tagHasNotes(name.c_str());
  bool ok = deleteTag(name.c_str());
  if (ok && hadNotes) transferServer.sendHeader("Location", "/tags?msg=moved");
  else                transferServer.sendHeader("Location", ok ? "/tags?msg=deleted" : "/tags?msg=protected");
  transferServer.send(303);
}

void handleTagsPage() {
  loadTags();
  loadIndex();
  activeFilter = -1;

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Pala Tags</title>"
                "<style>"
                "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:24px;background:#f3f0e9;color:#111}"
                ".wrap{max-width:720px;margin:0 auto}"
                "h1{font-size:42px;line-height:.9;letter-spacing:-.05em;margin:0 0 22px;font-weight:800}"
                ".card{background:#fffaf1;border:1.5px solid #111;border-radius:24px;padding:18px;margin:14px 0;box-shadow:4px 4px 0 #111}"
                ".row{display:flex;justify-content:space-between;align-items:center;gap:12px;border-top:1px solid #ddd;padding:12px 0}"
                ".row:first-child{border-top:0}"
                ".tag{font-size:20px;font-weight:700}"
                ".meta{font-size:13px;color:#666;margin-top:4px}"
                "input{font:inherit;padding:12px;border:1.5px solid #111;border-radius:999px;background:#fff;width:100%;box-sizing:border-box}"
                "button,.btn{font:inherit;border:1.5px solid #111;border-radius:999px;padding:10px 14px;background:#111;color:#fff;text-decoration:none;white-space:nowrap}"
                ".danger{background:#fffaf1;color:#111}"
                ".msg{border:1.5px solid #111;border-radius:18px;padding:12px 14px;background:#fff;margin:12px 0}"
                ".hint{font-size:13px;color:#666;line-height:1.4}"
                "form.add{display:flex;gap:10px}"
                "</style></head><body><div class='wrap'>";

  html += "<h1>pala<br>tags</h1>";
  html += "<a class='btn' href='/'>Back to notes</a>";

  if (transferServer.hasArg("msg")) {
    String msg = transferServer.arg("msg");
    html += "<div class='msg'>";
    if (msg == "added") html += "Tag added.";
    else if (msg == "exists")    html += "Tag already exists or cannot be added.";
    else if (msg == "deleted")   html += "Tag deleted.";
    else if (msg == "moved")     html += "Tag deleted. Existing notes were moved to Untagged.";
    else if (msg == "protected") html += "This tag cannot be deleted.";
    else html += "Please enter a tag name.";
    html += "</div>";
  }

  html += "<div class='card'><form class='add' action='/tag/add' method='get'>"
          "<input name='name' maxlength='31' placeholder='New tag name'>"
          "<button type='submit'>Add</button></form>"
          "<p class='hint'>Tags appear on the device after recording. Keep them short for the e-paper UI.</p></div>";

  html += "<div class='card'>";
  for (int i = 0; i < tagCount; i++) {
    int cnt = 0;
    for (int n = 0; n < (int)noteIndex.size(); n++)
      if (strcmp(noteIndex[n].tag, tags[i]) == 0) cnt++;
    html += "<div class='row'><div><div class='tag'>" + htmlEscape(String(tags[i])) + "</div>";
    html += "<div class='meta'>" + String(cnt) + (cnt == 1 ? " note" : " notes");
    if (cnt > 0) html += " · deleting moves them to Untagged";
    html += "</div></div>";
    if (strcasecmp(tags[i], "Untagged") != 0) {
      html += "<a class='btn danger' href='/tag/delete?name=" + htmlEscape(String(tags[i])) + "' "
              "onclick=\"return confirm('Delete this tag? Notes will not be deleted. Existing notes will move to Untagged.');\">Delete</a>";
    }
    html += "</div>";
  }
  html += "</div></div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handleNoteDelete() {
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  deleteNote(num);
  transferServer.sendHeader("Location", "/");
  transferServer.send(303);
}

void setupTransferServer() {
  transferServer.on("/", HTTP_GET, handlePortalRoot);
  transferServer.on("/tags", HTTP_GET, handleTagsPage);
  transferServer.on("/tag/add", HTTP_GET, handleTagAdd);
  transferServer.on("/tag/delete", HTTP_GET, handleTagDelete);
  transferServer.on("/note/delete", HTTP_GET, handleNoteDelete);
  transferServer.on("/api/notes", HTTP_GET, handlePortalJson);
  transferServer.on("/export.txt", HTTP_GET, handleExportTxt);
  transferServer.on("/txt",   HTTP_GET, [](){ sendFileByNum("txt", "text/plain", true); });
  transferServer.on("/wav",   HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  true); });
  transferServer.on("/audio", HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  false); });
  transferServer.onNotFound([](){
    transferServer.send(404, "text/plain", "Not found");
  });
}

void stopTransferMode() {
  if (transferServerActive) {
    transferServer.stop();
    transferServerActive = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  transferUrl = "";
}
