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
A self-contained module with **9 moods**, drawn in the **FluxGarage RoboEyes** style
(rounded-rect eyes + triangular/curved eyelid overlays — the repo lives at
`SJ- Pala Note/RoboEyes-main/`; we ported its *expressions*, not its OLED animation engine):

```
   AWAKE      BLINK      HAPPY      SLEEPY     LISTENING   THINKING    LOVE     TIRED      ANGRY
  rounded   squashed   bottom-    low slits   big & tall   glance up   hearts   outer     inner
   rects      bars      curved                  alert                          lids droop lids droop
```

- `drawBudeEyes()` — draws just the eyes (so they compose with a name, etc.)
- `drawBudeFace()` — the standalone full-face version

**Mood triggers (where each emotion shows):** awake = home · listening = recording ·
thinking = working · love/happy = pomodoro completion · sleepy = sleep screen ·
**tired = a brief beat as Bud-E falls asleep** (`enterUltraSleep()`) ·
**angry = error screens** (NO WIFI / SD ERR / REC FAIL, via `showError()`).

**Blinking (feels alive):** on the home screen Bud-E blinks every `IDLE_BLINK_MS` (5 s,
in `config.h`; 0 disables) — `showIdleBlink()` draws closed eyes, then `showIdle()` reopens
them. On e-paper this is a deliberate **slow wink**, not an OLED-fast blink (accepted limit).
It only blinks once you've settled on the screen, and the device still sleeps normally after
2 min of real inactivity.

**Eye geometry** lives as one-line constants at the top of `face.cpp`, so tuning is trivial:

```c
EYE_W   = 52   // eye width
EYE_H   = 64   // eye height (AWAKE)
EYE_R   = 18   // corner radius (roundness)
EYE_GAP = 28   // space between the two eyes
EYE_TOP = 60   // y of the eye top
```

### Where Bud-E appears (`ui.cpp`)
| Screen | What it shows |
|--------|--------------|
| **Home** (idle) | Bud-E's **awake eyes + "bud-e"** only — *blinks periodically*. (Clock & battery removed per Stef's request — kept clean.) |
| **Sleep** | **Sleepy Bud-E** (half-closed eyes) + "zzz" + the **battery %** (battery lives here now, not on home). |
| **Recording** | **Bud-E listening** — big alert eyes + rec dot |

> **Timezone:** `LOCAL_TIME_OFFSET_MIN` is set to **−300 (Central Daylight, Chicago)**.
> It's a fixed offset (no auto-DST) — switch to `-360` in winter (CST). Re-Sync after
> flashing so the RTC re-reads NTP. The on-device clock isn't shown on a screen right now
> (home is name-only); the offset still drives correct **note timestamps** in the web portal.
> `localClockHHMM()` remains available if we want to put a clock back (e.g. on the sleep screen).

---

## 5b. Timer & Pomodoro (built — Phase 1)

Two new menu entries: **Timer** and **Pomodoro**. While either runs, the device
**stays awake** showing a big **centered countdown** (no eyes mid-countdown — they were
a distraction). To spare the e-paper, the countdown shows **whole minutes (rounded up)**
and only redraws when that number changes; in the **final minute** it switches to
**seconds**, ticking each second. A progress bar fills as time elapses.

**Menu is now:** `Notes · Tags · Timer · Pomodoro · Sync · Settings`

### Timer
- Menu → **Timer** opens a duration picker. **PWR** cycles presets
  (**1\* / 5 / 10 / 15 / 25 / 45 / 60 min**), **REC** starts it. Default is 25 min.
  - \* The **1-min** preset is a **temporary test preset** (marked `// TEMP` in `timer.cpp`) —
    remove it once testing is done.
- While running: **PWR** = pause/resume, **hold REC** (long-press) or **double-tap REC** = cancel.
- When it finishes: a **loud chime** (`soundTimerDone()` — full volume, rings twice) +
  Bud-E's **heart eyes** + "time's up", then back to home.

### Pomodoro
- Menu → **Pomodoro** starts immediately: **4 blocks** of **25 min focus / 5 min break**,
  with a **15 min long break** after the 4th focus block (classic pomodoro).
- The screen header shows the phase + block, e.g. `focus 1/4` → `break 1/4` → `focus 2/4` …
- At each phase change: a **loud chime** + a transition face ("break!" / "focus!" /
  "long break"), then it auto-advances. After the long break it shows "done!" and returns home.
- Full session ≈ **130 min** (4×25 focus + 3×5 short breaks + 1×15 long break).
- **PWR** = pause/resume, **hold/double-tap REC** = cancel the whole session.

**Behaviour notes & honest limits**
- *Battery:* staying awake for a 25-min pomodoro uses real power — best on a desk or
  plugged in (this was the chosen trade-off for a live countdown).
- *Accuracy:* timing uses the awake `millis()` clock; the brief celebration screens are
  shown **before** the next phase starts, so a break/focus isn't shortened by them.
- *Pomodoro length:* classic 4×(25 focus / 5 break) + a 15-min long break after the
  4th block. All durations are `POMO_*` constants in `timer.h`.
- *Voice control* ("start a 10-minute timer") is **Phase 2** — for now it's button-driven.

**Code:** new `src/app/timer.cpp` / `timer.h` (state + logic); new screens in `ui.cpp`
(`showTimerSet`, `showTimerRun`, `showPomoRun`, `showTimerDone`); new states
`STATE_TIMER_SET / STATE_TIMER_RUN / STATE_POMO_RUN` and menu wiring in `pala_note.ino`.
The main loop's auto-sleep is suppressed while a timer runs.

