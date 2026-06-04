# Building & running the v0.0 probes on Windows

Step-by-step for the Windows rig. Assumes **Visual Studio 2022** is installed
with the **"Desktop development with C++"** workload (gives you `cl`, CMake, and
Ninja). See `v0.0/README.md` for the test procedure; this file is just *get the
binaries built and running*.

> Session 4 (the Mixxx `file_path` probe) needs none of this — it's just two
> files copied into `%APPDATA%\Mixxx\controllers\`. This doc is for the two C++
> probes (Sessions 1–3).

---

## 0. Get the code on the Windows machine

If the repo isn't already on this machine, clone your fork and check out the
working branch:

```bat
git clone https://github.com/Absorbing/rebellion.git
cd rebellion
git checkout claude/bold-babbage-l79mq
```

## 1. Open the right shell

Start menu → **"Developer Command Prompt for VS 2022"** (NOT a plain `cmd` or
PowerShell — this one puts `cl`, `cmake`, and `ninja` on PATH). `cd` into the
repo:

```bat
cd path\to\rebellion
```

Sanity check the tools are visible:

```bat
cmake --version
cl
```

## 2. Configure

From the repo root:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Notes:
- The Go `rebelliond` daemon is **off by default** on this branch (it's broken
  on Windows and you don't need it). If CMake ever complains about Go/TDM-GCC,
  that's the thing — leave `REBELLION_BUILD_REBELLIOND` OFF.
- The v0.0 probes are **on by default** (`REBELLION_BUILD_V00_PROBES=ON`).
- If `-G Ninja` gives you trouble, drop it and let CMake pick the VS generator:
  `cmake -S . -B build` then build with `--config Release` (step 3).

## 3. Build

```bat
cmake --build build --config Release
```

This produces (paths may be `build\bin\` or `build\bin\Release\` depending on
generator):
- `rebellion.dll`        — the librebellion core (in `build\lib\` or alongside)
- `rebellion_host.exe`   — Rebellion's stock demo host
- `nihia_monitor.exe`    — v0.0 Test 0.0.2b
- `display_probe.exe`    — v0.0 Test 0.0.3

## 4. Stage binaries at the repo root  ← easy to miss

Rebellion loads its Lua from a **relative** path (`scripts/`), so the exe must
run with the **repo root as the working directory**, and `rebellion.dll` must
sit next to the exe. Simplest: copy the built artifacts to the repo root.

```bat
copy build\bin\*.exe .
copy build\lib\rebellion.dll .
```

(If your generator put them under `Release\` subfolders, adjust the source
paths. After this, `nihia_monitor.exe`, `display_probe.exe`, and `scripts\`,
`config.lua` are all in the same top-level folder.)

## 5. Point Rebellion at the Studio

Edit `config.lua` at the repo root:

```lua
devices = { "MASCHINE_STUDIO" }
```

## 6. Run (from the repo root)

Confirm the Studio is plugged in (USB **and** PSU), powered, and no NI app
(Maschine 2, Controller Editor, Komplete Kontrol) is open.

**Session 1 & 2 — events:**
```bat
nihia_monitor.exe
```
Press controls; watch for `BTN_DATA` / `PAD_DATA` / `KNOB_ROTATE` lines.
Useful flags: `nihia_monitor.exe --filter PAD_DATA,BTN_DATA --raw`

**Session 3 — display probe (highest risk):**
```bat
display_probe.exe
```
It waits for the Studio to power on, then paints black/white columns on display
0, then display 1. **Watch both panels** and record outcome A / B / C in
`v0.0\V00_RESULTS.md`.

Redirect logs to a file if you want to study them:
```bat
display_probe.exe > probe-stdout.log 2>&1
```

---

## First-build reality check

I prepared the probe code by reading Rebellion's source but **could not compile
it** (no Windows/NIHIA in my environment). The single biggest unknown is whether
**stock Rebellion builds cleanly on your machine at all** — that's literally
SPEC Test 0.0.1's "most of the time is build setup."

If the build fights you, isolate it: build just the stock host first with
`cmake --build build --target rebellion_host`. Once that works and acquires the
Studio, the probes (same link, same runtime) are the easy part. Capture any
build errors and I'll help you work through them.

## Common gotchas

| Symptom | Likely cause / fix |
|---|---|
| `cmake`/`cl` not found | Not in the Developer Command Prompt. Reopen that specific shell. |
| Configure error mentioning Go / cgo / gcc | `rebelliond` got enabled. Keep `REBELLION_BUILD_REBELLIOND=OFF`. |
| Exe starts then can't find `start.lua` | Not running from repo root, or `scripts\` missing. Run from the folder that contains `scripts\`. |
| Exe missing `rebellion.dll` on launch | DLL not next to the exe. Copy `rebellion.dll` to the same folder. |
| Acquire fails / device drops | An NI app is holding the device. Close Maschine 2 / Controller Editor / Komplete Kontrol. |
| Panels stay dark in display_probe | Could be the real result (Scenario B). Also check the PSU is connected. |
