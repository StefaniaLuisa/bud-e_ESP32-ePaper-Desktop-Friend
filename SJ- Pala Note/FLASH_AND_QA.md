# Bud-E — Flash & QA Guide

Use this when you flash a new firmware build. It covers the upload steps, a full
**QA checklist**, and how to enable/use every feature. Last updated 2026-06-28
(**firmware v1.5 — Desk mode + device-side nudges**).

---

## Part 1 — Before you flash

1. **Mac brain is running.** It's now always-on (launchd), so it should already be up.
   Confirm: in a browser, `http://localhost:8765/health` → `bud-e ok`.
   - If not: `launchctl load -w ~/Library/LaunchAgents/com.bud-e.brain.plist`
2. **Same WiFi.** Device and Mac both on `WinstonPalace`.
3. **`BUDE_HOST` matches the Mac IP** (`config.h`). Current: `192.168.1.130`.
   If your Mac's IP changed, update it before flashing.

---

## Part 2 — Flash the firmware

1. Open **Arduino IDE** → the sketch `…/SJ- Pala Note/firmware 1.0/pala_note/pala_note.ino`.
2. **Put the board in download mode** (the reliable way):
   - **Unplug** the USB-C cable.
   - **Press and hold REC.**
   - While holding REC, **plug USB back in**, keep holding ~2 s, then release.
   - Blank/frozen screen = correct (it's in the bootloader).
3. **Tools → Port** → select the `usbmodem` port.
4. Click **Upload (→)**. Wait for **"Done uploading."** *(Compile error? Copy it to Claude.)*
5. **Unplug/replug** once to boot. You should see Bud-E's blinking eyes + "bud-e".
6. **Confirm the version:** Menu (PWR) → **Settings → Device** → should read **v1.5**.

---

## Part 3 — QA checklist

Tick each. Expected result in italics.

### A. Device basics
- [ ] Boots to **home** = eyes + "bud-e", and **blinks** every few seconds.
- [ ] **PWR** opens the Menu; the 7 items fit: `Record · Notes · Tags · Timer · Pomodoro · Sync · Settings`.
- [ ] **Settings** now has **4 items**: `Sounds · Desk Mode · Transfer · Device` (all fit on screen).
- [ ] Idle ~2 min → **sleep screen** = sleepy eyes + "zzz" + **battery %**. *(A brief "tired" face shows first. Desk mode OFF for this test.)*

### B. Talk to Bud-E (push-to-talk)
- [ ] From home, **hold REC**, ask *"how's my day?"*, release.
- [ ] Screen shows **listening → thinking →** Bud-E **speaks the answer** (and shows the text), then back home.
- [ ] Answer reflects your **calendar, tasks, and weather**. *(First ask shows a brief "connecting" while WiFi joins; later asks are quick.)*
- [ ] **Mac brain logs it:** the dashboard **Conversations** tab shows the exchange.

### C. Voice notes + dashboard sync ⭐ (new)
- [ ] **Menu → Record** → "hold rec to record" → hold REC, speak, release → pick a tag (PWR cycles, REC confirms).
- [ ] After saving, it returns to the **home screen** (does *not* sleep instantly).
- [ ] **Menu → Sync** → connects WiFi, transcribes + enriches, then uploads.
- [ ] **Dashboard → Notes tab:** the note appears with **title, summary, to-dos, tag, and ▶ play audio**.
- [ ] Note shows badge **"on device + dashboard."**

### D. Retention ⭐ (new)
- [ ] In the dashboard, set a note's dropdown to **"Move to dashboard only."** Badge → *"moves to dashboard next sync."*
- [ ] On the device, **Menu → Sync** again.
- [ ] Back in the dashboard, that note now shows **"dashboard only"** (the device dropped its local copy).

### E. Timer & Pomodoro
- [ ] **Menu → Timer** → PWR to **1 min** (temp test preset) → REC starts. Countdown + progress bar.
- [ ] **PWR** pauses ("paused"), **PWR** resumes; **hold REC** cancels.
- [ ] Let it hit 0 → **loud chime + heart eyes + "time's up"** → home.
- [ ] **Menu → Pomodoro** → header `focus 1/4`, counting down. *(No need to sit through it.)*

### F. The face / moods
- [ ] **Errors look annoyed:** turn off WiFi/unplug the Mac server and ask → "NO REPLY" with **angry eyes**.

### G. Mac brain & dashboard
- [ ] `http://192.168.1.130:8765/` opens the dashboard from your **phone** (same WiFi).
- [ ] **Settings:** change **Voice**, hit **▶ preview**, **Save**. *(New replies use it.)*
- [ ] **Conversations → 🌅 Briefing** button → speaks today's briefing; **🌙 Wrap-up** speaks the end-of-day one.
- [ ] **Settings → "Knows about"** lists `calendar, weather, tasks`.

### H. Desk mode + device nudges ⭐ (new in v1.5)
- [ ] **Plug Bud-E into USB power** (desk mode keeps WiFi on — it needs power).
- [ ] **Menu → Settings → Desk Mode → REC** → screen shows **"desk mode on / staying awake for you."**
- [ ] Leave it on the home screen >2 min → it **does NOT sleep** (blinks slowly, stays awake).
- [ ] Dashboard **Settings tab** → within ~30 s the dot shows **"● Bud-E online."**
- [ ] Click the **🔔 test** button → within ~30 s Bud-E **chimes + speaks** the test line, shows "heads up", returns to blinking.
- [ ] *(Optional real nudge)* Add a calendar event ~16 min out → Bud-E speaks a heads-up as it approaches, and again as it starts.
- [ ] **Turn Desk Mode off** (Settings → Desk Mode → REC) → "desk mode off"; normal ~2-min sleep resumes.
- [ ] Desk mode **survives a reboot** (unplug/replug → still on if you left it on).

---

## Part 4 — Enabling / configuring features

Most are already on. Reference:

| Feature | How it's enabled |
|---|---|
| **Talk / ask** | Built in — hold REC. Needs the Mac brain running + same WiFi. |
| **Calendar** | macOS Calendar.app (your Google cal is synced) — no setup. |
| **Weather** | On by default. Change the city in **Settings → Weather location**. |
| **Tasks (Productive)** | Token/org in `config.json` (private). Already configured. |
| **Voice** | **Settings → Voice** (preview + save). |
| **Morning briefing** | Auto at 8:00 AM. Retime: edit `Hour`/`Minute` in `~/Library/LaunchAgents/com.bud-e.briefing.plist`, then reload. |
| **Notes archive** | Automatic on **Sync**. |
| **Auto-migrate window** | **Settings → auto-move … days** (default 30). |
| **Always-on brain** | launchd agents `com.bud-e.brain` (server) + `com.bud-e.briefing` (8 AM). |

---

## Part 5 — Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Upload "failed to connect" | Redo download mode (hold REC *before* replug, release *after*). |
| **"NO WIFI"** on ask/sync | Device can't reach `WinstonPalace`. |
| **"NO REPLY"** on ask | Mac brain not running, or `BUDE_HOST` ≠ Mac IP. Check `…/health`. |
| Note didn't appear in dashboard | Did you **Sync**? Brain running? Check `data/server.log`. |
| Briefing didn't fire | Mac asleep at 8 AM (launchd jobs don't fire while fully asleep). |
| Garbled audio | Tell Claude — the format is verified, but flag it. |
| Desk mode dot stays **offline** | Device not in desk mode, not on `WinstonPalace`, or can't reach the Mac. Confirm "desk mode on" + `…/health`. |
| 🔔 test plays on the **Mac**, not the device | "Nudge plays on" = `auto` and the device wasn't online yet. Wait for the online dot, or set it to **Bud-E device only**. |
| Desk mode drains the battery | Expected — it never sleeps. **Keep it plugged in.** (It force-sleeps below 5% to protect the cell.) |
| Nudges/ask break after a while | Mac's DHCP IP may have changed (still hardcoded `192.168.1.130`). Update `BUDE_HOST` + reflash. *(mDNS fix is a planned follow-up.)* |

> **Known temp item:** the **1-minute timer preset** is for testing — remove it when done
> (`TIMER_PRESETS` in `firmware…/src/app/timer.cpp`).
