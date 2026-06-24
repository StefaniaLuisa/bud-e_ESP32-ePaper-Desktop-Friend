# Pala Note — User Guide

A pocket voice-note recorder. You **record** notes with your voice, then **Sync** to turn
them into clean, organised text (title, summary, tidied transcript, tag, and to-dos) using
Groq (speech-to-text) and Claude (the smart stuff).

---

## The two buttons

The device has just two buttons:

- **REC** — record, select, open, play, confirm
- **PWR** — move/scroll to the next item, open the menu

How you press them matters:

| Press | What it means |
|-------|----------------|
| **Tap REC** | Select / open / play / confirm |
| **Hold REC** | Record (only from the home screen) |
| **Double-tap or long-press REC** | Go back |
| **Tap PWR** | Next item / scroll / open the menu |

---

## 1. Record a note

1. Start on the **home screen**.
2. **Press and hold REC.** After about ⅓ second it begins recording.
3. **Keep holding while you talk.**
4. **Let go to stop** (record at least ½ second). You'll hear a "saved" chime.

> Too short (under ½ second) and the recording is discarded.

## 2. Pick a tag

Right after recording, the device asks for a tag:

- **Tap PWR** to cycle through tags: **Note, Work, Idea, Buy, Private**
- **Tap REC** to confirm the one you want

Tip: leave it on **Note** if you want Claude to auto-pick the best tag for you at Sync time.
If you choose a specific tag, Claude won't override your choice.

## 3. The big logo = sleeping

After you confirm the tag, the **full-screen Pala Note logo** appears. This is the **sleep
screen** — your note is saved and the device powers down to save battery. The e-ink screen
keeps showing the logo even while it's off. **Press REC to wake it.**

The device also auto-sleeps after **2 minutes** of no activity, on any screen.

---

## 4. Sync — turn recordings into smart notes

Recording only captures **audio**. To get the text and AI features, run Sync (needs WiFi):

1. From home, **tap PWR** to open the **Menu**.
2. **Tap PWR** to move to **"Sync"**, then **tap REC** to start.
3. The device connects to WiFi and, for each un-synced note:
   - **Groq** transcribes your voice to text,
   - **Claude** writes a **title, 1-line summary, cleaned-up transcript, tag, and to-dos**,
   - it also sets the clock from the internet.

If it says **"NO WIFI,"** it couldn't join your network — check the WiFi name/password in
`secrets.h` or move closer to the router.

---

## 5. Browse and play your notes

1. **Tap PWR** (from home) to open the **Menu**.
2. Stop on **"Notes"** and **tap REC** to open the list. Newest notes are at the top.
3. In the list:
   - **Tap PWR** = next note
   - **Double-tap PWR** = previous note
   - **Tap REC** = open the note
4. In an open note:
   - **Tap REC** = **play the audio** (tap REC again to stop)
   - **Tap PWR** = next page / next note
   - **Long-press REC** = delete this note (then **tap REC** to confirm, **tap PWR** to cancel)
   - **Double-tap REC** = back to the list

---

## 6. Filter by tag

1. Menu → **"Tags"** (tap REC).
2. **Tap PWR** to pick a tag, **tap REC** to see only notes with that tag.

---

## 7. Read full notes on your phone/computer (Transfer)

The device screen is small, so the best way to read everything is the built-in web page:

1. Menu → **"Settings"** → **"Transfer"** (tap REC).
2. It joins your WiFi and shows an **IP address** (e.g. `192.168.1.42`).
3. On a phone or computer **on the same WiFi**, open that address in a browser.

On that page you can, for every note: see the **title, summary, cleaned text, and to-dos**,
**play the audio**, **filter by tag**, **download** the text or audio (or "Download all TXT"),
**delete** notes, and **add/remove tags**.

> Transfer is **local only** — your notes stay on your home network and never go to a server.
> (Only Sync reaches the internet, to transcribe and enrich.)

To leave Transfer mode: **double-tap or long-press REC**.

---

## 8. Settings

Menu → **"Settings"**, then **tap PWR** to move, **tap REC** to choose:

- **Sounds** — turn the click/chime sounds on or off
- **Transfer** — start the web page (see section 7)
- **Device** — firmware version and device info

---

## 9. Battery & charging

- Charges over **USB-C**.
- A **low-battery warning** pops up at about 15% (clears once it's charged past ~20%).
- The home screen shows a small battery ring indicator.

---

## Cheat sheet

| I want to… | Do this |
|------------|---------|
| Record | Home screen → **hold REC**, release to stop |
| Choose tag | **PWR** to cycle, **REC** to confirm |
| Wake from sleep | Press **REC** |
| Open the menu | **PWR** from home |
| Move / scroll | **PWR** |
| Select / open / play | **REC** |
| Go back | **Double-tap or long-press REC** |
| Make notes smart (transcribe) | Menu → **Sync** |
| Read full notes | Menu → Settings → **Transfer**, open the IP in a browser |
| Delete a note | Open it → **long-press REC** → **REC** to confirm |

---

## Troubleshooting

- **Recorded but no text appears** — you haven't **Synced** yet. Recording only saves audio;
  Sync adds the text and AI fields.
- **Playback is silent** — recording may have been too short, or check the speaker connection.
- **"NO WIFI"** — wrong WiFi name/password in `secrets.h`, or out of range.
- **Sync fails / no smart fields** — check your Groq/Anthropic keys in `secrets.h`; if Claude
  fails, you still keep the raw transcript.
- **Screen looks frozen on the logo** — that's the sleep screen. Press **REC** to wake.
- **Notes look faint/ghosted over time** — a known e-ink quirk on this firmware (continuous
  partial refreshes); a fix is planned.
