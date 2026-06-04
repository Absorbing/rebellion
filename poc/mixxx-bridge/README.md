# Mixxx Bridge — input layer (Stage 3)

The daemon's **Mixxx → Studio** half: listen to Mixxx state on a virtual MIDI
port, decode it, resolve the loaded track to its analysed waveform, and fold
everything into per-deck state the renderer draws.

> **§3.4 correction — identity is a fingerprint, not a path.** The SPEC assumed a
> Mixxx controller script could read the track's `file_path` and send it as
> SysEx. It can't: Mixxx scripting exposes **no string controls** and has no
> `file_path` control (Test 0.0.4, confirmed against the 2.4 control reference).
> Instead the script sends a **numeric fingerprint** of the read-only controls
> Mixxx *does* expose on load — `track_samples` / `track_samplerate` / `duration`
> / `file_bpm` — and the bridge matches it to a `library` row. The 7-bit SysEx
> transport is reused unchanged; only the payload differs.

```
Mixxx (stock + Mixxx-Studio-Bridge.scripts.js)
   │  virtual MIDI "Mixxx-State" (loopMIDI / teVirtualMIDI)
   ▼
MixxxListener (RtMidi)        ── thin I/O shell, mixxx_listener.*
   ▼  raw MIDI bytes
MidiDecoder                   ── pure logic, mixxx_midi.*   ◀── unit-tested
   ▼  BridgeEvent{ TrackFingerprint }
DeckModel ─loader─▶ resolveTrackByFingerprint ─▶ mixxxdb.sqlite + analysis/{id}
   ▼                          (track_resolver.*, reuses mxb_decode)
DeckState[1..4]               ── what the renderer reads (deck_state.*)
```

The Mixxx-side emitter lives in `mixxx/Mixxx-Studio-Bridge.{midi.xml,scripts.js}`.

## Virtual MIDI port (the bridge ↔ Mixxx link)

The Studio is **not** a class-compliant MIDI device (it speaks NI's proprietary
protocol over the NIHardwareService pipe), so Mixxx can never see it directly —
the bridge is what turns it into a MIDI device Mixxx detects. That link needs a
virtual MIDI port, and the only question is who creates it (Windows has no
userland virtual-MIDI API; macOS/Linux do):

| Backend | Creates the port | Mixxx sees it | Cost |
|---|---|---|---|
| loopMIDI (RtMidi connects) | user, manually | yes | none; manual setup |
| teVirtualMIDI SDK | the bridge | yes | bundles a signed kernel driver; commercial license |
| **Windows MIDI Services** | the bridge | **yes — via the in-box MIDI 1.0 / WinMM compat layer** | **none; in-box on Win11 24H2+** |

**Decision (target = Win11):** use **Windows MIDI Services** in production — the
bridge creates an app-owned virtual device that Mixxx auto-detects like a
controller; no kernel driver, no licensing. Verified: app-created virtual
endpoints are auto-translated to MIDI 1.0 and surfaced to legacy WinMM/PortMidi
apps (Mixxx forwards through the compat shim with no changes). Caveats: GA'd
2026 (new; has a known-issues page), needs the WMS App SDK runtime, and is
C++/WinRT. Keep **RtMidi + loopMIDI** as a dev/fallback backend behind the same
seam for first bring-up. The WMS backend must be written against the real SDK on
the Windows rig (untestable off-device), so it's deliberately not stubbed blind.

## Files

| File | Role | Tested off-device |
|---|---|---|
| `mixxx_midi.{hpp,cpp}` | Decode MIDI + SysEx → `BridgeEvent`; unpack identity fingerprint | ✅ `test_mixxx_midi.cpp` |
| `track_resolver.{hpp,cpp}` | fingerprint → `library.id` → analysis blob → `Waveform` | ✅ matched real `mixxxdb.sqlite` on hardware (2026-05-31) |
| `deck_state.{hpp,cpp}` | fold events into per-deck state; trigger track load | ✅ |
| `mixxx_listener.{hpp,cpp}` | RtMidi virtual-port input shell | ✅ live loopMIDI in on hardware (2026-05-31) |
| `test_mixxx_midi.cpp` | unit tests (incl. the §3.4 worked SysEx example) | ✅ |

## What's verified

`mxb_bridge_tests` passes, plus a Node→C++ cross-language check:

- **SysEx 7-bit MSB-pack/unpack** — the original §3.4 worked example
  (`A1 B2 C3 04 05 06 07 08`) reconstructs exactly, plus fingerprint round-trips
  including all-bits-set (the case that stresses the MSB collector).
- **Cross-language seam** — bytes produced by the real `.scripts.js` packing
  logic, run in Node, decode in the real C++ `MidiDecoder` to the exact
  fingerprint (samples/samplerate/durationMs/bpmCenti, deck).
- **Channel-voice tables** — deck play/position/hotcue + master crossfader decode
  with correct units.
- **Deck model** — a track-identity SysEx invokes the loader once with the
  fingerprint and lands a waveform; play folds in; a clear (0x02) resets the deck.

## Confirmed end-to-end on hardware (2026-05-31)

Loading a track in Mixxx paints the Studio deck screen — the full inbound chain
live: loopMIDI in → SysEx fingerprint decode → `mixxxdb.sqlite` match → waveform
decode → both-screen render. The fingerprint identity (our pivot from the absent
`file_path`) matched a real library row first try. Maschine Studio serial
26C67082, loopMIDI port `Mixxx-State`.

**Waveform rendering (resolved):** the initial full-track max-overview looked
like a solid block ("all highs"). Replaced with a scrolling ~30s window centred
on the playhead, reduced by average + peak outline, and **frequency-band
colouring** decoded from `signal_filtered` (low→R, mid→G, high→B) — all confirmed
colouring correctly on hardware. Position is 14-bit for smooth scroll; the 2s
identity heartbeat is a no-op for the already-loaded track (no flicker).

## Build & test

```
cmake --build build --config Release --target mxb_bridge_tests
ctest --test-dir build -R mxb_bridge_tests --output-on-failure
```

RtMidi is fetched automatically (or vendor it at `poc/third_party/rtmidi/`).
To build the core without MIDI I/O: `-DREBELLION_POC_RTMIDI=OFF`.

## Integration: `studio_bridge` (Stage 3)

`poc/studio-bridge/` wires this layer to the screens:

```
studio_bridge "C:\Users\<you>\AppData\Local\Mixxx" ["Mixxx-State"]
```

- `deck_panel.hpp` — renders one `DeckState` to a 480×272 panel (deck badge,
  artist/title, BPM, full-track overview waveform + playhead, time + flags).
- `main.cpp` — reuses the proven studio_waveform device/loop pattern. Opens the
  loopMIDI port via `MixxxListener`, drains decoded events on the main thread
  into a `DeckModel` (loader = `resolveTrackByFingerprint`), and renders
  **display 0 = Deck A (ch1)**, **display 1 = Deck B (ch2)**, throttled to ~30fps.

RtMidi runs MIDI on its own thread; events cross to the single-threaded device
loop via a mutex-guarded queue, so `rebellion_loop`/`rebellion_rpc` stay on main.

Verified off-device: `mxb_deck_panel_tests` renders loaded / empty / no-waveform
panels and confirms each encodes through the RLE command path. `main.cpp`
compiles against rebellion.h. The two live seams (SQLite resolver, loopMIDI
listener) run for the first time on the Windows rig — by design, they're the
*only* unproven parts left.
