# Bud-E — Product & UX Brief

*A designer-facing specification. Covers what Bud-E is, who it's for, how it works, every user interaction, the hard design constraints (especially the e-paper display), known UX pain points, and the roadmap. Firmware v1.5 · last updated 2026-06-28.*

---

## 1. The one-liner

**Bud-E is a small, kind, always-on desk companion — a physical AI buddy you talk to out loud, who answers in his own voice, reacts with an expressive face, and proactively looks out for your day.**

He is *not* an app, a smart speaker clone, or a phone accessory. He's a tangible little character that lives on your desk.

---

## 2. Why Bud-E exists (the vision)

Most "AI assistants" live behind a glass screen and compete for attention with everything else on it. Bud-E is the opposite bet:

- **Ambient, not demanding.** A calm physical object you glance at, not another notification feed. The e-paper face holds an expression with zero power and zero glow.
- **Voice-first and reciprocal.** You hold a button and talk; he *talks back* in a warm voice through his own speaker. No typing, no reading required.
- **Proactive, like a good deskmate.** He knows your calendar, tasks, and weather, and speaks up before a meeting — you don't have to ask.
- **Local-first and private.** The "brain" runs on your own Mac over your home WiFi. Notes and conversations live on your hardware, not a cloud account.
- **A character, not a tool.** He blinks, gets sleepy, perks up, looks annoyed when something fails. The personality is the point — it makes a productivity device feel like a friend.

Bud-E began as a purchased pocket voice-recorder firmware ("Pala Note") and is being rebuilt into this companion.

---

## 3. Who it's for

**Primary user (today): Stef** — a busy Director of Web Strategy juggling heavy project-management load. Wants quick capture (voice notes), spoken day-awareness ("how's my day", "what's left"), and gentle proactive nudges — without opening yet another app.

**Generalized target:** knowledge workers who live in back-to-back meetings and want a low-friction, screen-light, friendly way to capture thoughts and stay oriented through the day.

---

## 4. How it works (system architecture)

Bud-E is **two cooperating pieces**:

```
┌─────────────────────────────┐         WiFi (LAN, plain HTTP)        ┌──────────────────────────────┐
│   THE DEVICE  (Bud-E)        │ ───────────────────────────────────► │   THE MAC BRAIN  (headless)   │
│   ESP32-S3 + e-paper face    │   • uploads recorded audio (/ask)     │   Python service, port 8765    │
│   mic · speaker · 2 buttons  │   • uploads notes on Sync (/note)     │   always-on (launchd)          │
│   the whole user interface   │   • polls for nudges (/api/device/…)  │   • Groq Whisper  → speech→text│
│                              │ ◄─────────────────────────────────── │   • Claude         → reasoning │
│                              │   • spoken reply WAV + text           │   • edge-tts       → text→voice│
└─────────────────────────────┘                                       │   • Calendar/Weather/Tasks     │
                                                                       │   • Web dashboard (home base)  │
                                                                       └──────────────────────────────┘
```

- **The device is the entire interface** — the thing you talk to *and* that talks/expresses back. It has no screen UI beyond its 200×200 face.
- **The Mac is an invisible backend.** It does speech-to-text (Groq Whisper), reasoning (Claude), generates the spoken voice (edge-tts, "Brian"), pulls data (Calendar, weather, Productive.io tasks), stores notes/conversations, and serves a **web dashboard** as Bud-E's "home base" for browsing notes, history, and settings.
- **A typical "ask":** hold REC → device records a WAV → POSTs to the Mac → Mac transcribes → Claude answers using your live context → Mac returns a spoken WAV → device plays it through its speaker and shows the text.
- **Proactive nudges (desk mode):** when plugged in, the device stays awake, holds WiFi, and polls the Mac every ~30 s. The Mac watches the calendar and queues a heads-up; the device chimes, perks up, and speaks it.

---

## 5. Hardware specification