---

## 6. How to use the device today

Two buttons: **REC** (talk / select / play) and **PWR** (next / navigate).

### Talk to Bud-E (primary gesture)
1. Home screen → **press and hold REC**, ask your question, **release**.
2. Bud-E thinks, then **speaks the answer** (and shows the text). Hold REC again to ask more.

### Record a note
1. **Menu (PWR) → Record** → the screen says "hold rec to record".
2. **Hold REC**, speak, **release** (~½ s minimum).
3. **Pick a tag:** tap **PWR** to cycle (Note, Work, Idea, Buy, Private), tap **REC** to confirm.
4. After saving, Bud-E returns to the **home screen** (blinking eyes) — it no longer sleeps
   instantly; it sleeps on the normal ~2-minute idle timeout.

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
| **1** | On-device buddy: **face**, clock, **timer**, **pomodoro** — no Mac needed | ✅ Built (pending hardware test) |
| **2 MVP** | **Push-to-talk** → Mac brain → **Calendar "how's my day"** → spoken reply | ✅ Built (Mac side tested; device side pending flash) |
| **2+** | More integrations (Productive, Gmail, Slack, iMessage), scheduled check-ins, wake word | 📋 Next |
| **3** | Stretch: caller ID, voicemail-aloud | 💭 Maybe |

### Phase 2 MVP — how it works (built 2026-06-25)
From the home screen, **hold REC to talk to Bud-E** (push-to-talk is now the primary
gesture). The device records your question, POSTs the WAV to the Mac brain
(`bud-e-mac/bud_e_server.py`) at `BUDE_HOST:8765/ask`, which runs **Groq STT → your
calendar (icalBuddy) → Claude → macOS `say` TTS** and returns a spoken WAV the device
plays, then returns home. WiFi connects on demand and stays up for fast follow-ups.

> **Gesture model (changed 2026-06-25):** home **hold REC = talk to Bud-E**.
> Recording a **note** moved to **Menu → Record → hold REC**. Menu is now
> `Record · Notes · Tags · Timer · Pomodoro · Sync · Settings`.
- **Mac side** (`bud-e-mac/`): zero-pip Python; calendar via `icalBuddy` (no Google
  OAuth); keys read from `secrets.h`; voice configurable in `config.json`. **Tested
  end-to-end via `--text` mode.**
- **Device side:** `src/app/ask.cpp` (HTTP upload/download), `recordAsk()`, new
  `STATE_ASK` + "Ask" menu item, screens `showAskReady/Thinking/Speaking`. Reply WAV
  is repackaged to a clean 44-byte header so the device plays it correctly.
- **Setup:** set `BUDE_HOST` in `config.h` to the Mac's LAN IP (currently
  `192.168.1.130`); run `python3 bud_e_server.py --serve`; device + Mac on same WiFi.

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

**Flash and test on hardware.** Two things to eyeball on the real screen:
1. **Bud-E's eye geometry** (Home / Sleep / Recording) — bigger / rounder / closer?
   One-line constants in `face.cpp` (`EYE_W/H/R/GAP/TOP`).
2. **The new Timer & Pomodoro screens** — countdown readability, mini-eye size, progress
   bar, and the focus→break transitions. Tune in `ui.cpp` (`drawMiniEyes`,
   `drawCountdownBig`) and `timer.h` (`POMO_*`, `TIMER_PRESETS`).

After hardware tuning, Phase 1 is complete and the next build is **Phase 2** (the Mac
backend: voice Q&A + data integrations). A small Phase-1.5 option is **voice-started
timers**, but that depends on the Phase-2 voice pipeline.

---

## 12. File map (quick reference)

```
SJ- Pala Note/
├─ BUD-E_PROJECT.md            ← this document
├─ PALA_NOTE_USER_GUIDE.md     ← plain-language how-to-use manual
├─ START HERE.pdf              ← creator's pointer to the Craft assembly guide
├─ RoboEyes-main/              ← FluxGarage RoboEyes reference repo (style source; not compiled in)
├─ bud-e-mac/                  ← Phase 2 Mac brain  [NEW]
│  ├─ bud_e_server.py          ← zero-pip service: STT→calendar→Claude→say TTS; --serve / --text / --audio
│  ├─ config.json              ← voice, port, models (keys fall back to secrets.h)
│  └─ README.md
└─ firmware 1.0/pala_note/
   ├─ pala_note.ino            ← main sketch (state machine, idle blink, timer + ask handlers)
   ├─ secrets.h                ← GROQ_KEY, ANTHROPIC_KEY, WiFi (keep private)
   ├─ config.h                 ← cloud macros, BUDE_HOST, timezone, IDLE_BLINK_MS, v1.1
   ├─ sounds.h                 ← UI tones incl. soundTimerDone() (loud completion chime)
   ├─ globals.h / types.h
   └─ src/app/
      ├─ network.cpp/.h        ← Groq transcription + Claude enrichment + web portal
      ├─ notes.cpp/.h          ← note storage incl. .ai sidecar
      ├─ face.cpp/.h           ← Bud-E's eyes — RoboEyes-style, 9 moods + blink  [NEW]
      ├─ timer.cpp/.h          ← Timer + Pomodoro state & logic    [NEW]
      ├─ ask.cpp/.h            ← Phase 2 push-to-talk: upload WAV to Mac, play reply  [NEW]
      ├─ ui.cpp/.h             ← screens (home/sleep/recording + timer/pomodoro + ask)
      ├─ draw.cpp/.h           ← low-level e-paper framebuffer renderer
      ├─ buttons.cpp, record.cpp, rtc.h, ...
```
