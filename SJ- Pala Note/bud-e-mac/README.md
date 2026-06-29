# Bud-E Mac Brain (Phase 2 MVP)

The headless backend that gives Bud-E a voice + a brain. The ESP32 device records
your voice, sends it here over WiFi; this service transcribes it, asks Claude (with
your calendar as context), and sends back spoken audio for the device to play.

```
device WAV ──► POST /ask ──► Groq STT ──► + today's calendar ──► Claude ──► macOS say ──► WAV back
```

- **Zero pip dependencies** — Python standard library only.
- **Calendar:** read from `icalBuddy` (macOS Calendar.app, which already has the
  Google calendar synced) — *no Google OAuth needed.*
- **Keys:** read automatically from the firmware's `secrets.h` (single source of
  truth), or set `groq_key` / `anthropic_key` in `config.json`.

## Try it now — no device required

```bash
cd "bud-e-mac"

# Ask Bud-E something and SEE the reply (uses your real calendar):
python3 bud_e_server.py --text "how's my day looking?"

# ...and HEAR it spoken on this Mac:
python3 bud_e_server.py --text "how's my day looking?" --play

# Audition a voice (try any from --list-voices):
python3 bud_e_server.py --text "Hi, I'm Bud-E, your desk buddy." --voice Evan --play
python3 bud_e_server.py --list-voices
```

## Run the server (for the device + dashboard)

```bash
python3 bud_e_server.py --serve
```
It prints the Mac's LAN IP + port (default `8765`). The device's firmware points at
that IP (`BUDE_HOST`).

### The dashboard
Open **`http://localhost:8765/`** (or `http://<that-IP>:8765/` from your phone on the
same WiFi). It's Bud-E's home base:
- **Notes** — your synced voice notes (title, summary, to-dos, play audio), kept here
  permanently even when the device is asleep.
- **Conversations** — a live log of what you asked and what Bud-E answered.
- **Settings** — current voice/model (editable controls coming next).

### Endpoints

| Method / path | Purpose |
|---|---|
| `GET /`                  | the dashboard web app |
| `POST /ask`  (WAV body)  | full turn → spoken WAV; transcript & reply in `X-Bude-*` headers; logged to history |
| `POST /note` (JSON)      | device uploads a note's metadata during Sync |
| `POST /note/audio?id=N`  | device uploads a note's WAV |
| `GET /api/notes` · `/api/conversations` · `/api/config` | dashboard data (JSON) |
| `GET /api/audio?id=N`    | a note's audio |
| `GET /say?text=...`      | debug: speak arbitrary text → WAV |
| `GET /health`            | liveness check |

Data is stored under **`bud-e-mac/data/`** (`notes_index.json`, `notes/note_NNN.wav`,
`conversations.json`).

## Choose Bud-E's voice

Set `"voice"` in `config.json` to any macOS voice name (see `--list-voices`).
Good picks: **Samantha** (clear), **Evan (Enhanced)** / **Nathan (Enhanced)** (natural),
or a robotic character like **Zarvox** / **Trinoids**. Audition with `--voice X --play`.

## Config (`config.json`)

| key | meaning |
|---|---|
| `voice` | macOS `say` voice name |
| `port` | HTTP port the device connects to |
| `claude_model` | brain model (default `claude-haiku-4-5` — fast + cheap) |
| `groq_model` | STT model (`whisper-large-v3-turbo`) |
| `groq_key` / `anthropic_key` | blank = read from `secrets.h` |
| `calendar_window` | `icalBuddy` selector, e.g. `eventsToday` or `eventsToday+1` |

## Status & next steps

- ✅ Brain + calendar + voice loop working (testable via `--text` / `--audio`).
- ⏭ **Device side:** add push-to-talk "ask mode" to the firmware (hold REC →
  record → POST to this server → play the reply through Bud-E's speaker).
- 🔜 Roadmap: more integrations (Productive, Gmail, Slack, iMessage), scheduled
  check-ins, and exploring a wake word.
