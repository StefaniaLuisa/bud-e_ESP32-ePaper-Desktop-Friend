#!/usr/bin/env python3
"""
Bud-E Mac Brain — the headless backend for the Bud-E desk buddy (Phase 2 MVP).

Pipeline (push-to-talk):
    device records WAV ──► POST /ask ──► Groq STT ──► + today's calendar ──►
    Claude ──► macOS `say` TTS ──► WAV back to the device (plays on its speaker)

Design notes:
  • Zero pip dependencies — Python standard library only.
  • Calendar comes from `icalBuddy` (reads macOS Calendar.app, which already has
    the Google calendar synced) — no Google OAuth needed.
  • API keys are read from config.json, or fall back to the firmware's secrets.h
    so there's a single source of truth.

Usage:
    python3 bud_e_server.py --serve                 # run the HTTP server for the device
    python3 bud_e_server.py --text "how's my day"   # test brain+calendar+voice (no device)
    python3 bud_e_server.py --text "hi" --play      # ...and play the reply on this Mac
    python3 bud_e_server.py --audio clip.wav --play  # full pipeline from an audio file
    python3 bud_e_server.py --list-voices
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import urllib.request
import urllib.error
import html
from datetime import datetime, timezone, date, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, quote

HERE = os.path.dirname(os.path.abspath(__file__))
USER_AGENT = "bud-e/1.0 (macOS)"   # Cloudflare in front of Groq 403s the default Python urllib UA

DEFAULT_CONFIG = {
    "voice": "Samantha",                 # any macOS `say` voice (see --list-voices)
    "port": 8765,
    "claude_model": "claude-haiku-4-5",  # fast + cheap; great for short spoken replies
    "groq_model": "whisper-large-v3-turbo",
    "groq_key": "",                      # blank => read GROQ_KEY from secrets.h
    "anthropic_key": "",                 # blank => read ANTHROPIC_KEY from secrets.h
    "secrets_h": "../firmware 1.0/pala_note/secrets.h",
    "calendar_window": "eventsToday+7",  # icalBuddy selector — today + the next 7 days
    "auto_migrate_days": 30,             # notes older than this auto-move to dashboard-only (Phase B)
    "weather_location": "Chicago",       # for the weather context source
    "sources": {"calendar": True, "weather": True},   # which context sources feed the brain
    "nudges": True,                      # speak a heads-up before calendar events
    "nudge_lead_min": 15,                # how many minutes ahead to nudge
    "nudge_target": "auto",              # auto | device | mac | both — where nudges play
    #   auto   = the device when it's online (desk mode, polling), else the Mac speaks
    #   device = only queue for the device   mac = only speak on the Mac   both = both
    # Productive.io (tasks). Token/org/person live in config.json (private).
    "productive_base": "https://api.productive.io/api/v2",
    "productive_org": "",
    "productive_person_id": "",
    "productive_token": "",
}

CONFIG_PATH = os.path.join(HERE, "config.json")

# Claude models Bud-E can use as his "brain" (pickable in the dashboard).
# $/1M tokens (input/output): Haiku 1/5, Sonnet 3/15, Opus 5/25 — Bud-E's replies
# are short, so even Opus is pennies. Haiku is the fast default.
CLAUDE_MODELS = [
    {"id": "claude-haiku-4-5",  "label": "Haiku 4.5",  "note": "fastest & cheapest (default)"},
    {"id": "claude-sonnet-4-6", "label": "Sonnet 4.6", "note": "balanced — warmer replies"},
    {"id": "claude-sonnet-4-5", "label": "Sonnet 4.5", "note": "previous Sonnet"},
    {"id": "claude-opus-4-8",   "label": "Opus 4.8",   "note": "most capable"},
]
CLAUDE_MODEL_IDS = {m["id"] for m in CLAUDE_MODELS}

# Free high-quality British neural voices via edge-tts (Microsoft Edge, no key).
# These play through edge-tts instead of macOS `say`; everything else is the same.
# All English edge-tts (Microsoft Edge neural) voices, fetched live + cached.
EDGE_ACCENTS = {
    "GB": "British", "US": "American", "AU": "Australian", "CA": "Canadian",
    "IE": "Irish", "NZ": "New Zealand", "ZA": "South African", "IN": "Indian",
    "HK": "Hong Kong", "SG": "Singapore", "PH": "Philippine", "KE": "Kenyan",
    "NG": "Nigerian", "TZ": "Tanzanian",
}
# Which accents to show in the picker. US + UK + EU(=Ireland, the only EU English accent).
KEEP_EDGE_ACCENTS = {"US", "GB", "IE"}
_EDGE_VOICE_CACHE = None


def _is_edge_voice(voice):
    return bool(re.match(r"^[a-z]{2}-[A-Z]{2}-.+Neural$", voice or ""))


def edge_voices():
    """Every English edge-tts voice, with friendly labels (British first)."""
    global _EDGE_VOICE_CACHE
    if _EDGE_VOICE_CACHE is not None:
        return _EDGE_VOICE_CACHE
    rank = {"GB": 0, "US": 1, "AU": 2, "IE": 3, "CA": 4, "NZ": 5, "ZA": 6, "IN": 7}
    try:
        out = subprocess.run(["edge-tts", "--list-voices"], capture_output=True,
                             text=True, timeout=20).stdout
    except Exception:
        out = ""
    rows = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        name, gender = parts[0], parts[1]
        m = re.match(r"^en-([A-Z]{2})-(.+)Neural$", name)
        if not m:
            continue
        cc, base = m.group(1), m.group(2)
        if cc not in KEEP_EDGE_ACCENTS:
            continue
        if "Multilingual" in base:
            continue                      # drop multilingual duplicates of plain voices
        tags = []
        if "Expressive" in base:
            tags.append("expressive"); base = base.replace("Expressive", "")
        g = "male" if gender.lower().startswith("m") else "female"
        extra = (", " + ", ".join(tags)) if tags else ""
        label = f"{base} — {EDGE_ACCENTS.get(cc, cc)} ({g}{extra})"
        rows.append((rank.get(cc, 99), base, {"name": name, "lang": "neural", "label": label}))
    rows.sort(key=lambda r: (r[0], r[1]))
    _EDGE_VOICE_CACHE = [r[2] for r in rows]
    return _EDGE_VOICE_CACHE


# ─────────────────────────────────────────────────────────────────────────────
#  Config & secrets
# ─────────────────────────────────────────────────────────────────────────────
def load_config():
    cfg = dict(DEFAULT_CONFIG)
    path = os.path.join(HERE, "config.json")
    if os.path.exists(path):
        try:
            with open(path) as f:
                cfg.update(json.load(f))
        except Exception as e:
            print(f"[warn] could not read config.json: {e}", file=sys.stderr)
    return cfg


def _read_define(path, name):
    """Pull a #define NAME "value" out of a C header (the firmware secrets.h)."""
    try:
        with open(path) as f:
            for line in f:
                m = re.match(rf'\s*#define\s+{name}\s+"([^"]*)"', line)
                if m:
                    return m.group(1)
    except Exception:
        pass
    return None


