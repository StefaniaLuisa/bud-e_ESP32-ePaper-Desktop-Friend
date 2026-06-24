# Bud-E — Project Documentation

> A pocket/desk voice-note device built on a purchased **Pala Note** ESP32-S3 firmware,
> rebuilt to run on **Groq + Claude** instead of OpenAI, and being grown into a
> cute, kind **desktop buddy** named **Bud-E**.

**Last updated:** 2026-06-24
**Hardware:** Waveshare **ESP32-S3-ePaper-1.54** (200×200 e-paper, ES7210 mic, ES8311 speaker, microSD, RTC, battery)
**Firmware project:** `SJ- Pala Note/firmware 1.0/pala_note/` (Arduino, open `pala_note.ino`)
**Firmware version:** `v1.1`

---

## 1. What this is

Stef purchased the **Pala Note** "AI Note" firmware (a ko-fi project) for a Waveshare
ESP32-S3 ePaper board. The goals, in order, became:

1. **Stop using OpenAI** — the original firmware sent recordings to OpenAI Whisper.
2. **Add Claude** to make notes smart (titles, summaries, to-dos…).
3. **Get the device working** and document how to use it.
4. **Grow it into a desktop buddy** — a cute companion ("Bud-E") that eventually
   answers questions about your day, runs timers, and checks in on you.

The device is **working today** as a Groq + Claude voice-note recorder, and now has a
**face** (Bud-E) on its home, sleep, and recording screens.

---

## 2. The key constraint that shaped everything

**Claude has no speech-to-text / audio API.** You cannot send Claude a `.wav` and get a
transcript back — Claude handles text, images, and PDFs only. So "swap OpenAI for Claude"
is not 1:1; the voice→text step *must* use a real STT service.

After evaluating options:

| Option | Verdict |
|--------|---------|
| OpenAI Whisper | ❌ Rejected — the whole point was to leave OpenAI |
| useminutes.app / Minutes | ❌ Desktop app, **no device-callable web API** |
| **Groq (Whisper)** | ✅ **Chosen** — free tier, **OpenAI-compatible** API, so barely any code change |
| Deepgram, etc. | Viable but Groq won on free + compatibility |

**Final design:** **Groq** does voice→text, **Claude** does the smart enrichment.

---

## 3. Architecture — voice-note pipeline (built & working)

```
  Press REC (hold) ──► record .wav to microSD ──► pick a tag ──► sleep
                                                                  │
                              Menu ► Sync (needs WiFi) ───────────┘
                                                                  ▼
   ┌──────────────────────────────────────────────────────────────────┐
   │  For each un-synced note:                                         │
   │   1. Groq  : .wav ─► raw text   → saved as note_NNN.txt           │
   │   2. Claude: raw text ─► {title, summary, cleaned text,           │
   │              tag, to-dos}        → saved as note_NNN.ai           │
   └──────────────────────────────────────────────────────────────────┘
                                   ▼
        Local web portal (Transfer mode) shows the rich version
```

**Both cloud calls are best-effort:** if Claude fails you still keep the raw Groq
transcript, so a note never breaks. The original raw transcript is always retained
(nothing Claude does is destructive).

