# Mixxx Studio Bridge — PoC

A vertical slice: read a Mixxx-analyzed track's waveform from `mixxxdb.sqlite`,
render it to the Maschine Studio's screens, and scroll with the knobs.
Display 0 = track list (Knob 2 selects), display 1 = waveform (Knob 1 scrubs).

Built in risk-first stages (see V00_RESULTS.md for the v0.0 groundwork):

| Stage | Tool | Status |
|---|---|---|
| 1   | `waveform-inspect/inspect_waveform.py` | ✅ Python data-chain inspector (no build) |
| 1.5 | `decode/` → `waveform_decode.exe` | C++ console decoder (sqlite3+zlib+protobuf), no device |
| 2   | render + knobs (links librebellion) | TODO |

## Confirmed waveform schema (from Stage 1, real data)
Analysis blob = `[4-byte big-endian uncompressed length][zlib stream]`; inflated
payload is a protobuf `Waveform`:
- field 1 (double) `visual_sample_rate` (e.g. 441.0)
- field 2 (double) `audio_visual_ratio`
- field 3 (len) `signal_all` = Signal{ field1: repeated int32 samples 0..255
  interleaved; field2: channels (=2 stereo); field3: units }
- field 4 (len) `signal_filtered` { low, mid, high } nested Signals

## Build (Stage 1.5)
Part of the normal Rebellion build (`REBELLION_BUILD_POC=ON`). Fetches zlib +
the SQLite amalgamation at configure time. If your machine blocks those
downloads, vendor them:
- `poc/third_party/zlib/` (madler/zlib checkout), and/or
- `poc/third_party/sqlite3/sqlite3.c` + `sqlite3.h` (SQLite amalgamation).

```
cmake --build build --config Release
build\bin\waveform_decode.exe "C:\Users\<you>\AppData\Local\Mixxx"
```

Its sample-rate / channels / frame-count must match the Python inspector.

## Note: Mixxx data dir
Real installs use `%LOCALAPPDATA%\Mixxx` (Local), not `%APPDATA%\Mixxx`
(Roaming) as the SPEC assumes. The tools default to Local, fall back to Roaming.