def get_keys(cfg):
    groq = cfg.get("groq_key") or ""
    anth = cfg.get("anthropic_key") or ""
    if not groq or not anth:
        secrets = cfg.get("secrets_h", "")
        if secrets and not os.path.isabs(secrets):
            secrets = os.path.normpath(os.path.join(HERE, secrets))
        groq = groq or _read_define(secrets, "GROQ_KEY") or os.environ.get("GROQ_API_KEY", "")
        anth = anth or _read_define(secrets, "ANTHROPIC_KEY") or os.environ.get("ANTHROPIC_API_KEY", "")
    return groq, anth


# ─────────────────────────────────────────────────────────────────────────────
#  Calendar (icalBuddy — no Google OAuth)
# ─────────────────────────────────────────────────────────────────────────────
def calendar_context(window="eventsToday+7"):
    """Today + upcoming days, grouped by day (TODAY / TOMORROW / dates), deduped."""
    try:
        out = subprocess.run(
            ["icalBuddy", "-sd", "-nc", "-b", "", "-iep", "title,datetime",
             "-eep", "notes,attendees,location,url,uid", window],
            capture_output=True, text=True, timeout=20,
        ).stdout
    except FileNotFoundError:
        return "(icalBuddy not installed — calendar unavailable)"
    except Exception as e:
        return f"(calendar unavailable: {e})"

    timere = re.compile(r"\d{1,2}:\d{2}")
    sections = []          # [(day_header, [ "title (time)" ]), ...]
    header = None
    events = []
    seen = set()
    cur_title = None
    for raw in out.splitlines():
        line = raw.rstrip()
        if not line.strip():
            continue
        if set(line.strip()) == {"-"}:        # the "------" divider
            continue
        if line.strip().endswith(":"):        # a day header (today:, tomorrow:, "Jun 30, 2026:")
            if header is not None:
                sections.append((header, events))
            header = line.strip()[:-1].strip()
            events, seen, cur_title = [], set(), None
            continue
        if timere.search(line) and cur_title is not None:
            key = (cur_title.strip(), line.strip())
            if key not in seen:               # same event appears in multiple calendars
                seen.add(key)
                events.append(f"{cur_title.strip()} ({line.strip()})")
            cur_title = None
        else:
            cur_title = line
    if header is not None:
        sections.append((header, events))
    if not sections:
        return "No events scheduled in the next week."
    out_parts = []
    for h, evs in sections:
        body = "\n".join("  - " + e for e in evs) if evs else "  (nothing scheduled)"
        out_parts.append(h.upper() + ":\n" + body)
    return "\n".join(out_parts)


# ─────────────────────────────────────────────────────────────────────────────
#  Weather (wttr.in — no API key)
# ─────────────────────────────────────────────────────────────────────────────
def weather_today(location="Chicago"):
    try:
        url = "https://wttr.in/" + quote(location) + "?format=j1"
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=10) as r:
            d = json.loads(r.read().decode("utf-8"))
        cur = d["current_condition"][0]
        today = d["weather"][0]
        rain = max((int(h["chanceofrain"]) for h in today["hourly"]), default=0)
        desc = cur["weatherDesc"][0]["value"]
        return (f"{location}: now {cur['temp_F']}°F, {desc}. "
                f"Today high {today['maxtempF']}°/low {today['mintempF']}°F, "
                f"max rain chance {rain}%.")
    except Exception:
        return ""


# ─────────────────────────────────────────────────────────────────────────────
#  Tasks (Productive.io — assignee's open tasks, soonest due first)
# ─────────────────────────────────────────────────────────────────────────────
def productive_tasks(cfg):
    token = cfg.get("productive_token")
    org = cfg.get("productive_org")
    pid = cfg.get("productive_person_id")
    base = cfg.get("productive_base", "https://api.productive.io/api/v2")
    if not (token and org and pid):
        return ""
    url = (f"{base}/tasks?filter%5Bassignee_id%5D={pid}&filter%5Bstatus%5D=1"
           f"&sort=due_date&page%5Bsize%5D=20&include=project")
    try:
        req = urllib.request.Request(url, headers={
            "Content-Type": "application/vnd.api+json",
            "X-Auth-Token": token,
            "X-Organization-Id": str(org),
            "User-Agent": USER_AGENT,
        })
        with urllib.request.urlopen(req, timeout=15) as r:
            d = json.loads(r.read().decode("utf-8"))
    except Exception:
        return ""
    projects = {i["id"]: i["attributes"].get("name")
                for i in d.get("included", []) if i["type"] == "projects"}
    today = date.today().isoformat()
    overdue = due_today = 0
    lines = []
    for t in d.get("data", [])[:15]:
        a = t["attributes"]
        rel = t.get("relationships", {})
        due = a.get("due_date")
        proj = projects.get((rel.get("project", {}).get("data") or {}).get("id"), "")
        if due and due < today:
            label, ovr = f"overdue, was due {due}", True
            overdue += 1
        elif due == today:
            label, ovr = "due today", False
            due_today += 1
        else:
            label, ovr = (f"due {due}" if due else "no due date"), False
        title = html.unescape(a.get("title", ""))
        lines.append(f"- {title} [{label}]" + (f" — {proj}" if proj else ""))
    if not lines:
        return "No open tasks assigned to you."
    header = f"{overdue} overdue, {due_today} due today (open tasks assigned to you, soonest first):"
    return header + "\n" + "\n".join(lines)