### The 5 smart fields Claude produces per note
1. **Clean title** (when Claude's title is absent, the portal falls back to the first 58 chars of the raw transcript + "…", or "Voice note N")
2. **One-line summary**
3. **Cleaned-up transcript** (fixes dictation typos / punctuation / filler words)
4. **Auto-tag** — only fills in if the tag is still generic ("Note"/"Untagged");
   it **never overrides** a tag you deliberately picked
5. **To-dos / action items** as a checklist

### Files on the microSD per note (`/notes/`)
| File | Contents |
|------|----------|
| `note_NNN.wav` | the audio recording |
| `note_NNN.txt` | raw transcript (created at Sync, by Groq) |
| `note_NNN.ai` | Claude's smart fields (created at Sync) |
| `note_NNN.meta` | timestamp + tag + synced flag |

---

## 4. Firmware changes (Pala Note → Groq + Claude)

| File | What changed |
|------|--------------|
| `secrets.h` | Replaced the OpenAI key with **`GROQ_KEY`** + **`ANTHROPIC_KEY`**; WiFi `WIFI_SSID` / `WIFI_PASS` |
| `config.h` | Added cloud macros (below); bumped version to **v1.1** |
| `src/app/network.cpp` | Transcription now calls **Groq**; added `enrichNoteWithClaude` + the web-portal display of title/summary/cleaned text/to-dos |
| `src/app/notes.cpp` / `notes.h` | New `note_NNN.ai` sidecar read/write helpers; delete now also removes the `.ai` file |

### Cloud config (`config.h`)
```c
#define STT_HOST    "api.groq.com"
#define STT_PATH    "/openai/v1/audio/transcriptions"
#define STT_MODEL   "whisper-large-v3-turbo"

#define CLAUDE_HOST     "api.anthropic.com"
#define CLAUDE_PATH     "/v1/messages"
#define CLAUDE_MODEL    "claude-haiku-4-5"   // cheap + fast; great for this job
#define CLAUDE_VERSION  "2023-06-01"
#define CLAUDE_MAXTOK   1024
```

> **Secrets:** `secrets.h` holds the real `GROQ_KEY`, `ANTHROPIC_KEY`, and WiFi
> credentials. Keep that file private — don't commit it to a public repo.

---

## 5. Bud-E — the face (built)

You named the buddy **Bud-E** and chose to match **EMO's eyes** (living.ai/emo;
reference repo `AsutoshPati/Ani-Emo-Eye`). The lucky break: that project draws EMO's
eyes with **Adafruit GFX primitives** (`fillRoundRect`, arcs, circles) — the *same*
graphics library Pala Note already uses — so the technique ported natively without
needing their code.

**e-paper reality:** the screen refreshes ~once/second and ghosts if pushed, so Bud-E's
expressions **snap to a new mood on events** rather than animate continuously. Colors are
**black eyes on a white background** (chosen deliberately — avoids the heavy full-black
refresh, ghosts less, sips less battery).

### The face module (`src/app/face.cpp` / `face.h`)
A self-contained module with **7 moods**:

```
   AWAKE      BLINK      HAPPY      SLEEPY     LISTENING   THINKING    LOVE
  rounded   squashed   bottom-    low slits   big & tall   glance up   hearts
   rects      bars      curved                  alert
```

- `drawBudeEyes()` — draws just the eyes (so they compose with name/clock/battery)
- `drawBudeFace()` — the standalone full-face version

**Eye geometry** lives as one-line constants at the top of `face.cpp`, so tuning is trivial:

```c
EYE_W   = 52   // eye width
EYE_H   = 64   // eye height (AWAKE)
EYE_R   = 18   // corner radius (roundness)
EYE_GAP = 28   // space between the two eyes
EYE_TOP = 60   // y of the eye top
```

### Where Bud-E appears (`ui.cpp`)
| Screen | Before | Now |
|--------|--------|-----|
| **Home** (idle) | "Pala" wordmark + battery ring | Bud-E **awake eyes** + name + **clock (HH:MM)** + battery % |
| **Sleep** (the full-screen "big logo" moment) | Plain logo | **Sleepy Bud-E** — half-closed eyes + "zzz" |
| **Recording** | A white dot | **Bud-E listening** — big alert eyes + rec dot |

Also added `localClockHHMM()` — reads the RTC and shows local time (or `--:--` until a
Sync has set the clock). **Note logic (record/sync/notes) was not touched.**

> **Clock caveat:** it shows the time when you land on the home screen but doesn't *tick*
> live yet, because the device deep-sleeps after ~2 minutes to save battery. A live clock
> only matters once Bud-E is a plugged-in, always-on desk buddy (a Phase-2 "desk mode").

---

## 6. How to use the device today

Two buttons: **REC** (record / select / play) and **PWR** (next / navigate).

