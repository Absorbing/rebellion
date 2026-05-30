# v0.0 Results

> Fill this in at the Windows rig as you run each session. This document anchors
> all subsequent planning — v0.1 scope is derived from these results, not from
> the SPEC's assumptions. (SPEC §0)

**Date:** YYYY-MM-DD
**Hardware:** Maschine Studio (PID 0x____)
**OS:** Windows __ (e.g. 11 22H2)
**Mixxx version:** 2.4.x
**Maschine 2 version:** _._._  (for NIHIA service version)
**Rebellion commit:** ________ (this fork, branch claude/bold-babbage-l79mq)

---

## Test 0.0.1 — NIHIA acquires Studio
- [ ] Pass / [ ] Fail
- Pipe name observed: `\\.\pipe\____________`
- Studio serial: `__________`
- Notes:

## Test 0.0.2 — Events surface
(Press one of each; record example numeric IDs from nihia-monitor output.)

| Control | Pass? | Example event line |
|---|---|---|
| PLAY (BTN_DATA) | | |
| Pad 1 soft (PAD_DATA velocity) | | |
| Pad 1 hard (PAD_DATA velocity) | | |
| Pad 1 hold (aftertouch / cpressure changes) | | |
| Knob 1 turn (KNOB_ROTATE delta) | | |
| Knob 1 touch (BTN_DATA) | | |
| Group A (BTN_DATA) | | |
| Jog rotate (KNOB_ROTATE) | | |
| Jog touch (BTN_DATA) | | |
| Jog push (BTN_DATA) | | |

Yellow flags observed (velocity always same value? knob touch missing? jog buggy?):

## Test 0.0.2b — NIHIA monitor tool
- [ ] Built (`nihia_monitor.exe`)
- [ ] Produces clean readable lines for every control category
- Notes:

## Test 0.0.3 — Display protocol probe  ⚠️ HIGHEST RISK
- Outcome: [ ] A (works) / [ ] B (silent failure) / [ ] C (hard failure/crash)
- If A: per-frame `rebellion_rpc` call duration observed: ____ µs (display 0), ____ µs (display 1)
- If B or C: USB capture saved as `v00-maschine-displays.pcapng`
- NIHIA responses/errors logged (paste relevant lines):
  ```
  ```
- Display 0 first-frame byte sequence (if captured): ...
- Display 1 first-frame byte sequence (if captured): ...
- Did `display_probe` send from the callback OK, or did SEND_FROM_CALLBACK need to be 0? :

## Test 0.0.4 — Mixxx file_path
- [ ] Pass / [ ] Fail
- Exact log line (`PROBE> ... file_path returned: ...`):
- If fail: Mixxx version + chosen fallback (track_id via SQLite, artist+title, etc.):

---

## Decision
- Proceed to v0.1 (if 0.0.3 = A): [ ] yes / [ ] no
- Pivot to v0.1a/v0.1b split (if 0.0.3 = B/C): [ ] yes / [ ] no
- Project stop (if 0.0.1 fails): [ ] yes / [ ] no
- Rationale:
