# Mixxx Bridge — input layer (Stage 3)

The daemon's **Mixxx → Studio** half (SPEC §3.3, §3.4): listen to Mixxx state on
a loopMIDI port, decode it, resolve loaded tracks to their analysed waveforms,
and fold everything into per-deck state the renderer draws.

```
Mixxx (stock + StudioBridge.js)
   │  loopMIDI "Mixxx-State"
   ▼
MixxxListener (RtMidi)        ── thin I/O shell, mixxx_listener.*
   ▼  raw MIDI bytes
MidiDecoder                   ── pure logic, mixxx_midi.*   ◀── unit-tested
   ▼  BridgeEvent
DeckModel  ──loader──▶ resolveTrackByPath ──▶ mixxxdb.sqlite + analysis/{id}
   ▼                          (track_resolver.*, reuses mxb_decode)
DeckState[1..4]               ── what the renderer reads (deck_state.*)
```

## Files

| File | Role | Tested off-device |
|---|---|---|
| `mixxx_midi.{hpp,cpp}` | Decode MIDI + SysEx → `BridgeEvent` (§3.3/§3.4) | ✅ `test_mixxx_midi.cpp` |
| `track_resolver.{hpp,cpp}` | path → `library.id` → analysis blob → `Waveform` (§3.4) | ⬜ needs a real `mixxxdb.sqlite` |
| `deck_state.{hpp,cpp}` | fold events into per-deck state; trigger track load | ✅ |
| `mixxx_listener.{hpp,cpp}` | RtMidi loopMIDI input shell | ⬜ needs loopMIDI (Windows) |
| `test_mixxx_midi.cpp` | unit tests (incl. the §3.4 worked SysEx example) | ✅ |

## What's verified

`mxb_bridge_tests` passes, covering the parts with real algorithmic risk:

- **SysEx 7-bit MSB-pack/unpack** — the SPEC §3.4 worked example
  (`A1 B2 C3 04 05 06 07 08`) reconstructs exactly, plus round-trips of ASCII,
  UTF-8 (accents + multibyte), empty, and multi-group paths through an encoder
  mirrored from the SPEC §11.4 controller script.
- **Channel-voice tables** — deck play/bpm/position/hotcue and master crossfader
  decode to the right `BridgeEvent` with correct units (BPM un-mapped from the
  60–187.5 range; position 0..1; crossfader −1..+1).
- **Sampler channel math** — ch5 note 0x02 → sampler 3, ch7 note 0x12 → sampler 35.
- **Deck model** — a track-path SysEx invokes the loader once and lands a
  waveform; play/bpm fold in; a clear (0x02) resets the deck.

## What still needs hardware / a real machine

- `track_resolver` against a real `mixxxdb.sqlite` (the JOIN is written to §3.1/§3.4
  but unproven against live data — verify the `library.location → track_locations.id`
  direction on your DB).
- `MixxxListener` against loopMIDI + the `StudioBridge.js` mapping actually
  emitting SysEx on `track_loaded`.

## Build & test

```
cmake --build build --config Release --target mxb_bridge_tests
ctest --test-dir build -R mxb_bridge_tests --output-on-failure
```

RtMidi is fetched automatically (or vendor it at `poc/third_party/rtmidi/`).
To build the core without MIDI I/O: `-DREBELLION_POC_RTMIDI=OFF`.

## Next wiring step

Drive the existing `studio_waveform` screens from `DeckState` instead of the
local library browser: construct a `DeckModel` whose loader calls
`resolveTrackByPath`, open a `MixxxListener` on `"Mixxx-State"`, and on
`deck(1).dirty` re-render the waveform panel from `deck(1).waveform`. That makes
"load a track in Mixxx → Studio screen updates" real end-to-end.
