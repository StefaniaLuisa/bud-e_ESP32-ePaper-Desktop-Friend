#include "Arduino.h"
#include "SD_MMC.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include <Preferences.h>
#include <vector>
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"

extern "C" {
#include "config.h"
#include "src/i2c_bsp/i2c_bsp.h"
#include "src/audio/audio_bsp.h"
}

#include "src/power/board_power_bsp.h"
#include "src/display/epaper_driver_bsp.h"
#include "logo_bitmap.h"
#include "secrets.h"
#include "sounds.h"

#include <Adafruit_GFX.h>
#include <pgmspace.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "types.h"
#include "globals.h"
#include "src/app/draw.h"
#include "src/app/face.h"
#include "src/app/timer.h"
#include "src/app/battery.h"
#include "src/app/rtc.h"
#include "src/app/notes.h"
#include "src/app/ui.h"
#include "src/app/buttons.h"
#include "src/app/network.h"
#include "src/app/sleep.h"
#include "src/app/record.h"
#include "src/app/ask.h"
#include "src/app/dashsync.h"

// All pin, timing, path and threshold constants live in config.h.

// ─── Content arrays ───────────────────────────────────────────────────────
const char* DEFAULT_TAGS[]    = { "Note", "Work", "Idea", "Buy", "Private" };
const char* MENU_ITEMS[]     = { "Record", "Notes", "Tags", "Timer", "Pomodoro", "Sync", "Settings" };
const char* SETTINGS_ITEMS[] = { "Sounds", "Desk Mode", "Transfer", "Device" };

// ─── Global variable definitions ─────────────────────────────────────────
board_power_bsp_t      board(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
epaper_driver_display* display = nullptr;

std::vector<NoteEntry> noteIndex;

AppState state          = STATE_IDLE;
int      listCursor     = 0;
int      tagCursor      = 2;
int      menuCursor     = 0;
int      settingsCursor = 0;
int      timerSetCursor = 4;   // default simple-timer preset (25 min; index shifted by the temp 1-min preset)
bool     soundsOn       = true;
int      activeFilter   = -1;
int      lastRecNum     = -1;

uint32_t lastActivityMs      = 0;
bool     wokeFromUltraSleep  = false;
bool     wakeToMenuRequested = false;
bool     wakeToRecRequested  = false;

bool     deskMode        = false;   // plugged-in always-on mode (persisted in NVS)
uint32_t lastNudgePollMs = 0;
uint32_t lastWifiCheckMs = 0;
Preferences prefs;

static void saveDeskMode(bool on) {
  prefs.begin("bude", false);
  prefs.putBool("desk", on);
  prefs.end();
}
static bool loadDeskMode() {
  prefs.begin("bude", true);
  bool v = prefs.getBool("desk", false);
  prefs.end();
  return v;
}

uint32_t tickerLastMs = 0;
int      tickerOffset = 0;
int      tickerCursor = -1;

WebServer transferServer(80);
bool      transferServerActive = false;
String    transferUrl          = "";

bool timeReady    = false;
bool audioPlaying = false;
bool stopPlayback = false;

int detailScrollPage = 0;
int detailTotalLines = 0;

uint32_t lastBatCheckMs    = 0;
bool     batLowWarned      = false;
bool     batWarnActive     = false;
uint32_t batWarnShowUntilMs = 0;

char tags[20][32];
int  tagCount = 0;

// ─── Power latch ──────────────────────────────────────────────────────────
void keepBatteryPowerOn() {
  pinMode(PWR_HOLD_PIN, OUTPUT);
  digitalWrite(PWR_HOLD_PIN, HIGH);
}

// ─── Flow functions ───────────────────────────────────────────────────────
void startRecordFlow() {
  state = STATE_RECORDING;
  showRecording();

  palaSoundSetEnabled(false);
  bool recOk = record();
  palaSoundSetEnabled(true);

  if (!recOk) {
    showError("REC FAIL");
    delay(1600);
    state = STATE_IDLE;
    showIdle();
    return;
  }

  soundSaved();

  state = STATE_SAVED;
  showSaved(lastRecNum);
  delay(900);

  tagCursor = min(2, max(tagCount - 1, 0));
  state = STATE_TAG_SELECT;
  showTagSelect(tagCursor);
}

void startSyncFlow() {
  const int MAX_TRIES = 20;
  showWifiConnecting(0, MAX_TRIES);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
    delay(500); tries++;
    showWifiConnecting(tries, MAX_TRIES);
  }

  if (WiFi.status() == WL_CONNECTED) {
    syncTimeFromNTP(6000);
    transcribeAll();
    loadIndex();
    syncNotesToDashboard();   // upload enriched notes to the Mac home base
    reconcileWithDashboard(); // apply retention: drop notes moved/auto-migrated to dashboard-only
    loadIndex();              // refresh after any local removals
    WiFi.disconnect(true);
    showDone();
    soundSuccess();
    delay(1600);
  } else {
    showError("NO WIFI");
    delay(1800);
  }

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else {
    showIdle();
  }
}