### Record
1. Home screen → **press and hold REC**, speak, **release to stop** (~½ s minimum).
2. **Pick a tag:** tap **PWR** to cycle (Note, Work, Idea, Buy, Private), tap **REC** to confirm.
3. The full-screen Bud-E logo/sleepy face = **"saved, going to sleep."** Press **REC** to wake.

> Recording only saves **audio**. Text + Claude smarts appear **only after Menu → Sync** (needs WiFi).

### Play back
Wake (**REC**) → **PWR** to open Menu → stop on **Notes**, tap **REC** → **PWR** = next note →
**REC** to open → **REC** again to play audio → **REC** to stop. Back out with **double-tap REC**.

### Sync (the AI step — the only thing that reaches the internet)
Menu → **Sync**. Joins WiFi, runs Groq + Claude on every un-synced note.

### Transfer (local web portal — does NOT reach the internet)
Menu → Settings → **Transfer**. The device joins your home WiFi, syncs the clock via NTP
(the only outbound call), and starts a **web server on port 80**, showing its **local IP**
on screen. Open that IP in a browser **on the same WiFi** to list/play/download/manage
notes. Notes never leave your LAN during Transfer.

See **`PALA_NOTE_USER_GUIDE.md`** for the full plain-language manual + cheat sheet.

---

## 7. Build & flash (Arduino IDE)