# ─────────────────────────────────────────────────────────────────────────────
#  Context assembler — gathers the data sources Claude gets each turn.
#  Add a new source here (email, Slack, …) and it flows into every answer.
# ─────────────────────────────────────────────────────────────────────────────
def build_context(cfg):
    sources = cfg.get("sources", {})
    now = datetime.now().astimezone()
    parts = ["[NOW] " + now.strftime("%A, %B %-d, %Y — %-I:%M %p %Z")]
    if sources.get("calendar", True):
        parts.append("[CALENDAR — today + upcoming days]\n"
                     + calendar_context(cfg.get("calendar_window", "eventsToday+7")))
    if sources.get("weather", True):
        w = weather_today(cfg.get("weather_location", "Chicago"))
        if w:
            parts.append("[WEATHER — today]\n" + w)
    if sources.get("tasks"):
        tk = productive_tasks(cfg)
        if tk:
            parts.append("[TASKS — Productive]\n" + tk)
    return "\n\n".join(parts)


# ─────────────────────────────────────────────────────────────────────────────
#  Speech-to-text (Groq, OpenAI-compatible Whisper)
# ─────────────────────────────────────────────────────────────────────────────
def groq_transcribe(wav_bytes, key, model):
    if not key:
        raise RuntimeError("no Groq key (set groq_key in config.json or GROQ_KEY in secrets.h)")
    boundary = "----budeBoundary8a7c2f1e"
    pre = []
    for field, value in (("model", model), ("response_format", "text")):
        pre.append(f"--{boundary}\r\n".encode())
        pre.append(f'Content-Disposition: form-data; name="{field}"\r\n\r\n'.encode())
        pre.append(f"{value}\r\n".encode())
    pre.append(f"--{boundary}\r\n".encode())
    pre.append(b'Content-Disposition: form-data; name="file"; filename="speech.wav"\r\n')
    pre.append(b"Content-Type: audio/wav\r\n\r\n")
    body = b"".join(pre) + wav_bytes + f"\r\n--{boundary}--\r\n".encode()

    req = urllib.request.Request(
        "https://api.groq.com/openai/v1/audio/transcriptions",
        data=body, method="POST",
        headers={
            "Authorization": f"Bearer {key}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "User-Agent": USER_AGENT,   # Groq's Cloudflare 403s the default Python UA
        },
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read().decode("utf-8").strip()


# ─────────────────────────────────────────────────────────────────────────────
#  The brain (Claude)
# ─────────────────────────────────────────────────────────────────────────────
SYSTEM_PROMPT = (
    "You are Bud-E, a warm, upbeat little desk buddy that talks out loud to Stef. "
    "Your replies are SPOKEN aloud by a text-to-speech voice, so: keep it to 1-3 short, "
    "natural sentences; no markdown, no bullet lists, no emoji, no URLs, no times written "
    "like '10:00 AM-11:00 AM' — say them naturally ('ten to eleven'). Be friendly and brief. "
    "Use the context below (calendar, weather, etc.) when it's relevant to the question; "
    "summarize rather than reading every line, mention the count if the day's busy, and weave "
    "in weather naturally when it matters (e.g. rain before an outing). Ignore context that "
    "isn't relevant to what was asked. "
    "IMPORTANT — reason from the CURRENT TIME in the [NOW] line: when asked about the day, the "
    "schedule, 'what's on my plate', or 'what's left', focus on what's still AHEAD from now. "
    "Treat events whose end time is already past as done — don't recite finished meetings as if "
    "they're coming up. If it's afternoon, talk about the afternoon/evening, not the morning. "
    "Only mention earlier events if specifically relevant (e.g. 'you've had a packed morning'). "
    "The CALENDAR is grouped by day (TODAY, TOMORROW, then dates) and covers the week ahead — "
    "answer about the day the question is about: 'today' / 'what's left' → TODAY from now; "
    "'tomorrow' → TOMORROW; 'this week' / 'what's coming up' → a quick sweep across the days. "
    "Default to today unless another day is clearly asked about."
)


def claude_reply(user_text, context_text, key, model):
    if not key:
        raise RuntimeError("no Anthropic key (set anthropic_key in config.json or ANTHROPIC_KEY in secrets.h)")
    user = f"{user_text}\n\n{context_text}" if context_text else user_text
    payload = json.dumps({
        "model": model,
        "max_tokens": 400,
        "system": SYSTEM_PROMPT,
        "messages": [{"role": "user", "content": user}],
    }).encode()

    req = urllib.request.Request(
        "https://api.anthropic.com/v1/messages",
        data=payload, method="POST",
        headers={
            "x-api-key": key,
            "anthropic-version": "2023-06-01",
            "content-type": "application/json",
            "User-Agent": USER_AGENT,
        },
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    parts = [b.get("text", "") for b in data.get("content", []) if b.get("type") == "text"]
    return "".join(parts).strip() or "Sorry, I didn't catch that."


# ─────────────────────────────────────────────────────────────────────────────
#  Text-to-speech (macOS `say` → device-friendly 16 kHz mono WAV)
# ─────────────────────────────────────────────────────────────────────────────
def _edge_to_wav(text, voice):
    """British neural voices via edge-tts → device-friendly 16 kHz mono WAV."""
    import io
    import wave
    tmpdir = tempfile.mkdtemp(prefix="bude_")
    mp3 = os.path.join(tmpdir, "out.mp3")
    wav = os.path.join(tmpdir, "out.wav")
    subprocess.run(["edge-tts", "--voice", voice, "--text", text, "--write-media", mp3],
                   check=True, capture_output=True)
    subprocess.run(["ffmpeg", "-y", "-i", mp3, "-ar", "16000", "-ac", "1",
                    "-f", "wav", "-acodec", "pcm_s16le", wav], check=True, capture_output=True)
    with wave.open(wav, "rb") as w:
        frames = w.readframes(w.getnframes())
    buf = io.BytesIO()
    with wave.open(buf, "wb") as o:
        o.setnchannels(1); o.setsampwidth(2); o.setframerate(16000)
        o.writeframes(frames)
    try:
        os.remove(mp3); os.remove(wav); os.rmdir(tmpdir)
    except OSError:
        pass
    return buf.getvalue()


def say_to_wav(text, voice):
    if _is_edge_voice(voice):
        return _edge_to_wav(text, voice)
    import io
    import wave
    tmpdir = tempfile.mkdtemp(prefix="bude_")
    aiff = os.path.join(tmpdir, "out.aiff")
    wav = os.path.join(tmpdir, "out.wav")
    subprocess.run(["say", "-v", voice, "-o", aiff, text], check=True)
    # 16-bit little-endian PCM, 16 kHz, mono — matches what the device records/plays.
    subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1", aiff, wav],
                   check=True)
    # Re-pack through Python's wave module so the header is EXACTLY 44 bytes.
    # The device's playWavFile does seek(44); afconvert can emit a filler chunk
    # that would push the audio data past byte 44 and play as noise.
    with wave.open(wav, "rb") as w:
        frames = w.readframes(w.getnframes())
    buf = io.BytesIO()
    with wave.open(buf, "wb") as o:
        o.setnchannels(1)
        o.setsampwidth(2)
        o.setframerate(16000)
        o.writeframes(frames)
    try:
        os.remove(aiff); os.remove(wav); os.rmdir(tmpdir)
    except OSError:
        pass
    return buf.getvalue()


# ─────────────────────────────────────────────────────────────────────────────
#  Note archive + conversation log (the dashboard's data) — persisted to disk
#  under bud-e-mac/data/. The Mac is the permanent home base for notes.
# ─────────────────────────────────────────────────────────────────────────────
DATA_DIR        = os.path.join(HERE, "data")
NOTES_AUDIO_DIR = os.path.join(DATA_DIR, "notes")
NOTES_INDEX     = os.path.join(DATA_DIR, "notes_index.json")
CONV_LOG        = os.path.join(DATA_DIR, "conversations.json")


def _now_iso():
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def _load_json(path, default):
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return default


def _save_json(path, data):
    os.makedirs(NOTES_AUDIO_DIR, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2)
    os.replace(tmp, path)


def load_notes():
    return _load_json(NOTES_INDEX, [])


def save_note(meta):
    """Insert/update a note from the device. Retention-ready schema."""
    nid = int(meta.get("id", 0))
    todos = meta.get("todos", [])
    if isinstance(todos, str):
        # device sends todos pipe-delimited (per the Claude prompt); also tolerate newlines
        todos = [t.strip() for t in re.split(r"[|\n]", todos) if t.strip()]
    record = {
        "id": nid,
        "title": meta.get("title") or f"Voice note {nid}",
        "summary": meta.get("summary", ""),
        "tag": meta.get("tag", ""),
        "todos": todos,
        "transcript": meta.get("clean") or meta.get("transcript", ""),
        "raw_transcript": meta.get("transcript", ""),
        "created": meta.get("created", "") or _now_iso(),
        "synced_at": _now_iso(),
        "audio": f"note_{nid:03d}.wav",
        "on_device": True,          # still on the device's SD card?
        "retention": "both",        # both | dashboard_only (Stef's per-note choice)
    }
    notes = load_notes()
    for i, n in enumerate(notes):
        if n.get("id") == nid:
            # preserve fields the dashboard/retention may have changed
            record["on_device"] = n.get("on_device", True)
            record["retention"] = n.get("retention", "both")
            notes[i] = {**n, **record}
            _save_json(NOTES_INDEX, notes)
            return notes[i]
    notes.append(record)
    notes.sort(key=lambda n: n.get("created", ""), reverse=True)
    _save_json(NOTES_INDEX, notes)
    return record


def note_audio_path(nid):
    return os.path.join(NOTES_AUDIO_DIR, f"note_{int(nid):03d}.wav")


def append_conversation(heard, reply):
    conv = _load_json(CONV_LOG, [])
    conv.append({"ts": _now_iso(), "heard": heard, "reply": reply})
    _save_json(CONV_LOG, conv[-500:])   # keep the last 500 turns


def load_conversations():
    return _load_json(CONV_LOG, [])


def set_note_retention(nid, retention):
    """Set a note's retention (both | dashboard_only). The device honors the
    'on_device' side on its next Sync; here we just record the intent."""
    notes = load_notes()
    for n in notes:
        if n.get("id") == int(nid):
            n["retention"] = retention
            if retention == "dashboard_only":
                n["pending_device_delete"] = True   # device removes its copy next Sync
            else:
                n.pop("pending_device_delete", None)
                n["pending_device_restore"] = not n.get("on_device", True)
            _save_json(NOTES_INDEX, notes)
            return n
    return None


def get_note(nid):
    for n in load_notes():
        if n.get("id") == int(nid):
            return n
    return None


def device_plan(cfg):
    """During Sync, tell the device which local notes to drop or restore.
    Drop = retention 'dashboard_only', a pending delete, or older than the
    auto-migrate window. Restore = a pending restore-to-device request."""
    notes = load_notes()
    days = int(cfg.get("auto_migrate_days", 30) or 30)
    cutoff = (date.today() - timedelta(days=days)).isoformat()
    delete_ids, restore_ids = [], []
    for n in notes:
        if n.get("on_device", True):
            created = (n.get("created") or "")[:10]
            too_old = bool(created) and created < cutoff
            if n.get("retention") == "dashboard_only" or n.get("pending_device_delete") or too_old:
                delete_ids.append(n["id"])
        elif n.get("pending_device_restore"):
            restore_ids.append(n["id"])
    return {"delete": delete_ids, "restore": restore_ids}


def device_applied(deleted, restored):
    """The device confirms what it did; update on-device state accordingly."""
    notes = load_notes()
    dset, rset = set(deleted or []), set(restored or [])
    for n in notes:
        if n["id"] in dset:
            n["on_device"] = False
            n["retention"] = "dashboard_only"   # auto-migrate also lands here
            n.pop("pending_device_delete", None)
        if n["id"] in rset:
            n["on_device"] = True
            n["retention"] = "both"
            n.pop("pending_device_restore", None)
    _save_json(NOTES_INDEX, notes)
    return {"ok": True, "deleted": len(dset), "restored": len(rset)}


def list_voices(include_macos=False):
    """The edge-tts neural voices (the good ones Bud-E uses). The older macOS
    `say` voices are hidden by default to keep the picker tidy; pass
    include_macos=True (GET /api/voices?all=1) to also list them."""
    voices = list(edge_voices())
    if include_macos:
        out = subprocess.run(["say", "-v", "?"], capture_output=True, text=True).stdout
        for line in out.splitlines():
            if "en_" not in line:
                continue
            m = re.match(r"^(.*?)\s{2,}(en_[A-Z]{2})", line)
            if m:
                voices.append({"name": m.group(1).strip(), "lang": m.group(2)})
    return voices


def update_config_file(changes):
    """Merge changes into config.json on disk; returns the new config dict."""
    current = _load_json(CONFIG_PATH, {})
    current.update(changes)
    tmp = CONFIG_PATH + ".tmp"
    with open(tmp, "w") as f:
        json.dump(current, f, indent=2)
    os.replace(tmp, CONFIG_PATH)
    return current


# ─────────────────────────────────────────────────────────────────────────────
#  One full turn: (optional audio) → transcript → reply → spoken WAV
# ─────────────────────────────────────────────────────────────────────────────
def handle_turn(cfg, *, audio_bytes=None, text=None):
    groq_key, anth_key = get_keys(cfg)
    transcript = text
    if transcript is None:
        transcript = groq_transcribe(audio_bytes, groq_key, cfg["groq_model"])
    context = build_context(cfg)
    reply = claude_reply(transcript, context, anth_key, cfg["claude_model"])
    wav = say_to_wav(reply, cfg["voice"])
    return {"transcript": transcript, "reply": reply, "wav": wav}


def morning_briefing(cfg):
    """Generate Bud-E's proactive morning briefing from the full context."""
    _, anth_key = get_keys(cfg)
    context = build_context(cfg)
    prompt = ("Give me my briefing for right now. Greet me warmly by name (Stef) with a greeting "
              "that fits the time of day (good morning / afternoon / evening), then in a few natural "
              "spoken sentences cover what matters from now through the rest of today: what's still "
              "AHEAD on my schedule, anything overdue or due today, and the weather if it's worth "
              "mentioning. Don't recap meetings that already finished. Upbeat, concise, like a friend "
              "catching me up — not a list.")
    reply = claude_reply(prompt, context, anth_key, cfg["claude_model"])
    append_conversation("(morning briefing)", reply)
    return reply


def wrapup_briefing(cfg):
    """Bud-E's end-of-day wind-down: what's left today + a peek at tomorrow."""
    _, anth_key = get_keys(cfg)
    context = build_context(cfg)
    prompt = ("Give me a short end-of-day wrap-up. Greet me by name (Stef) for the evening, then in "
              "a few natural spoken sentences: note anything still left on today's schedule and any "
              "tasks still overdue or due today I should try to close out, then give me a quick "
              "heads-up on tomorrow — the first thing on the calendar and anything worth prepping "
              "for. Warm and brief, like a friend helping me wind down, not a list. If today's "
              "already clear, just say so and look ahead to tomorrow.")
    reply = claude_reply(prompt, context, anth_key, cfg["claude_model"])
    append_conversation("(end-of-day wrap-up)", reply)
    return reply


# ─────────────────────────────────────────────────────────────────────────────
#  Speaking on the Mac + proactive meeting nudges
# ─────────────────────────────────────────────────────────────────────────────
def speak_on_mac(text, cfg):
    """Speak text aloud on the Mac using the CONFIGURED voice (edge-tts or macOS
    `say`). Going through say_to_wav means edge voices (e.g. Brian) sound right —
    `say -v en-US-BrianNeural` silently falls back to the system default."""
    try:
        wav = say_to_wav(text, cfg["voice"])
        tmpdir = tempfile.mkdtemp(prefix="bude_")
        tmp = os.path.join(tmpdir, "speak.wav")
        with open(tmp, "wb") as f:
            f.write(wav)
        subprocess.run(["afplay", tmp])
        try:
            os.remove(tmp); os.rmdir(tmpdir)
        except OSError:
            pass
    except Exception as e:
        print(f"[speak] error: {e}", file=sys.stderr)


def upcoming_events():
    """Today's TIMED events as [(minutes_until_start, start_dt, title)], soonest
    first. All-day / multi-day events (no plain HH:MM start) are skipped."""
    try:
        out = subprocess.run(
            ["icalBuddy", "-nc", "-b", "", "-iep", "title,datetime",
             "-eep", "notes,attendees,location,url,uid",
             "-df", "%Y-%m-%d", "-tf", "%H:%M", "eventsToday"],
            capture_output=True, text=True, timeout=20).stdout
    except Exception:
        return []
    now = datetime.now().astimezone()
    events, seen, cur_title = [], set(), None
    time_re = re.compile(r"^(\d{1,2}):(\d{2})\b")   # a line that STARTS with a time
    for raw in out.splitlines():
        if not raw.strip() or set(raw.strip()) == {"-"}:
            continue
        indented = raw[:1].isspace()
        s = raw.strip()
        m = time_re.match(s)
        if indented and m and cur_title is not None:
            start = now.replace(hour=int(m.group(1)), minute=int(m.group(2)),
                                second=0, microsecond=0)
            key = (cur_title, m.group(0))
            if key not in seen:                     # same event across calendars
                seen.add(key)
                mins = (start - now).total_seconds() / 60.0
                events.append((mins, start, cur_title))
            cur_title = None
        elif not indented:
            cur_title = s
    events.sort()
    return events


# A tiny queue the device drains by polling GET /api/device/nudge. The device
# is the preferred mouthpiece (it's the desk buddy); the Mac speaks as a fallback.
_NUDGE_LOCK = threading.Lock()
_NUDGE_QUEUE = []            # [{"id","text","mood","ts"}]
_NUDGE_SEQ = 0
_LAST_DEVICE_POLL = None     # when the device last polled (None = never)


def enqueue_nudge(text, mood="happy"):
    """Queue a nudge for the device to pick up on its next poll."""
    global _NUDGE_SEQ
    now = datetime.now().astimezone()
    with _NUDGE_LOCK:
        _NUDGE_SEQ += 1
        _NUDGE_QUEUE.append({"id": _NUDGE_SEQ, "text": text, "mood": mood, "ts": now})
        cutoff = now - timedelta(minutes=20)          # drop anything stale, cap the depth
        _NUDGE_QUEUE[:] = [n for n in _NUDGE_QUEUE if n["ts"] > cutoff][-10:]


def pop_nudge():
    with _NUDGE_LOCK:
        return _NUDGE_QUEUE.pop(0) if _NUDGE_QUEUE else None


def mark_device_poll():
    global _LAST_DEVICE_POLL
    _LAST_DEVICE_POLL = datetime.now().astimezone()


def device_online():
    """True if the device has polled recently (≈ desk mode, listening for nudges)."""
    return (_LAST_DEVICE_POLL is not None
            and (datetime.now().astimezone() - _LAST_DEVICE_POLL).total_seconds() < 90)


def deliver_nudge(text, cfg, mood="happy"):
    """Route a due nudge to the device, the Mac, or both per nudge_target."""
    target = cfg.get("nudge_target", "auto")
    if target == "mac":
        speak_on_mac(text, cfg)
    elif target == "device":
        enqueue_nudge(text, mood)
    elif target == "both":
        enqueue_nudge(text, mood); speak_on_mac(text, cfg)
    else:                                              # auto: device if online, else Mac
        if device_online():
            enqueue_nudge(text, mood)
        else:
            speak_on_mac(text, cfg)


def nudge_loop(cfg):
    """Background: once a minute, fire a heads-up as calendar events approach.
    Two tiers per event — a lead-time nudge (~15 min) and a 'starting now' nudge.
    Reads cfg live, so toggling nudges in Settings takes effect within a minute."""
    import time
    nudged, day = {}, None
    while True:
        try:
            today = date.today()
            if today != day:                         # fresh slate each day
                day, nudged = today, {}
            lead = max(1, int(cfg.get("nudge_lead_min", 15) or 15))
            if cfg.get("nudges", True) and cfg.get("sources", {}).get("calendar", True):
                for mins, start, title in upcoming_events():
                    tiers = nudged.setdefault((start.strftime("%H:%M"), title), set())
                    if 0 < mins <= 2 and "now" not in tiers:
                        tiers.update(("now", "lead"))
                        deliver_nudge(f"Heads up Stef — {title} is starting now.", cfg)
                    elif 2 < mins <= lead and "lead" not in tiers:
                        tiers.add("lead")
                        deliver_nudge(f"Heads up Stef — {title} starts in about "
                                      f"{int(round(mins))} minutes.", cfg)
        except Exception as e:
            print(f"[nudge] error: {e}", file=sys.stderr)
        time.sleep(60)


# ─────────────────────────────────────────────────────────────────────────────
#  HTTP server (the device talks to this)
# ─────────────────────────────────────────────────────────────────────────────
def make_handler(cfg):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            print(f"[http] {self.address_string()} {fmt % args}")

        def _send(self, code, body=b"", ctype="text/plain", extra=None):
            if isinstance(body, str):
                body = body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if body:
                self.wfile.write(body)

        def _send_json(self, obj, code=200):
            self._send(code, json.dumps(obj), "application/json")

        # ── GET ────────────────────────────────────────────────────────────
        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            p = u.path

            if p == "/health":
                self._send(200, b"bud-e ok")
            elif p in ("/", "/index.html"):
                self._serve_dashboard()
            elif p == "/api/notes":
                self._send_json(load_notes())
            elif p == "/api/conversations":
                self._send_json(load_conversations())
            elif p == "/api/config":
                self._send_json({
                    "voice": cfg.get("voice"),
                    "claude_model": cfg.get("claude_model"),
                    "port": cfg.get("port"),
                    "auto_migrate_days": cfg.get("auto_migrate_days"),
                    "weather_location": cfg.get("weather_location"),
                    "sources": cfg.get("sources", {}),
                    "nudges": cfg.get("nudges", True),
                    "nudge_lead_min": cfg.get("nudge_lead_min", 15),
                    "nudge_target": cfg.get("nudge_target", "auto"),
                    "device_online": device_online(),
                    "models": CLAUDE_MODELS,
                })
            elif p == "/api/voices":
                self._send_json(list_voices(q.get("all", ["0"])[0] == "1"))
            elif p == "/api/features":
                # Canonical gesture/feature reference (edit features.json to update).
                self._send_json(_load_json(os.path.join(HERE, "features.json"), {}))
            elif p == "/api/briefing":
                try:
                    self._send_json({"reply": morning_briefing(cfg)})
                except Exception as e:
                    self._send(500, f"error: {e}".encode())
            elif p == "/api/wrapup":
                try:
                    self._send_json({"reply": wrapup_briefing(cfg)})
                except Exception as e:
                    self._send(500, f"error: {e}".encode())
            elif p == "/api/device/nudge":
                # The device polls this in desk mode. 204 = nothing pending; else
                # the queued nudge as a spoken WAV + text/mood headers to present.
                mark_device_poll()
                n = pop_nudge()
                if not n:
                    self._send(204)
                else:
                    try:
                        wav = say_to_wav(n["text"], cfg["voice"])
                        print(f'[nudge→device] "{n["text"]}"')
                        self._send(200, wav, "audio/wav", extra={
                            "X-Bude-Reply": _hdr(n["text"]),
                            "X-Bude-Mood": n.get("mood", "happy"),
                        })
                    except Exception as e:
                        self._send(500, f"tts error: {e}".encode())
            elif p == "/api/nudge/test":
                # QA helper: fire a test nudge through the normal routing (device if
                # online, else the Mac speaks). Off-thread so the Mac-speak fallback
                # doesn't block the HTTP response.
                threading.Thread(target=deliver_nudge, args=(
                    "This is a test nudge from Bud-E. If you can hear me, nudges are working.",
                    cfg), daemon=True).start()
                self._send_json({"ok": True, "device_online": device_online(),
                                 "target": cfg.get("nudge_target", "auto")})
            elif p == "/api/audio":
                self._serve_audio(q.get("id", [""])[0])
            elif p == "/api/device/plan":
                self._send_json(device_plan(cfg))
            elif p == "/api/note":
                rec = get_note(q.get("id", ["-1"])[0]) if q.get("id", [""])[0].lstrip("-").isdigit() else None
                self._send_json(rec) if rec else self._send(404, b"no such note")
            elif p == "/say":
                text = q.get("text", ["hello, I am Bud-E"])[0]
                voice = q.get("voice", [cfg["voice"]])[0]   # preview an arbitrary voice
                try:
                    self._send(200, say_to_wav(text, voice), "audio/wav")
                except Exception as e:
                    self._send(500, f"tts error: {e}".encode())
            else:
                self._send(404, b"not found")

        def _serve_dashboard(self):
            path = os.path.join(HERE, "dashboard.html")
            try:
                with open(path, "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            except FileNotFoundError:
                self._send(404, b"dashboard.html missing")

        def _serve_audio(self, nid):
            if not str(nid).isdigit():
                self._send(400, b"bad id"); return
            path = note_audio_path(nid)
            if not os.path.exists(path):
                self._send(404, b"no audio"); return
            with open(path, "rb") as f:
                self._send(200, f.read(), "audio/wav")

        # ── POST ───────────────────────────────────────────────────────────
        def do_POST(self):
            u = urlparse(self.path)
            p = u.path
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else b""

            if p == "/ask":
                self._handle_ask(body)
            elif p == "/note":
                self._handle_note(body)
            elif p == "/note/audio":
                self._handle_note_audio(parse_qs(u.query).get("id", [""])[0], body)
            elif p == "/api/ask-text":
                self._handle_ask_text(body)
            elif p == "/api/settings":
                self._handle_settings(body)
            elif p == "/api/note/retention":
                self._handle_retention(body)
            elif p == "/api/device/applied":
                try:
                    data = json.loads(body or b"{}")
                    self._send_json(device_applied(data.get("deleted", []), data.get("restored", [])))
                except Exception as e:
                    self._send(500, f"error: {e}".encode())
            else:
                self._send(404, b"not found")

        def _handle_ask_text(self, body):
            """Typed chat from the dashboard — same brain, no TTS."""
            try:
                data = json.loads(body or b"{}")
                text = (data.get("text") or "").strip()
                if not text:
                    self._send(400, b"no text"); return
                _, anth_key = get_keys(cfg)
                context = build_context(cfg)
                reply = claude_reply(text, context, anth_key, cfg["claude_model"])
                append_conversation(text, reply)
                print(f'[chat] "{text}" -> "{reply}"')
                self._send_json({"transcript": text, "reply": reply})
            except Exception as e:
                print(f"[chat] error: {e}", file=sys.stderr)
                self._send(500, f"error: {e}".encode())

        def _handle_settings(self, body):
            try:
                data = json.loads(body or b"{}")
                changes = {}
                if "voice" in data:
                    changes["voice"] = str(data["voice"])
                if "auto_migrate_days" in data:
                    changes["auto_migrate_days"] = int(data["auto_migrate_days"])
                if "weather_location" in data:
                    changes["weather_location"] = str(data["weather_location"]).strip() or "Chicago"
                if "claude_model" in data and data["claude_model"] in CLAUDE_MODEL_IDS:
                    changes["claude_model"] = data["claude_model"]
                if "nudges" in data:
                    changes["nudges"] = bool(data["nudges"])
                if "nudge_lead_min" in data:
                    changes["nudge_lead_min"] = max(1, int(data["nudge_lead_min"]))
                if data.get("nudge_target") in ("auto", "device", "mac", "both"):
                    changes["nudge_target"] = data["nudge_target"]
                update_config_file(changes)
                cfg.update(changes)   # apply to the running server immediately (nudge loop reads cfg live)
                print(f"[settings] updated: {changes}")
                self._send_json({"ok": True, "voice": cfg.get("voice"),
                                 "auto_migrate_days": cfg.get("auto_migrate_days"),
                                 "nudges": cfg.get("nudges"),
                                 "nudge_lead_min": cfg.get("nudge_lead_min")})
            except Exception as e:
                self._send(500, f"error: {e}".encode())

        def _handle_retention(self, body):
            try:
                data = json.loads(body or b"{}")
                rec = set_note_retention(data.get("id"), data.get("retention", "both"))
                self._send_json({"ok": rec is not None})
            except Exception as e:
                self._send(500, f"error: {e}".encode())

        def _handle_ask(self, audio):
            if not audio:
                self._send(400, b"no audio"); return
            try:
                result = handle_turn(cfg, audio_bytes=audio)
                print(f'[ask] heard: "{result["transcript"]}"')
                print(f'[ask] reply: "{result["reply"]}"')
                append_conversation(result["transcript"], result["reply"])
                self._send(200, result["wav"], "audio/wav", extra={
                    "X-Bude-Transcript": _hdr(result["transcript"]),
                    "X-Bude-Reply": _hdr(result["reply"]),
                })
            except Exception as e:
                print(f"[ask] error: {e}", file=sys.stderr)
                self._send(500, f"error: {e}".encode())

        def _handle_note(self, body):
            """Device uploads a note's metadata (JSON) during Sync."""
            try:
                meta = json.loads(body.decode("utf-8")) if body else {}
                rec = save_note(meta)
                print(f'[note] stored #{rec["id"]}: "{rec["title"]}"')
                self._send_json({"ok": True, "id": rec["id"]})
            except Exception as e:
                print(f"[note] error: {e}", file=sys.stderr)
                self._send(500, f"error: {e}".encode())

        def _handle_note_audio(self, nid, body):
            if not str(nid).isdigit() or not body:
                self._send(400, b"bad id/audio"); return
            os.makedirs(NOTES_AUDIO_DIR, exist_ok=True)
            with open(note_audio_path(nid), "wb") as f:
                f.write(body)
            print(f"[note] audio for #{nid} ({len(body)} bytes)")
            self._send_json({"ok": True})

    return Handler


def _hdr(s):
    """Make a string safe to put in an HTTP header (ASCII, single line)."""
    return re.sub(r"[\r\n]+", " ", s).encode("ascii", "ignore").decode("ascii")[:480]


def lan_ip():
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


def serve(cfg):
    # Flush prints immediately so the startup banner/logs show even when piped to a file.
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass
    port = int(cfg["port"])
    httpd = ThreadingHTTPServer(("0.0.0.0", port), make_handler(cfg))
    ip = lan_ip()
    if cfg.get("nudges", True):
        import threading
        threading.Thread(target=nudge_loop, args=(cfg,), daemon=True).start()
    print(f"Bud-E brain listening on http://{ip}:{port}")
    print(f"  voice = {cfg['voice']}   model = {cfg['claude_model']}")
    nudge_state = ("on, %d min ahead" % int(cfg.get("nudge_lead_min", 15) or 15)) if cfg.get("nudges", True) else "off"
    print(f"  meeting nudges = {nudge_state}")
    print(f"  point the device's BUDE_HOST at:  {ip}")
    print("  test:  curl http://localhost:%d/health" % port)
    print("Ctrl-C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye 👋")


# ─────────────────────────────────────────────────────────────────────────────
#  CLI
# ─────────────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="Bud-E Mac brain")
    ap.add_argument("--serve", action="store_true", help="run the HTTP server for the device")
    ap.add_argument("--text", help="test: skip STT, ask Bud-E this directly")
    ap.add_argument("--audio", help="test: run the full pipeline on this WAV file")
    ap.add_argument("--play", action="store_true", help="play Bud-E's spoken reply on this Mac")
    ap.add_argument("--voice", help="override the configured voice for this run")
    ap.add_argument("--list-voices", action="store_true", help="list usable English macOS voices")
    ap.add_argument("--briefing", action="store_true", help="generate + speak the morning briefing (used by the scheduled job)")
    ap.add_argument("--wrapup", action="store_true", help="generate + speak the end-of-day wrap-up (used by the scheduled job)")
    ap.add_argument("--port", type=int, help="override the configured port")
    args = ap.parse_args()

    cfg = load_config()
    if args.voice:
        cfg["voice"] = args.voice
    if args.port:
        cfg["port"] = args.port

    if args.list_voices:
        out = subprocess.run(["say", "-v", "?"], capture_output=True, text=True).stdout
        print("\n".join(l for l in out.splitlines() if "en_" in l))
        return

    if args.briefing:
        reply = morning_briefing(cfg)
        print(f"briefing: {reply}")
        speak_on_mac(reply, cfg)   # uses the real configured voice (Brian, via edge-tts)
        return

    if args.wrapup:
        reply = wrapup_briefing(cfg)
        print(f"wrap-up: {reply}")
        speak_on_mac(reply, cfg)
        return

    if args.serve:
        serve(cfg)
        return

    if args.text or args.audio:
        if args.audio:
            with open(args.audio, "rb") as f:
                result = handle_turn(cfg, audio_bytes=f.read())
        else:
            result = handle_turn(cfg, text=args.text)
        print(f'heard:  {result["transcript"]}')
        print(f'bud-e:  {result["reply"]}')
        if args.play:
            tmp = os.path.join(tempfile.mkdtemp(prefix="bude_"), "reply.wav")
            with open(tmp, "wb") as f:
                f.write(result["wav"])
            subprocess.run(["afplay", tmp])
        return

    ap.print_help()


if __name__ == "__main__":
    main()
