# Studio Bridge — first hardware bring-up (loopMIDI path)

Goal: **load a track in Mixxx → it paints the Deck A / Deck B screens on the
Maschine Studio.** This is the first end-to-end run of the Mixxx→Studio chain.
Uses loopMIDI as the virtual port (the dev/fallback backend); the Windows MIDI
Services backend comes later.

Prereqs already proven on this rig: NIHIA acquires the Studio, both displays
render (v0.0 Scenario A), waveform decode + DB read work. The two *new* things
this run exercises: the loopMIDI listener, and the fingerprint→library match.

---

## 1. Build

The bridge target + vendored RtMidi were added since your last configure, so
**reconfigure**, then build (from the repo root):

```
cmake -S . -B build
cmake --build build --config Release --target studio_bridge
```

RtMidi is compiled straight in (vendored at `poc/third_party/rtmidi`, WinMM
backend) — no extra DLL, no network at configure time.

Optional sanity check that the decode/match logic is sound on this machine:

```
ctest --test-dir build -R "mxb_bridge_tests|mxb_deck_panel_tests" --output-on-failure
```

## 2. Stage the exe at the repo root

Same rule as the v0.0 tools (rebellion loads `scripts/` by relative path):

```
copy build\bin\studio_bridge.exe .
:: rebellion.dll must already be at the repo root from v0.0; if not:
copy build\lib\rebellion.dll .
```

## 3. loopMIDI

1. Install loopMIDI (https://www.tobias-erichsen.de/software/loopmidi.html).
2. In its window, type a port name **`Mixxx-State`** and click **+**.
   You should see `Mixxx-State` in the port list. Leave loopMIDI running.

## 4. Mixxx mapping

The two files in `mixxx/` must go in Mixxx's **controllers** folder — the
`controllers` subfolder of the same Mixxx settings dir that holds
`mixxxdb.sqlite` (on this rig: `%LOCALAPPDATA%\Mixxx`, NOT Roaming):

```
copy mixxx\Mixxx-Studio-Bridge.midi.xml    "%LOCALAPPDATA%\Mixxx\controllers\"
copy mixxx\Mixxx-Studio-Bridge.scripts.js  "%LOCALAPPDATA%\Mixxx\controllers\"
```
(create the `controllers` folder if it doesn't exist.)

Then in Mixxx → **Preferences → Controllers**:
1. Select **`Mixxx-State`** in the device list (the loopMIDI port shows up here).
2. **Load Mapping** → **Mixxx Studio Bridge (state)**.
3. Tick **Enabled** → **Apply**.

Confirm it loaded: Mixxx **Help → … (or the log file)** shows `StudioBridge> init`.

## 5. Run

Order matters a little — have Mixxx emitting before/around launching the bridge:

1. loopMIDI running with the `Mixxx-State` port (step 3).
2. Mixxx running, mapping enabled (step 4).
3. Studio plugged in (USB + PSU) and powered on.
4. From the repo root:
   ```
   studio_bridge.exe "%LOCALAPPDATA%\Mixxx" "Mixxx-State"
   ```
   (first arg = Mixxx data dir with `mixxxdb.sqlite`; second = port name.)

You should see the boot splash, then **"waiting for Mixxx on deck A/B"** on each
screen. **Load a track on Deck 1 in Mixxx** → Deck A fills in (title, BPM,
overview waveform); hit play → the playhead moves. Deck 2 → Deck B.

---

## What the bridge prints (your read-out)

- `device ON, serial=…` — NIHIA acquired the Studio.
- `ready: display 0 = Deck A …` — screens initialised.
- On load: `loaded: <artist> - <title> [<path>] (<N> frames)` — **success.**
- On a miss: `resolve failed: no analysed track matching fingerprint
  (samples=… sr=… dur=…s)` — see troubleshooting.
- If the port isn't found at startup it prints the available input ports and
  keeps running on the splash.

## Troubleshooting (most-likely first)

| Symptom | Likely cause / fix |
|---|---|
| Screens stay on "waiting", no bridge log on load | Mixxx not emitting. Check the mapping is **Enabled** and `StudioBridge> init` is in the Mixxx log. Files in the right `controllers` folder? |
| Bridge: "no MIDI input port matching Mixxx-State" + port list | loopMIDI port name mismatch, or loopMIDI not running. Name must be exactly `Mixxx-State` (or pass the actual name as arg 2). |
| `resolve failed: no analysed track matching fingerprint` | **The thing we're testing.** The track loaded but didn't match a library row. Copy that line (samples/sr/dur) to me — likely a `duration` precision/tolerance tweak, or the track has no waveform analysis yet (analyse it in Mixxx once). |
| Wrong/garbled metadata on screen | Fingerprint matched the wrong row (collision). Rare; send me the line + the real track and I'll tighten the match (add `track_samples` exactness). |
| `sqlite open failed` | Wrong arg 1. Point it at the folder containing `mixxxdb.sqlite`. |
| Playhead moves in coarse steps | Expected — position is 7-bit (≈4 px) for now. 14-bit + clock extrapolation is a planned refinement. |
| Studio not acquired | v0.0 §0.0.1 notes: start NIHardwareService, close other NI apps, replug. |

## What this run validates (and what it doesn't)

- ✅ if a track paints: loopMIDI listener + SysEx fingerprint decode + DB match +
  waveform decode + both-screen render — the whole inbound chain, live.
- ⬜ still unproven after this: Windows MIDI Services backend (self-announce),
  and the outbound Studio-buttons→Mixxx direction.