| Component | Spec |
|---|---|
| **MCU** | ESP32-S3 (dual-core, Wi-Fi 2.4 GHz + BLE, PSRAM), native USB-Serial/JTAG |
| **Display** | 1.54" e-paper, **200 × 200 px**, **1-bit monochrome** (black/white only), SPI, partial + full refresh |
| **Audio in** | Microphone → records **16 kHz mono 16-bit WAV** |
| **Audio out** | Speaker via I2S codec (plays the same 16 kHz mono WAV format) |
| **Input** | **Two physical buttons only: REC and PWR** (no touch, no dial) |
| **Storage** | microSD card (notes, audio, index) |
| **Power** | LiPo battery + firmware power-latch; **USB-C charging**; battery-level ADC |
| **Sleep/wake** | ESP32 deep sleep; wakes on button (EXT1). Auto-sleeps after **2 min** idle (suppressed in desk mode) |
| **Connectivity** | 2.4 GHz Wi-Fi (currently a single configured network) |
| **Form factor** | Small desk object; screen is the "face" |

---

## 6. Companion (Mac brain) specification

| Aspect | Detail |
|---|---|
| **Runtime** | Python 3 standard library (zero-pip) + `edge-tts`; always-on via launchd; HTTP on port **8765** |
| **Speech-to-text** | Groq Whisper (`whisper-large-v3-turbo`) |
| **Reasoning** | Anthropic Claude (currently Sonnet 4.6; model is user-selectable) |
| **Text-to-speech** | edge-tts neural voices (current voice: **"Brian," US male**); ~20 US/UK/EU voices selectable |
| **Knowledge sources** | **Calendar** (macOS Calendar.app via `icalBuddy` — no cloud auth), **Weather** (wttr.in), **Tasks** (Productive.io API). Each is toggleable. |
| **Persistence** | Notes archive + conversation log on disk; retention rules (keep both / dashboard-only / 30-day auto-migrate / restore-to-device) |
| **Scheduled** | **8:00 AM morning briefing**, **5:30 PM end-of-day wrap-up** (both spoken) |
| **Dashboard** | Local web app (Notes · Conversations · Features · Settings tabs), reachable on the LAN |

---

## 7. The interaction model

### 7.1 The complete input alphabet
This is the **entire vocabulary** a designer has to work with — two buttons, each with three discrete events, plus one hold gesture:

| Input | Meaning of the raw event |
|---|---|
| **REC — single tap** | Primary action / select / confirm |
| **REC — double tap** (within 200 ms) | Back (in some screens) |
| **REC — long press** (600 ms) | Back / delete / cancel (varies by screen) |
| **REC — hold** (≥350 ms, then keep holding) | **Record / talk** (push-to-talk) |
| **PWR — single tap** | Next item / cycle / open menu (varies) |
| **PWR — double tap** | Previous item (in lists) |
| **PWR — long press** | Open menu (from home) |

> ⚠️ **There are only ~6 distinguishable inputs, and several are overloaded.** REC's single-tap (select) and double-tap (back) share a button, so the system must wait out the 200 ms double-tap window — which, combined with slow e-paper redraw, makes "back" feel laggy. This is the root of the navigation pain (§11).

### 7.2 Personality & expression (the face)
Bud-E communicates state through **9 facial moods** rendered as rounded-rectangle "RoboEyes":
`awake · blink · happy · love · sleepy · tired · listening · thinking · angry`.

- **Home:** awake eyes + "bud-e"; **blinks** every ~5 s (every ~25 s in desk mode) to feel alive.
- **Listening** (recording), **thinking** (working), **happy/love** (timer done, success), **sleepy/tired** (winding down to sleep), **angry** (errors: NO WIFI / NO REPLY / SD ERR).
- Expressions **snap** on events — e-paper can't tween (see §10).

---

## 8. Screen-by-screen interaction map (current v1.5)

> Notation: REC = record button, PWR = power button. "Back" today generally = double-tap or long-press REC.

### Home (idle)
| Do | Result |
|---|---|
| Hold REC, speak, release | **Talk to Bud-E** — he answers aloud |
| Tap PWR (or long-press PWR) | Open the **Menu** |
| ~2 min idle | Sleeps (tired face → sleepy screen + battery %) |

### Menu — `Record · Notes · Tags · Timer · Pomodoro · Sync · Settings`
| Do | Result |
|---|---|
| PWR | Next item |
| REC | Select highlighted item |
| Long-press / double-tap REC | Back to home |

### Notes browser (Menu → Notes)
| Do | Result |
|---|---|
| PWR | Next note |
| Double-tap PWR | Previous note |
| REC | Open the note (detail) |
| Long-press / double-tap REC | Back to menu |