void startTransferMode() {
  state = STATE_TRANSFER;
  showTransferConnecting();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const int MAX_TRIES = 24;
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
    delay(500); tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    showError("NO WIFI");
    delay(1600);
    state = STATE_SETTINGS;
    showSettings(settingsCursor);
    return;
  }

  syncTimeFromNTP(8000);
  setupTransferServer();
  transferServer.begin();
  transferServerActive = true;

  IPAddress ip = WiFi.localIP();
  transferUrl = ip.toString();
  showTransferMode(transferUrl.c_str());
}

// ─── Bud-E ask (push-to-talk from the home screen) ─────────────────────────
// Hold REC on the home screen to talk. One self-contained turn: record the
// question, make sure WiFi is up, upload to the Mac brain, play the reply, then
// return home. WiFi is left connected so follow-up questions are fast (it powers
// down when the device sleeps).
void startAskFlow() {
  // 1. Record while REC is held (Bud-E listens).
  showRecording();
  palaSoundSetEnabled(false);
  bool ok = recordAsk(ASK_WAV_PATH);
  palaSoundSetEnabled(true);
  if (!ok) { resetActivity(); showIdle(); return; }   // nothing captured

  // 2. Make sure WiFi is connected (connect on demand).
  if (WiFi.status() != WL_CONNECTED) {
    const int MAX_TRIES = 20;
    showWifiConnecting(0, MAX_TRIES);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
      delay(500); tries++;
      showWifiConnecting(tries, MAX_TRIES);
    }
    if (WiFi.status() != WL_CONNECTED) {
      showError("NO WIFI");
      delay(1600);
      resetActivity();
      showIdle();
      return;
    }
  }

  // 3. Ask the Mac brain and speak the reply.
  showAskThinking();
  if (!askBude(ASK_WAV_PATH, REPLY_WAV_PATH)) {
    showError("NO REPLY");
    delay(1500);
    resetActivity();
    showIdle();
    return;
  }
  showAskSpeaking(askReplyText.c_str());
  playWavFile(REPLY_WAV_PATH);
  resetActivity();
  showIdle();
}

