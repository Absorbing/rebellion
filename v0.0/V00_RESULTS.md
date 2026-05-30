# v0.0 Results

> This document anchors all subsequent planning — v0.1 scope is derived from
> these results, not from the SPEC's assumptions. (SPEC §0)

**Date:** 2026-05-30
**Hardware:** Maschine Studio (PID 0x1300)
**OS:** Windows (MSVC 19.51 / VS 2022 toolchain)
**Mixxx version:** (Test 0.0.4 pending)
**Maschine 2 version:** NIHIA via NIHardwareService (older PORT_MAIN protocol)
**Rebellion commit:** fork `absorbing/rebellion`, branch `claude/bold-babbage-l79mq`
**Studio serial:** 26C67082

**Headline:** Tests 0.0.1, 0.0.2, 0.0.2b, 0.0.3 all PASS. Display protocol =
**Scenario A** (MK3 commands drive the Studio's panels) — the highest project
risk resolved in the best-case direction. **Proceed to full-scope v0.1
(screens included).** Only 0.0.4 (Mixxx file_path) remains.

---

## Test 0.0.1 — NIHIA acquires Studio — ✅ PASS
- Pipe name observed: `\\.\pipe\NIHWMainHandler` (older PORT_MAIN protocol)
- Studio serial: `26C67082`
- Served by **NIHardwareService** (NOT NIHostIntegrationAgent / NTKDaemonService,
  which serve the newer MK3-era protocol). If acquire fails with "cannot find
  the file specified" / "handle is set", NIHardwareService is stopped — start it
  (set Startup type = Automatic) and replug the Studio.
- Full Serial-Connect handshake completes; reaches the event loop.

## Test 0.0.2 — Events surface — ✅ PASS
Every control category produces events; unmapped controls still surface with
numeric IDs. Values read from the `MAPPING>` log lines.

| Control | Pass? | Notes (numeric id / observed) |
|---|---|---|
| PLAY (BTN_DATA) | ✅ | btnid 29 |
| Group A (BTN_DATA) | ✅ | btnid 16 |
| Pad hit (PAD_DATA velocity) | ✅ | velocity present per hit |
| Pad hold (aftertouch / pressure) | ✅ | continuous pressure ~10000, → 0 on release |
| Knob turn (KNOB_ROTATE) | ✅ | rotation events fire |
| Knob touch (BTN_DATA) | ✅ | btnid 88 = KNOB1 touch, 89 = KNOB2 touch |

**Yellow flags / findings:**
- **Jog wheel is an unparsed 4-D encoder** (msgid `0x77`): turning it logs
  `TODO: PARSE_KNOB_ROTATE_4D_EVENT`. The SPEC §1.1 assumed the Studio had no
  4-D encoder — it does (the jog). Rebellion's `PARSE_KNOB_ROTATE_4D_EVENT` is
  a stub. **v0.1 task: implement it** to recover jog rotation/touch/push.
- `0x3444e00` / `0x3434e00` after power on/off are unmapped device power-state
  messages (expected per SPEC §1.5), harmless.

## Test 0.0.2b — NIHIA monitor tool — ✅ BUILT
- `nihia_monitor.exe` built and committed to `v0.0/tools/nihia-monitor/`.
- Prints typed events; raw `data` object appended per line so ids/velocity/
  pressure are always visible. Supports `--filter`, `--csv`, `--raw`.
- librebellion emits heavy `C>`/`L>` debug spam; filter our lines with
  `nihia_monitor.exe | findstr /B "["`.

## Test 0.0.3 — Display protocol probe — ✅ PASS, Scenario A
- Outcome: **A — MK3 display protocol generalises to the Studio.**
- Both 480×272 panels render the black/white stripe pattern.
- Per-frame `rebellion_rpc` (full 130560 px / 261168-byte frame): **~35–40 ms**
  (e.g. 34847 µs, 39118 µs). NOTE for §4 latency budget: this is far above the
  16 ms/60Hz target — full-frame sends over this path are ~25–30 fps at best, so
  **diff-region updates (SPEC §4.4) are mandatory**, not optional, for smooth
  waveforms. Run the dedicated latency-probe in v0.1 to confirm diff-region timing.
- **Bug found & fixed (display 1 was blank):** `_display_cmd_end` hardcoded its
  3rd byte to `0x00`, so every frame's commit targeted display 0 regardless of
  the header's display index. The device's own DSD setup data shows the
  per-display command uses the index in BOTH header (`0x84 0x00 0xNN 0x60`) and
  end/commit (`0x40 0x00 0xNN 0x00`). Fixed `_display_cmd_end` + call site to
  pass the display index. After the fix, **both displays render.** (Pure Lua fix
  in `scripts/niinstance_methods.lua` — no rebuild needed.)
- Probe gotchas learned (encoded in `v0.0/tools/display-probe/main.cpp`):
  - Must NOT send on the `device.state ON` event — the per-serial instance
    isn't created until the later serial-connect handshake (~2s). Send → `no
    instance found`. Probe now waits `INSTANCE_WARMUP_MS` (4s) before sending.
  - `--sweep` sending to display indices 2/3 (which don't exist) **crashes the
    pipe** (`writefile failed error 232, the pipe is being closed`). Studio has
    only displays 0 and 1. Use plain mode.
  - A crashed/non-exiting probe can strand `\\.\pipe\NIHWMainHandler`; the probe
    now calls `rebellion(nullptr)` to release the device and exit. If stranded:
    kill stray `display_probe.exe`, restart NIHardwareService, replug Studio.

## Test 0.0.4 — Mixxx file_path — ⏳ PENDING
- Not yet run. Use `v0.0/mixxx-probe/probe.{midi.xml,js}` (no build needed):
  copy to `%APPDATA%\Mixxx\controllers\`, enable in Preferences → Controllers,
  load a track on deck 1, check Help → Logs for `PROBE> ... file_path returned`.

---

## Build notes (Rebellion on Windows — was effectively unbuilt upstream)
Fixes required to build/run on Windows (all on the branch):
1. Static libs (`liblua`, `librebellion`) shared the SHARED libs' output name →
   Ninja "multiple rules generate lib/liblua.lib". Renamed static outputs.
2. `niproto.hpp` included `<pthread.h>` (no MSVC equivalent). Replaced the one
   mutex with `std::mutex`.
3. Go `rebelliond` (needs Go+TDM-GCC, broken on Windows) gated behind
   `REBELLION_BUILD_REBELLIOND=OFF`.
4. Binaries must run from the repo root (relative `scripts/` path) with
   `rebellion.dll` beside the exe. See `v0.0/BUILD-WINDOWS.md`.

---

## Decision
- **Proceed to v0.1 (0.0.3 = A): YES.** Full scope, displays included.
- Pivot to v0.1a/v0.1b split: no (not needed — Scenario A).
- Project stop: no.

**Rationale:** Acquire, full control/LED I/O, and BOTH displays all confirmed
working on real hardware. Highest risk (display protocol) resolved best-case.

**Carry-forward tasks for v0.1:**
1. Implement `PARSE_KNOB_ROTATE_4D_EVENT` for the jog wheel (rotate/touch/push).
2. Full control-surface mapping enumeration (SPEC §1.1) using nihia_monitor —
   known ids so far: PLAY=29, GROUP_A=16, KNOB1 touch=88, KNOB2 touch=89.
3. Diff-region display updates are mandatory (full frame ≈ 35–40 ms); build the
   latency-probe to characterise diff-region timing.
4. Run Test 0.0.4 (Mixxx file_path) before committing to the §3.4 track-id path.
