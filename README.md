# Bud-E 🤖 — an e-paper desk companion

**Bud-E is a small, kind, always-on desk buddy you talk to out loud.** Hold a button and ask him about your day; he answers in his own voice, reacts with an expressive e-paper face, and proactively speaks up before your meetings. He's a tangible little character — not another app behind glass.

Built on a Waveshare **ESP32-S3 1.54" e-paper** board, with a headless **macOS "brain"** that does the speech, reasoning, and knowledge.

> Started life as a purchased pocket voice-recorder firmware ("Pala Note") and is being rebuilt into a friendly companion.

---

## What he does

- 🎙️ **Talk to him** — hold REC, ask *"how's my day?"*; he transcribes, reasons over your real calendar/weather/tasks, and **speaks the answer aloud**.
- 📝 **Voice notes** — capture, auto-title/summarize/extract to-dos, and sync to a local dashboard.
- ⏱️ **Timer & Pomodoro** — on-device, with a chime and a happy face when done.
- 🔔 **Proactive nudges** — in "desk mode" (plugged in), he stays awake and **speaks a heads-up ~15 min before a meeting**, and again as it starts.
- 🌅 **Briefings** — a spoken morning briefing (8 AM) and end-of-day wrap-up (5:30 PM).
- 😊 **A face with moods** — awake, blinking, listening, thinking, happy, sleepy, annoyed.
- 🖥️ **Web dashboard** — browse notes + conversation history, pick his voice and brain model, tune settings — all served locally on your LAN.

See the full **[Product & UX Brief](SJ-%20Pala%20Note/BUD-E_PRODUCT_BRIEF.md)** for the deep dive.

---

## How it works

```
┌─────────────────────────────┐      WiFi (LAN, plain HTTP)       ┌──────────────────────────────┐
│   THE DEVICE  (Bud-E)        │ ────────────────────────────────►│   THE MAC BRAIN  (headless)   │
│   ESP32-S3 + e-paper face    │   • uploads recorded audio        │   Python service, port 8765    │
│   mic · speaker · 2 buttons  │   • uploads notes on Sync         │   • Groq Whisper → speech→text │
│   the whole user interface   │   • polls for nudges              │   • Claude       → reasoning   │
│                              │◄──────────────────────────────── │   • edge-tts     → text→voice  │
│                              │   • spoken reply (WAV) + text     │   • Calendar / Weather / Tasks │
└─────────────────────────────┘                                   │   • Web dashboard (home base)  │
                                                                   └──────────────────────────────┘
```

- **The device is the entire interface** — the thing you talk to *and* that talks/expresses back.
- **The Mac is an invisible backend** — speech-to-text (Groq Whisper), reasoning (Claude), voice (edge-tts), knowledge (your Calendar via `icalBuddy`, weather via wttr.in, tasks via Productive.io), storage, and the dashboard.
- **Local-first & private** — runs on your own Mac over your home WiFi; notes/conversations live on your hardware, and secrets never leave it.

---

## Repository layout

| Path | What it is |
|---|---|
| `SJ- Pala Note/firmware 1.0/pala_note/` | The **device firmware** (Arduino / ESP32-S3) |
| `SJ- Pala Note/bud-e-mac/` | The **macOS brain** (`bud_e_server.py`) + web dashboard |
| `SJ- Pala Note/BUD-E_PRODUCT_BRIEF.md` | Full product & UX specification |
| `SJ- Pala Note/FLASH_AND_QA.md` | Flashing + QA checklist |
| `01_Arduino_Libraries/`, `02_Example/`, `03_Firmware/` | **Upstream Waveshare** sample code (see licensing) |

---

## Setup

### 1. The device (firmware)
1. Open `SJ- Pala Note/firmware 1.0/pala_note/pala_note.ino` in the Arduino IDE (ESP32-S3 board support installed).
2. Copy `secrets.h.example` → `secrets.h` and fill in your WiFi + API keys (this file is git-ignored).
3. Set `BUDE_HOST` in `config.h` to your Mac's LAN IP.
4. Flash. (Step-by-step: [FLASH_AND_QA.md](SJ-%20Pala%20Note/FLASH_AND_QA.md).)

### 2. The Mac brain
```bash
cd "SJ- Pala Note/bud-e-mac"

# Optional: copy the config template (keys can be left blank to read from the firmware's secrets.h)
cp config.example.json config.json

# One free dependency for neural voices:
pip3 install edge-tts

# Try it with no device — uses your real calendar:
python3 bud_e_server.py --text "how's my day looking?" --play

# Run the always-on server the device talks to:
python3 bud_e_server.py --serve
```
You'll need free API keys from **[Groq](https://console.groq.com)** (speech-to-text) and **[Anthropic](https://console.anthropic.com)** (Claude). The calendar uses macOS Calendar.app via `icalBuddy` — no Google OAuth.

More detail: [`bud-e-mac/README.md`](SJ-%20Pala%20Note/bud-e-mac/README.md).

---

## Status

Actively developed. Working today: push-to-talk Q&A, voice notes + dashboard sync, timer/pomodoro, the expressive face, morning/evening briefings, and desk-mode device nudges (firmware **v1.5**). On the roadmap: a navigation UX redesign, mDNS device→Mac discovery, restore-to-device for archived notes, and more knowledge sources (email, Slack). See the brief's roadmap.

---

## Licensing

This repository contains two bodies of work:

- **Bud-E — original work** (everything under `SJ- Pala Note/`): © 2026 Stef Jones, released under the **[MIT License](LICENSE)**.
- **Upstream Waveshare sample code** (`01_Arduino_Libraries/`, `02_Example/`, `03_Firmware/`, `README_waveshare.md`, `Tools Configuration.png`): forked from [waveshareteam/ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54) and remains the property of **Waveshare** under their original terms.

The on-device face technique is adapted from [FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes) (GPL); that library is referenced, not vendored here.

---

## Acknowledgements

- **Waveshare** for the ESP32-S3 e-paper hardware and base firmware.
- **FluxGarage** for the RoboEyes eye-rendering inspiration.
- **Groq**, **Anthropic**, and **edge-tts** for the brain and voice.
