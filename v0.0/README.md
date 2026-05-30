# Mixxx Studio Bridge — v0.0 (risk reduction, before forking)

This directory holds everything needed to run **v0.0** from SPEC §0: four cheap
hardware-in-the-loop tests that confirm or invalidate the assumptions the rest
of the project is built on. **Do v0.0 before building anything else.**

Total effort ~4–8 hours across 4 sessions. Cost of skipping it: weeks of work on
possibly-wrong assumptions. The load-bearing question is **Test 0.0.3** (does the
MK3 display protocol work on the older-protocol Studio?).

> ⚠️ These tests require a **physical Maschine Studio on Windows** with the NIHIA
> service. They cannot be run in CI or on Linux. The code and docs here were
> prepared so the hardware session is just *run → observe → record*.

## What's here

```
v0.0/
├── README.md                     ← this runbook
├── V00_RESULTS.md                ← results template; fill in at the rig
├── CMakeLists.txt                ← builds the two probe exes
├── tools/
│   ├── nihia-monitor/main.cpp    ← Test 0.0.2b: prints typed NIHIA events
│   └── display-probe/main.cpp    ← Test 0.0.3: pushes a test pattern to both panels
└── mixxx-probe/
    ├── probe.midi.xml            ← Test 0.0.4: Mixxx controller mapping
    └── probe.js                  ← Test 0.0.4: probes engine.getValue(..., "file_path")
```

Plus one in-place change already applied to the Rebellion tree:
- `scripts/mappings.lua` — the Studio entry now carries display config
  (`103, 2, 272, 480` = ledcnt, dcnt, dheight, dwidth), mirroring MK3, required
  by Test 0.0.3. Revert this if you want stock-Rebellion Studio behaviour.

## Prerequisites (SPEC §0)

- [ ] Maschine 2 installed; `services.msc` shows **Native Instruments Hardware Agent** running.
- [ ] Maschine 2, Controller Editor, Komplete Kontrol Standalone **all closed** (NIHIA grants the device to one client at a time).
- [ ] Studio plugged in via **USB AND external PSU** (panels stay dark without the PSU). Power LED on.
- [ ] CMake 3.13+ and Visual Studio 2022 Build Tools.
- [ ] Mixxx 2.4+ installed and launched once (creates `%APPDATA%\Mixxx\mixxxdb.sqlite`).

## Build

The probes link Rebellion's `librebellion` and build as part of the normal
Rebellion build (wired via `REBELLION_BUILD_V00_PROBES`, default ON):

```bash
cmake -S . -B build
cmake --build build --config Release
# Outputs: build/bin/nihia_monitor[.exe], build/bin/display_probe[.exe]
```

Set the active device in `config.lua` at the repo root:

```lua
devices = { "MASCHINE_STUDIO" }
```

---

## Session 1 — acquire (Test 0.0.1, ~2h, mostly build)

1. Build per above. Run `nihia_monitor` (it claims the device via config.lua).
2. **Pass:** logs a NIHIA pipe connect + `device.state ... ON` with a serial; no immediate disconnect.
3. **Fail modes:** pipe-connect error → restart NIHIA service; acquires-then-drops → another NI client holds the device, close it.

Record in `V00_RESULTS.md` → Test 0.0.1.

## Session 2 — events + monitor (Test 0.0.2 / 0.0.2b, ~2–4h)

With `nihia_monitor` running, press one of each control category and confirm a
recognised line appears:

| Press | Expect |
|---|---|
| PLAY | `BTN_DATA btnid=<N> state=PRESSED` |
| Pad 1 soft / hard | `PAD_DATA pad=0 ... pressure=<low/high>` |
| Pad 1 hold | repeated `PAD_DATA` with changing `cpressure` (aftertouch) |
| Knob 1 turn | `KNOB_ROTATE knob=... rot=±1` |
| Knob 1 touch | `BTN_DATA ...` (touch encoded as button) |
| Group A | `BTN_DATA ...` |
| Jog rotate / touch / push | `KNOB_ROTATE` / `BTN_DATA` / `BTN_DATA` |

Use `--filter PAD_DATA,BTN_DATA` to focus, `--csv` to capture for a spreadsheet,
`--raw` to see the full JSON. Pass criteria: every category produces an event and
unmapped controls still surface with numeric IDs. Record IDs in `V00_RESULTS.md`.

> The 28 existing Studio button entries in `mappings.lua` mean this is largely
> confirmatory. Note any yellow flags (constant velocity, missing knob touch, buggy jog).

## Session 3 — display probe (Test 0.0.3, ~1–3h)  ⚠️ HIGHEST RISK

The `mappings.lua` display config is already applied. Run:

```bash
build/bin/display_probe
```

It waits for the Studio to power on, then sends a 10px black/white column
pattern to display 0, waits 2s, sends it to display 1, logging every NIHIA
response and the `rebellion_rpc` call duration.

**Watch both panels** and record the outcome:

| Result | Scenario | Next |
|---|---|---|
| Pattern correct on both | **A — protocol generalises** | Best case; keep the mapping change; proceed to v0.1 full scope. |
| No error but dark/garbage | **B — silent route failure** | Capture Maschine 2 → Studio with USBPcap + Wireshark; reverse the real protocol; parallel PORT_MAIN display path. |
| Disconnect / error / crash | **C — hard mismatch** | As B, more careful; study cabl's `MaschineStudio` class. |

If the probe hangs when sending from inside the callback, flip
`SEND_FROM_CALLBACK` to `0` in `tools/display-probe/main.cpp` and rebuild
(see the CAVEAT at the top of that file). Record which path worked.

## Session 4 — Mixxx file_path (Test 0.0.4, ~30m)

1. Copy `mixxx-probe/probe.midi.xml` and `mixxx-probe/probe.js` to `%APPDATA%\Mixxx\controllers\`.
2. Mixxx → Preferences → Controllers → enable **Studio Bridge file_path probe**.
3. Load a track on deck 1. Check Help → Logs for `PROBE> ... file_path returned: ...`.
4. **Pass:** full path string. **Fail:** `<undefined>`/`<empty>` → note Mixxx version and plan the SQLite/track_id fallback (SPEC §3.4).

Record in `V00_RESULTS.md` → Test 0.0.4.

---

## After v0.0

Complete `V00_RESULTS.md` and let it drive the gate (SPEC §8):
- 0.0.1 + 0.0.2 pass → Studio-as-input-controller is viable regardless of displays.
- 0.0.3 = A → full plan incl. displays; proceed to v0.1.
- 0.0.3 = B/C → split v0.1 into **v0.1a** (controls-only MIDI bridge) + **v0.1b** (display reversing).

## Probability calibration (SPEC §0, for expectation-setting)

| Test | Likely outcome |
|---|---|
| 0.0.1 passes | ~95% |
| 0.0.2 passes | ~95% |
| 0.0.3 = A | ~40–50% |
| 0.0.3 = B | ~35–40% |
| 0.0.3 = C | ~10–15% |
| 0.0.4 passes | ~80% |