// ─── Desk mode (plugged-in always-on: holds WiFi, polls the Mac for nudges) ──
// Connect WiFi if it isn't, within a time budget. Non-fatal on failure.
static bool ensureWifiConnected(uint32_t budgetMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < budgetMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

// Present a nudge that just arrived: banner + chime + speak it + back home.
static void presentNudge() {
  showNudge(nudgeText.c_str());
  soundNudge();
  playWavFile(NUDGE_WAV_PATH);
  delay(300);
  resetActivity();
  showIdle();
  lastNudgePollMs = millis();   // reset the poll clock after presenting
}

// Called every loop while idle in desk mode. Self-throttles; only runs on the
// home screen so it never interrupts recording, menus, a timer, etc.
static void serviceDeskMode() {
  if (!deskMode || state != STATE_IDLE) return;
  uint32_t now = millis();

  if (now - lastWifiCheckMs > DESK_WIFI_CHECK_MS) {
    lastWifiCheckMs = now;
    // Safety: desk mode suppresses sleep, so if it's unplugged and the battery
    // gets critically low, protect the cell by sleeping anyway (setting stays on
    // for next boot, by which point it's presumably back on power).
    int pct = readBatteryPercent();
    if (pct >= 0 && pct <= BAT_CRITICAL_THRESHOLD) {
      Serial.println("[Desk] battery critical — sleeping to protect the cell");
      enterUltraSleep();
      return;
    }
    if (WiFi.status() != WL_CONNECTED) ensureWifiConnected(8000);
  }

  if (now - lastNudgePollMs > NUDGE_POLL_MS) {
    lastNudgePollMs = now;
    if (WiFi.status() == WL_CONNECTED && pollNudge(NUDGE_WAV_PATH)) {
      presentNudge();
    }
  }
}

// ─── Setup ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Pala Note " FIRMWARE_VERSION " ===");

  pinMode(BTN_REC, INPUT_PULLUP);
  pinMode(BTN_PWR, INPUT_PULLUP);

  board.VBAT_POWER_ON();

  wokeFromUltraSleep  = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1);
  delay(50);

  wakeToMenuRequested = (wokeFromUltraSleep && digitalRead(BTN_PWR) == LOW);
  wakeToRecRequested  = (wokeFromUltraSleep && digitalRead(BTN_REC) == LOW);

  resetActivity();
  keepBatteryPowerOn();
  delay(20);

  board.POWEER_EPD_ON();
  board.POWEER_Audio_ON();
  delay(200);

  custom_lcd_spi_t dispCfg = {};
  dispCfg.cs       = EPD_CS_PIN;
  dispCfg.dc       = EPD_DC_PIN;
  dispCfg.rst      = EPD_RST_PIN;
  dispCfg.busy     = EPD_BUSY_PIN;
  dispCfg.mosi     = EPD_MOSI_PIN;
  dispCfg.scl      = EPD_SCK_PIN;
  dispCfg.spi_host = EPD_SPI_NUM;
  dispCfg.buffer_len = (200*200)/8;

  display = new epaper_driver_display(200, 200, dispCfg);
  display->EPD_Init();
  display->EPD_Clear();
  display->EPD_DisplayPartBaseImage();
  display->EPD_Init_Partial();

  i2c_master_Init();
  delay(50);

  audio_bsp_init();
  audio_play_init();

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    showError("SD ERR");
    while (true) delay(1000);
  }
  if (!SD_MMC.exists(NOTES_DIR)) SD_MMC.mkdir(NOTES_DIR);
  loadTags();
  loadIndex();
  Serial.printf("[SD] %d notes\n", (int)noteIndex.size());

  // Desk mode persists across reboots. If it was on, start the poll/WiFi clocks
  // so serviceDeskMode() connects and polls promptly once we're on the home screen.
  deskMode = loadDeskMode();
  if (deskMode) {
    lastNudgePollMs = 0;
    lastWifiCheckMs = 0;
    Serial.println("[Desk] desk mode ON (will hold WiFi + poll for nudges)");
  }

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else {
    // Wake to the home screen. Holding REC here (handled in the loop) talks to
    // Bud-E; a quick tap-to-wake does nothing. (wakeToRecRequested kept for the
    // boot-time wake reason but no longer auto-records a note.)
    showIdle();
  }
}

