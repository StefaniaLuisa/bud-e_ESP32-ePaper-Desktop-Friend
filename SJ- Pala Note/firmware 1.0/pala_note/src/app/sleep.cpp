#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "sleep.h"
#include "ui.h"
#include "face.h"
#include "network.h"
#include "../../sounds.h"
#include "WiFi.h"

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

void resetActivity() {
  lastActivityMs = millis();
}

void enterUltraSleep() {
  // A brief "winding down" tired face before Bud-E dozes off.
  drawBudeFace(MOOD_TIRED);
  delay(650);

  showUltraSleepScreen();
  delay(120);

  stopTransferMode();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  audio_playback_set_vol(0);
  palaSoundSetEnabled(false);

  board.VBAT_POWER_ON();

  uint64_t wakeMask = (1ULL << BTN_REC) | (1ULL << BTN_PWR);
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

  delay(50);
  esp_deep_sleep_start();
}