### Note detail (open note)
| Do | Result |
|---|---|
| REC | Play audio (REC again = stop) |
| PWR | Next page; past the last page → next note |
| Long-press REC | Delete (REC confirms, PWR cancels) |
| Double-tap REC | Back to the note list |

### Record a note (Menu → Record)
| Do | Result |
|---|---|
| Hold REC, speak, release | Record |
| PWR | Cycle tag (Note / Work / Idea / Buy / Private) |
| REC | Confirm tag → saved → **returns home** |
| Later: Menu → Sync | Transcribe + enrich + upload to dashboard |

### Tags (Menu → Tags)
PWR cycles a tag, REC shows that tag's notes; long/double-tap REC = back.

### Timer (Menu → Timer)
PWR cycles length (1†/5/10/15/25/45/60 min), REC starts; while running PWR = pause/resume, hold REC = cancel; at 0 → loud chime + heart eyes. *(†1-min is a temporary test preset.)*

### Pomodoro (Menu → Pomodoro)
Starts immediately: 4 × (25 focus / 5 break) + 15-min long break. PWR = pause/resume, hold REC = cancel.

### Settings (Menu → Settings) — `Sounds · Desk Mode · Transfer · Device`
| Item | Action |
|---|---|
| Sounds | REC toggles button sounds on/off |
| **Desk Mode** | REC toggles always-on desk mode (stays awake + online for nudges) |
| Transfer | Starts the on-device web page (shows an IP) |
| Device | Shows firmware version + info |

### Desk mode (plugged-in, always-on)
Stays awake, holds WiFi, polls the Mac every ~30 s. When a meeting nears: **face perks up + chime + spoken heads-up**, then back to blinking. Auto-sleeps below 5% battery to protect the cell.

### Sleep / wake
| Do | Result |
|---|---|
| Tap REC or PWR | Wake to home |
| Hold PWR while waking | Wake straight into the Menu |
| Hold REC after waking (~⅓ s) | Talk to Bud-E |

---

## 9. Feature catalog (what it does + where it runs)

| Feature | Trigger | Runs on |
|---|---|---|
| **Talk to Bud-E** (push-to-talk Q&A) | Home → hold REC | Device + Mac |
| **Voice notes** (capture, tag, store) | Menu → Record | Device |
| **Note enrichment** (title/summary/to-dos/clean transcript) | Menu → Sync | Mac (Groq + Claude) |
| **Notes archive & sync** | Menu → Sync → dashboard | Mac |
| **Retention** (keep both / dashboard-only / 30-day auto-migrate / restore) | Dashboard + Sync | Mac + Device |
| **Timer** | Menu → Timer | Device |
| **Pomodoro** | Menu → Pomodoro | Device |
| **Morning briefing** | Auto 8:00 AM (spoken) | Mac |
| **End-of-day wrap-up** | Auto 5:30 PM (spoken) | Mac |
| **Proactive meeting nudges** | Automatic in desk mode | Mac (schedules) + Device (speaks) |
| **Knowledge: calendar / weather / tasks** | Woven into every answer | Mac |
| **Web dashboard** (notes, history, settings, features) | Browser on the LAN | Mac |
| **Voice & brain-model pickers** | Dashboard → Settings | Mac |

---

## 10. Design constraints (READ THIS — e-paper is not a normal screen)

A UI/UX designer used to phones must internalize these or designs won't work:

1. **Monochrome, 1-bit.** Black or white pixels only. No grays, no color, no anti-aliased gradients. Hierarchy must come from **shape, weight, size, and whitespace**, not color or shading.
2. **Tiny canvas: 200 × 200 px.** Roughly a 1.54" square. Every element competes for very little room. Text must be large enough to read at desk distance.
3. **Refresh is SLOW.** A full refresh takes ~1–2 s and **flashes black**. Partial refreshes are faster but leave **ghosting** that needs an occasional full refresh to clear. **Animation is effectively impossible** — expressions and screens *snap*, they don't tween. The "blink" is a single closed-eyes frame, not a smooth wink.
4. **Refreshes have a cost.** Frequent redraws cause ghosting and wear the panel, so the design should **minimize how often the screen changes** (e.g., desk-mode blink is slowed to ~25 s).
5. **The image persists with no power.** A face stays on screen even when sleeping/off — "always showing his face" is free; only *changing* it costs anything.
6. **No touch, no pointer.** All navigation is the two-button alphabet in §7.1.
7. **No text input on the device.** Any typing happens on the Mac dashboard.

