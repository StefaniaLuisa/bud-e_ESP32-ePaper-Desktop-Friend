#ifndef CONFIG_H
#define CONFIG_H

#define EPD_SPI_NUM        SPI2_HOST
#define ESP32_I2C_DEV_NUM  I2C_NUM_0

#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define LVGL_SPIRAM_BUFF_LEN (EPD_WIDTH * EPD_HEIGHT * 2)

/* EPD SPI pins */
#define EPD_DC_PIN    GPIO_NUM_10
#define EPD_CS_PIN    GPIO_NUM_11
#define EPD_SCK_PIN   GPIO_NUM_12
#define EPD_MOSI_PIN  GPIO_NUM_13
#define EPD_RST_PIN   GPIO_NUM_9
#define EPD_BUSY_PIN  GPIO_NUM_8


/* Power control pins */
#define EPD_PWR_PIN     GPIO_NUM_6
#define Audio_PWR_PIN   GPIO_NUM_42
#define VBAT_PWR_PIN    GPIO_NUM_17
#define BAT_ADC_PIN     4   // ADC1_CHANNEL_3 on ESP32-S3

#define BOOT_BUTTON_PIN GPIO_NUM_0
#define PWR_BUTTON_PIN  GPIO_NUM_18

/* Deep-sleep wake-up pin */
#define ext_wakeup_pin_1 GPIO_NUM_0

/* I2C bus */
#define ESP32_I2C_SDA_PIN GPIO_NUM_47
#define ESP32_I2C_SCL_PIN GPIO_NUM_48

/* LVGL tick timing */
#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 100

/* I2C peripheral addresses */
#define I2C_RTC_DEV_Address        0x51
#define I2C_SHTC3_DEV_Address      0x70

/* Button GPIO aliases */
#define BTN_REC      0
#define BTN_PWR      18
#define PWR_HOLD_PIN 17

/* SD-MMC pins */
#define SD_CLK  39
#define SD_CMD  41
#define SD_D0   40

/* Audio */
#define SAMPLE_RATE  16000
#define REC_BUF      (8 * 1024)

/* Storage paths */
#define NOTES_DIR  "/notes"
#define INDEX_FILE "/notes/index.csv"
#define TAG_FILE   "/notes/tags.txt"
#define MAX_TAGS   20

/* UI timing */
#define REC_HOLD_MS         350
#define BTN_LONG_MS         600
#define DOUBLE_MS           200
#define ULTRA_SLEEP_MS      120000UL
#define TICKER_INTERVAL_MS  950
#define IDLE_BLINK_MS       5000     // how often Bud-E blinks on the home screen (0 = never)
#define DESK_BLINK_MS       25000UL  // slower blink in desk mode (spare the e-paper over long sessions)

/* Desk mode (Phase 2): plugged-in "always on" mode. Bud-E stays awake, holds
 * WiFi, and polls the Mac brain for proactive nudges (meeting heads-up, etc.). */
#define NUDGE_POLL_MS       30000UL  // how often to ask the Mac "anything for me?"
#define DESK_WIFI_CHECK_MS  15000UL  // how often to verify/reconnect WiFi in desk mode
#define NUDGE_WAV_PATH      "/nudge.wav"   // temp download for a nudge's spoken audio

/* Battery warning */
#define BAT_CHECK_INTERVAL_MS  30000
#define BAT_LOW_THRESHOLD      15
#define BAT_RECOVER_THRESHOLD  20
#define BAT_CRITICAL_THRESHOLD 5    // desk mode force-sleeps below this to protect the cell

/* Time & firmware */
#define LOCAL_TIME_OFFSET_MIN  (-300)   // UTC-5 = Chicago Central Daylight Time (summer).
                                        // Fixed offset (no auto-DST): in winter (CST) change to -360.
// Bump this every flash so the device + Serial banner report what's on it.
// v1.5 = Desk mode (plugged-in always-on): stays awake, holds WiFi, polls the
//        Mac for proactive meeting nudges (face + chime + spoken). Settings →
//        Desk Mode toggle. (Bump to v1.6 on the next flash.)
// v1.4 = Bud-E: RoboEyes face + blink, Timer/Pomodoro, push-to-talk ask,
//        notes→dashboard sync, retention.
#define FIRMWARE_VERSION       "v1.5"
#define FW_VERSION             "v1.5"

/* ─── Cloud AI services ─────────────────────────────────────────────────────
 * Speech-to-text is done by Groq (OpenAI-compatible Whisper endpoint).
 * Note enrichment (title/summary/cleanup/tag/to-dos) is done by Claude.
 * API keys live in secrets.h, not here. */

// Groq Whisper (voice -> text)
#define STT_HOST    "api.groq.com"
#define STT_PATH    "/openai/v1/audio/transcriptions"
#define STT_MODEL   "whisper-large-v3-turbo"

// Anthropic Claude (text -> smart fields)
#define CLAUDE_HOST     "api.anthropic.com"
#define CLAUDE_PATH     "/v1/messages"
#define CLAUDE_MODEL    "claude-haiku-4-5"   // cheap + fast; great for this job
#define CLAUDE_VERSION  "2023-06-01"
#define CLAUDE_MAXTOK   1024

/* ─── Bud-E brain (Phase 2: push-to-talk) ───────────────────────────────────
 * The Mac runs bud_e_server.py; the device POSTs recorded audio to it over the
 * LAN and plays back the spoken reply. Set BUDE_HOST to your Mac's LAN IP
 * (bud_e_server.py prints it on startup). Device + Mac must share WiFi. */
#define BUDE_HOST       "192.168.1.130"   // <-- your Mac's LAN IP
#define BUDE_PORT       8765
#define ASK_WAV_PATH    "/ask.wav"        // temp upload (the question)
#define REPLY_WAV_PATH  "/reply.wav"      // temp download (Bud-E's spoken answer)

#endif // CONFIG_H