One-time setup (already done on Stef's Mac):
- Arduino IDE 2.x + **esp32 by Espressif** board package (Boards Manager).
- **Adafruit GFX Library** (+ its **Adafruit BusIO** dependency) — the only external lib needed.
- **LVGL / SensorLib are NOT needed** for Pala Note (it draws with Adafruit GFX, not LVGL).
  Don't copy `lvgl8`/`lvgl9`/`SensorLib` — `lvgl8` and `lvgl9` would even conflict.

To flash:
1. Plug the device into the Mac with a **data** USB-C cable.
2. Open `…/SJ- Pala Note/firmware 1.0/pala_note/pala_note.ino`.
3. **Tools → Port** → select the device (`/dev/cu.usbmodem…` or `/dev/cu.wchusbserial…`).
4. **Upload** (→ button / ⌘U). Done when the bar says **"Done uploading."**

Notes:
- **First flash only** (to wipe the prior "Osmo" firmware): set **Erase All Flash Before
  Sketch Upload = Enabled**, and confirm board settings against `Tools Configuration.png`
  (ESP32S3 Dev Module, USB CDC On Boot = Enabled, PSRAM = OPI PSRAM, correct Flash Size /
  Partition Scheme). Flashing a new sketch **overwrites** the app — you don't "uninstall" Osmo.
- **Later flashes:** a normal Upload is fine; settings persist.
- **If Upload can't connect:** hold **BOOT** (the REC button, GPIO0), press/release **RESET**
  (or unplug/replug), then Upload.

---

## 8. The "desktop buddy" vision (Phase 2 — decided, not yet built)

The big-picture goal: Bud-E becomes a kind desk companion you **talk to** that answers
about your day, runs timers, and checks in on you.

**Architecture principle: the device is the whole interface.** Mic in, speaker out,
expressive face, buttons — Bud-E is the thing you talk to *and* that talks back. A
**headless Mac app is invisible backend only**: it does STT (Groq) + Claude reasoning +
data fetching, and **generates TTS audio it sends to the device** to play through the
device's own speaker (so Bud-E speaks, not the Mac).

```
        YOU
         │  speak ▲ ▼ hear        (all on the device)
   ┌─────┴───────────────┐
   │   THE DEVICE        │   mic · speaker · cute e-paper face · buttons
   │   (Bud-E)           │   timers · pomodoro · clock · voice notes
   └─────┬───────────────┘
         │  WiFi (invisible)
   ┌─────┴───────────────┐
   │   MAC (headless)    │   Groq STT · Claude · your data · TTS audio
   │   the silent engine │   Calendar · Productive · Gmail · Slack · iMessage
   └─────────────────────┘   scheduled check-ins
```

**Inspiration:** Instructables "Desktop Companion Robot" (Compagnon 309). Reference client
at `…/Hello Friend/desktop_companion_client.py` is **Windows-only** and just pushes an
activity "state" word to an ESP `/state` endpoint to drive a face — it needs a macOS rewrite.

### Data integrations Stef wants the Mac backend to pull
- **Google Calendar** — meetings / "how's my day"
- **Productive.io** — agency task management (REST API w/ token)
- **Slack**
- **Gmail**
- **iMessage notifications** — local Messages DB (needs Full Disk Access)
- *Harder / maybe-not:* incoming-call caller-ID, reading voicemails aloud (iPhone features
  macOS doesn't cleanly expose)

---

## 9. Roadmap & phasing

| Phase | Scope | Status |
|-------|-------|--------|
| **0** | Groq + Claude voice-note pipeline | ✅ Built & working |
| **1** | On-device buddy: **face (done)**, clock, **timer**, **pomodoro** — no Mac needed | 🔨 Face done; timers next |
| **2** | Mac brain: voice Q&A ("how's my day"), data integrations, scheduled check-ins, device speaks Mac-generated TTS, face reacts to activity | 📋 Designed, not built |
| **3** | Stretch: caller ID, voicemail-aloud | 💭 Maybe |

Possible small hardware add later: a **vibration motor** for gentle haptic nudges.

---

## 10. Untapped hardware (enhancement runway)

The board is far more capable than the app currently uses. All of these already have
driver/example code in the repo (`02_Example/`, `03_Firmware/`, `01_Arduino_Libraries/`)
— **no new chips required:**

| Capability | Chip / detail | Could enable |
|------------|---------------|--------------|
| Temp/humidity sensor | SHTC3 @ I²C 0x70 | Stamp notes with room temp/humidity; environmental logging |
| Capacitive touch | FT6336 @ I²C 0x38 | Touch/gesture nav — *only on the "Touch" board variant* |
| Bluetooth LE | Built into ESP32-S3 | Phone sync/backup without WiFi; pair-to-configure |
| Spare GPIO / ADC / PWM / I²S1 | — | Vibration motor, extra buttons, sensors |

**Already used today:** e-paper (200×200 partial refresh), ES7210 mic + ES8311 speaker
(16 kHz), SD card, PCF85063 RTC (timestamps + NTP), battery ADC (GPIO4) with low-battery
warning, the two buttons, WiFi, power-rail gating, deep-sleep/wake.

---

## 11. Immediate next step

Bud-E's eye **geometry hasn't been seen on real hardware yet**. Flash the current firmware,
look at the Home / Sleep / Recording screens, and decide if the eyes should be
**bigger / rounder / closer** (one-line constant tweaks in `face.cpp`). After that, the
planned next build is the **timer + pomodoro** (happy/love face + chime when done) to
complete Phase 1.

---

## 12. File map (quick reference)

```
SJ- Pala Note/
├─ BUD-E_PROJECT.md            ← this document
├─ PALA_NOTE_USER_GUIDE.md     ← plain-language how-to-use manual
├─ START HERE.pdf              ← creator's pointer to the Craft assembly guide
└─ firmware 1.0/pala_note/
   ├─ pala_note.ino            ← main sketch
   ├─ secrets.h                ← GROQ_KEY, ANTHROPIC_KEY, WiFi (keep private)
   ├─ config.h                 ← cloud macros (Groq/Claude), version v1.1
   ├─ globals.h / types.h
   └─ src/app/
      ├─ network.cpp/.h        ← Groq transcription + Claude enrichment + web portal
      ├─ notes.cpp/.h          ← note storage incl. .ai sidecar
      ├─ face.cpp/.h           ← Bud-E's EMO-style eyes (7 moods)  [NEW]
      ├─ ui.cpp/.h             ← screens (home/sleep/recording show Bud-E)
      ├─ draw.cpp/.h           ← low-level e-paper framebuffer renderer
      ├─ buttons.cpp, record.cpp, rtc.h, ...
```