**Implication:** good Bud-E UX is *glanceable states* + *a handful of unambiguous gestures*, not menus-within-menus.

---

## 11. Known UX pain points (prioritized)

**① Getting "back" and "home" is slow and ambiguous — the #1 complaint.**
- There is **no universal "go home"** gesture. From a note you must back out level by level: note → list → menu → home.
- "Back" is **double-tap or long-press REC** — the *same button* as the primary action (select/play). The system waits out the 200 ms double-tap window before acting, and e-paper redraw adds more delay, so backing out feels sluggish and uncertain.
- Users don't get feedback about *where they are* or *how to leave*.

**② Button roles are inconsistent across screens.** PWR means "next" in lists but "open menu/back" elsewhere; REC means "select," "play," "back," "delete," and "record" depending on context. The same gesture does different things screen to screen.

**③ Overloaded REC creates conflicts.** In Note Detail, REC-single = play audio while REC-double = back — so every "back" pays the double-tap penalty, and a stray second tap can start/stop playback unexpectedly.

**④ Pagination bleeds into list navigation.** In a note, PWR pages through text, then silently rolls into the *next note* — mixing two mental models.

**⑤ Minimal on-screen guidance.** Some screens show footer hints, but they're not consistent, so users memorize gestures rather than reading them.

**⑥ Discoverability of "talk."** "Hold REC on the home screen to talk" is invisible until learned.

---

## 12. Design opportunities / open questions for the designer

Working *within* the two-button alphabet (§7.1), some directions to evaluate:

- **A universal escape gesture.** e.g. reserve **PWR long-press = "go Home" from anywhere**. One reliable way out of any depth. (PWR-long is currently used only on the home screen.)
- **Make button roles consistent and predictable.** e.g. PWR = always "navigate/next," REC = always "select/confirm," and a single dedicated "back" that isn't overloaded with a primary action.
- **Reduce reliance on double-tap** (it's laggy on e-paper) in favor of long-press, which gives instant feedback.
- **Persistent, consistent footer hints** ("REC: open · PWR: next · hold PWR: home") on every screen, since the canvas can afford one small line.
- **Shallow over deep.** Can Notes/Tags/Timer be flatter so there's less to back out of?
- **State legibility.** A small persistent indicator of "where am I" (breadcrumb/title) given there's no color to lean on.
- **Onboarding the invisible gestures** (talk, wake-to-menu) — maybe a first-run hint or a printed card.

> The constraint to respect: **~6 distinguishable inputs total, several already reserved (REC-hold = record/talk).** Any new gesture must fit this budget and survive slow e-paper feedback.

---

## 13. Roadmap / upcoming enhancements

**Near-term (planned):**
- **Navigation redesign** (this brief's motivation) — a consistent, low-friction model for back/home.
- **mDNS for the Mac** — so the device finds the brain by name instead of a hardcoded IP (today, if the Mac's IP changes, talk/nudges break).
- **Restore-to-device** for archived notes (the dashboard→device half of retention).
- **Gmail/email as a knowledge source** ("any important email?") — deferred pending a one-time Google auth.

**Exploratory / later:**
- **Wake word** ("Hey Bud-E") instead of push-to-talk (battery + hardware tradeoffs; needs desk/plugged-in mode).
- More knowledge sources (Slack, iMessage).
- Device-spoken morning briefing / wrap-up (today they speak on the Mac).
- Richer proactive behaviors (end-of-day check-in dialogue, gentle haptics via a vibration motor).
- Remove the temporary 1-minute timer test preset.

---

## 14. Quick reference (cheat-sheet)

- **Talk:** Home → hold REC, speak, release.
- **Menu:** PWR. **Select:** REC. **Back (today):** double-tap / long-press REC.
- **Capture a note:** Menu → Record → hold REC.
- **Send notes to the dashboard:** Menu → Sync.
- **Always-on nudges:** plug in → Settings → Desk Mode → on.
- **Everything richer (notes, history, voice, settings):** the Mac dashboard in a browser.
- **The face tells you the state:** awake/blink = idle, listening = recording, thinking = working, happy = success, sleepy = sleeping, angry = error.

---

*Prepared for a UI/UX design engagement. The most valuable single outcome would be a coherent navigation model (§11①, §12) that works within the e-paper + two-button constraints (§7.1, §10).*