// ─── Main loop ────────────────────────────────────────────────────────────
void loop() {

  // Desk mode keeps Bud-E awake (plugged in) so he can hold WiFi and nudge you.
  if (!deskMode &&
      state != STATE_RECORDING && state != STATE_TRANSFER &&
      state != STATE_TIMER_RUN && state != STATE_POMO_RUN) {
    if (millis() - lastActivityMs > ULTRA_SLEEP_MS) {
      enterUltraSleep();
      return;
    }
  }

  serviceDeskMode();   // (no-op unless deskMode && on the home screen)

  if (state == STATE_NOTE_LIST && activeTickerNeedsScroll(listCursor)) {
    if (millis() - tickerLastMs > TICKER_INTERVAL_MS) {
      tickerLastMs = millis();
      tickerOffset++;
      showNoteList(listCursor);
      return;
    }
  }

  if (transferServerActive) transferServer.handleClient();

  // Battery warning: dismiss after 2.5 s without blocking
  if (batWarnActive && millis() >= batWarnShowUntilMs) {
    batWarnActive = false;
    switch (state) {
      case STATE_IDLE:           showIdle();                     break;
      case STATE_MENU:           showMenu(menuCursor);           break;
      case STATE_NOTE_LIST:      showNoteList(listCursor);       break;
      case STATE_NOTE_DETAIL:    showNoteDetail(listCursor);     break;
      case STATE_TAG_SELECT:     showTagSelect(tagCursor);       break;
      case STATE_TAG_BROWSER:    showTagBrowser(tagCursor);      break;
      case STATE_SETTINGS:       showSettings(settingsCursor);   break;
      case STATE_DEVICE_INFO:    showDeviceInfo();               break;
      case STATE_TIMER_RUN:
        showTimerRun(timerRemainingSec(), timerTotalSec(), timerIsPaused());
        break;
      case STATE_POMO_RUN:
        showPomoRun(timerRemainingSec(), timerTotalSec(),
                    pomoPhase() == PP_BREAK, pomoBlock(), timerIsPaused());
        break;
      case STATE_NOTE_READY:     showNoteReady();                break;
      case STATE_DELETE_CONFIRM: {
        int idx = noteAtFilteredIndex(listCursor);
        if (idx >= 0) showDeleteConfirm(noteIndex[idx].num);
        break;
      }
      default: break;
    }
  }

  // Periodic battery check
  if (state != STATE_RECORDING && !audioPlaying && !batWarnActive) {
    if (millis() - lastBatCheckMs > BAT_CHECK_INTERVAL_MS) {
      lastBatCheckMs = millis();
      int pct = readBatteryPercent();
      if (pct >= 0 && pct <= BAT_LOW_THRESHOLD && !batLowWarned) {
        batLowWarned        = true;
        batWarnActive       = true;
        batWarnShowUntilMs  = millis() + 2500;
        showBatteryLow(pct);
      } else if (pct > BAT_RECOVER_THRESHOLD) {
        batLowWarned = false;
      }
    }
  }

  // IDLE ─────────────────────────────────────────────────────────────────
  if (state == STATE_IDLE) {
    // Occasional blink so Bud-E feels alive. Only once you've settled on the
    // home screen (so it doesn't blink the instant you arrive), and the device
    // still sleeps normally after ULTRA_SLEEP_MS of real inactivity.
    static uint32_t lastBlinkMs = 0;
    uint32_t blinkEvery = deskMode ? DESK_BLINK_MS : IDLE_BLINK_MS;
    if (blinkEvery > 0) {
      uint32_t now = millis();
      if (now - lastActivityMs > 1500 && now - lastBlinkMs > blinkEvery) {
        showIdleBlink();      // eyes closed
        delay(130);
        showIdle();           // eyes open again
        lastBlinkMs = millis();
      }
    }

    if (handleIdleRec()) return;

    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    if (pwr == EV_SINGLE || pwr == EV_LONG) {
      soundSelect();
      menuCursor = 0;
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // TAG SELECT after recording ──────────────────────────────────────────
  else if (state == STATE_TAG_SELECT) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE || rec == EV_LONG) {
      soundSelect();
      saveTag(lastRecNum, tags[constrain(tagCursor, 0, max(tagCount - 1, 0))]);
      // Note saved → back to Bud-E's home face (instead of sleeping immediately).
      // The normal idle timeout still sleeps it after ULTRA_SLEEP_MS.
      resetActivity();
      state = STATE_IDLE;
      showIdle();
    } else if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      showTagSelect(tagCursor);
    }
  }

  // MENU ────────────────────────────────────────────────────────────────
  else if (state == STATE_MENU) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      menuCursor = (menuCursor + 1) % MENU_COUNT;
      showMenu(menuCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (menuCursor == 0) {                 // Record a note
        state = STATE_NOTE_READY;
        showNoteReady();
      } else if (menuCursor == 1) {          // Notes
        activeFilter = -1; listCursor = 0;
        state = STATE_NOTE_LIST;
        showNoteList(listCursor);
      } else if (menuCursor == 2) {          // Tags
        tagCursor = 0;
        state = STATE_TAG_BROWSER;
        showTagBrowser(tagCursor);
      } else if (menuCursor == 3) {          // Timer
        timerSetCursor = constrain(timerSetCursor, 0, TIMER_PRESET_COUNT - 1);
        state = STATE_TIMER_SET;
        showTimerSet(timerSetCursor);
      } else if (menuCursor == 4) {          // Pomodoro
        timerStartPomodoro();
        state = STATE_POMO_RUN;
        showPomoRun(timerRemainingSec(), timerTotalSec(), false, pomoBlock(), false);
      } else if (menuCursor == 5) {          // Sync
        startSyncFlow();
      } else {                               // Settings
        settingsCursor = 0;
        state = STATE_SETTINGS;
        showSettings(settingsCursor);
      }
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_IDLE;
      showIdle();
    }
  }

  // SETTINGS ────────────────────────────────────────────────────────────
  else if (state == STATE_SETTINGS) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      settingsCursor = (settingsCursor + 1) % SETTINGS_COUNT;
      showSettings(settingsCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (settingsCursor == 0) {                 // Sounds
        palaSoundSetEnabled(!palaSoundIsEnabled());
        showSettings(settingsCursor);
      } else if (settingsCursor == 1) {          // Desk Mode toggle
        deskMode = !deskMode;
        saveDeskMode(deskMode);
        showDeskMode(deskMode);
        if (deskMode) {
          ensureWifiConnected(8000);             // connect now so nudges start flowing
          lastNudgePollMs = millis();            // first poll one interval from now
          lastWifiCheckMs = millis();
        } else {
          WiFi.disconnect(true);                 // drop WiFi; normal sleep resumes
        }
        delay(900);
        showSettings(settingsCursor);
      } else if (settingsCursor == 2) {          // Transfer
        startTransferMode();
      } else {                                   // Device info
        state = STATE_DEVICE_INFO;
        showDeviceInfo();
      }
    } else if (rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // DEVICE INFO ─────────────────────────────────────────────────────────
  else if (state == STATE_DEVICE_INFO) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_DOUBLE || rec == EV_LONG || rec == EV_SINGLE || pwr == EV_SINGLE) {
      soundBack();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TIMER SET (duration picker) ─────────────────────────────────────────
  else if (state == STATE_TIMER_SET) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      timerSetCursor = (timerSetCursor + 1) % TIMER_PRESET_COUNT;
      showTimerSet(timerSetCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      timerStartSimple(TIMER_PRESETS[timerSetCursor]);
      state = STATE_TIMER_RUN;
      showTimerRun(timerRemainingSec(), timerTotalSec(), false);
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // TIMER RUN (simple countdown) ────────────────────────────────────────
  else if (state == STATE_TIMER_RUN) {
    static int  lastShown  = -1;
    static bool lastPaused = false;
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_LONG || rec == EV_DOUBLE) {          // cancel
      soundBack();
      timerCancel();
      lastShown = -1; lastPaused = false;
      state = STATE_IDLE;
      showIdle();
    } else if (pwr == EV_SINGLE) {                     // pause / resume
      timerPauseToggle();
      soundSelect();
      lastShown = -1;
      showTimerRun(timerRemainingSec(), timerTotalSec(), timerIsPaused());
      lastPaused = timerIsPaused();
    } else if (timerExpired()) {                       // finished
      timerCancel();
      soundTimerDone();
      showTimerDone("time's up", "", true);
      delay(2500);
      lastShown = -1; lastPaused = false;
      resetActivity();                                 // give the home screen its full idle window
      state = STATE_IDLE;
      showIdle();
    } else {
      int rem   = timerRemainingSec();
      int shown = (rem > 60) ? (rem + 59) / 60 : rem;  // minutes above 60s, else seconds
      if (shown != lastShown || timerIsPaused() != lastPaused) {
        lastShown  = shown;
        lastPaused = timerIsPaused();
        showTimerRun(rem, timerTotalSec(), timerIsPaused());
      }
    }
  }

  // POMODORO RUN (focus / break cycles) ─────────────────────────────────
  else if (state == STATE_POMO_RUN) {
    static int  lastShownP  = -1;
    static bool lastPausedP = false;
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_LONG || rec == EV_DOUBLE) {          // cancel the whole session
      soundBack();
      timerCancel();
      lastShownP = -1; lastPausedP = false;
      state = STATE_IDLE;
      showIdle();
    } else if (pwr == EV_SINGLE) {                     // pause / resume
      timerPauseToggle();
      soundSelect();
      lastShownP = -1;
      showPomoRun(timerRemainingSec(), timerTotalSec(),
                  pomoPhase() == PP_BREAK, pomoBlock(), timerIsPaused());
      lastPausedP = timerIsPaused();
    } else if (timerExpired()) {                       // phase finished
      soundTimerDone();
      bool wasFocus  = (pomoPhase() == PP_FOCUS);
      bool lastBlock = (pomoBlock() >= POMO_BLOCKS);
      bool lastBreak = (!wasFocus && lastBlock);
      // Celebrate the phase that just ended (advance AFTER, so the next
      // phase isn't shortened by this screen's delay).
      char sub[12];
      if (wasFocus && lastBlock) {                      // 4th focus → long break
        snprintf(sub, sizeof(sub), "%d min", POMO_LONG_BREAK_MIN);
        showTimerDone("long break", sub, true);
      } else if (wasFocus) {                            // focus → short break
        snprintf(sub, sizeof(sub), "%d min", POMO_BREAK_MIN);
        showTimerDone("break!", sub, true);
      } else if (lastBreak) {                           // final break done
        showTimerDone("done!", "great work", true);
      } else {                                          // break → next focus
        showTimerDone("focus!", "back to it", false);
      }
      delay(lastBreak ? 3000 : 2500);

      PomoAdvance adv = pomoAdvance();
      if (adv == PA_ALL_DONE) {
        timerCancel();
        lastShownP = -1; lastPausedP = false;
        resetActivity();
        state = STATE_IDLE;
        showIdle();
      } else {
        lastShownP = -1;                               // redraw the new phase next loop
      }
    } else {
      int rem   = timerRemainingSec();
      int shown = (rem > 60) ? (rem + 59) / 60 : rem;
      if (shown != lastShownP || timerIsPaused() != lastPausedP) {
        lastShownP  = shown;
        lastPausedP = timerIsPaused();
        showPomoRun(rem, timerTotalSec(), pomoPhase() == PP_BREAK,
                    pomoBlock(), timerIsPaused());
      }
    }
  }

  // NOTE READY ("hold rec to record a note") ────────────────────────────
  else if (state == STATE_NOTE_READY) {
    // Hold REC to record a note (record → tag → sleep, the classic flow).
    if (isDown(BTN_REC)) {
      resetActivity();
      delay(20);
      if (isDown(BTN_REC)) {
        uint32_t t0 = millis();
        while (isDown(BTN_REC) && millis() - t0 < REC_HOLD_MS) delay(5);
        if (isDown(BTN_REC)) startRecordFlow();
      }
    }
    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    if (pwr == EV_SINGLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // TRANSFER MODE ───────────────────────────────────────────────────────
  else if (state == STATE_TRANSFER) {
    if (transferServerActive) transferServer.handleClient();
    ButtonEvent rec = readButtonEvent(BTN_REC);
    if (rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      stopTransferMode();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TAG BROWSER ─────────────────────────────────────────────────────────
  else if (state == STATE_TAG_BROWSER) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      showTagBrowser(tagCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      activeFilter = tagCursor; listCursor = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE LIST ───────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_LIST) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    int count = filteredCount();

    if (pwr == EV_SINGLE && count > 0) {
      soundNext();
      listCursor = (listCursor + 1) % count;
      showNoteList(listCursor);
    } else if (pwr == EV_DOUBLE && count > 0) {
      soundNext();
      listCursor = (listCursor - 1 + count) % count;
      showNoteList(listCursor);
    } else if (rec == EV_SINGLE && count > 0) {
      soundSelect();
      detailScrollPage = 0;
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE DETAIL ─────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_DETAIL) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      const int linesPerPage = 7;
      int totalPages = (detailTotalLines + linesPerPage - 1) / linesPerPage;
      if (detailScrollPage + 1 < totalPages) {
        detailScrollPage++;
      } else {
        detailScrollPage = 0;
        int count = filteredCount();
        if (count > 0) listCursor = (listCursor + 1) % count;
      }
      showNoteDetail(listCursor);
    } else if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        char wavPath[64];
        snprintf(wavPath, sizeof(wavPath), "%s/note_%03d.wav", NOTES_DIR, noteIndex[idx].num);
        showPlaybackOverlay();
        playWavFile(wavPath);
        showNoteDetail(listCursor);
      }
    } else if (rec == EV_LONG) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        state = STATE_DELETE_CONFIRM;
        showDeleteConfirm(noteIndex[idx].num);
      }
    } else if (rec == EV_DOUBLE) {
      soundBack();
      detailScrollPage = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    }
  }

  // DELETE CONFIRM ──────────────────────────────────────────────────────
  else if (state == STATE_DELETE_CONFIRM) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        deleteNote(noteIndex[idx].num);
        soundDelete();
      }
      detailScrollPage = 0;
      listCursor = constrain(listCursor, 0, max(filteredCount() - 1, 0));
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (pwr == EV_SINGLE || rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    }
  }

  delay(15);
}
