// Mixxx Studio Bridge — Studio control -> outbound MIDI mapping.
//
// The Studio's buttons/pads/knobs arrive as NIHIA events (BTN_DATA / PAD_DATA /
// KNOB_ROTATE) in the host callback. We forward each as a stable MIDI message on
// a virtual port that Mixxx reads with an ordinary controller mapping — so the
// *action* (play, cue, hotcue, load, ...) is decided in Mixxx (editable, or via
// Mixxx's MIDI-learn), and the bridge stays a dumb, predictable forwarder.
//
// Wire scheme (documented so it can be mapped/learned in Mixxx):
//   Buttons : 0x90 note=buttonid (0..95)            vel 127 press / 0 release   (ch 1)
//   Pads    : 0x91 note=padid (1..16)               vel = cpressure 1..127 / 0  (ch 2)
//   Knobs   : 0xB0 cc=0x30+(knob-1) (0x30..0x37)    val 0x41 CW / 0x3F CCW      (ch 1, relative)
// Pads use a separate channel so their note numbers can't collide with buttons.

#pragma once
#include <cstdint>

namespace mxb {

struct MidiOutMsg {
    uint8_t status = 0, data1 = 0, data2 = 0;
};

// MIDI channels (1-based for humans; encoded into the status low nibble as -1).
constexpr int kButtonChannel = 1;  // status 0x90/0x80, 0xB0
constexpr int kPadChannel    = 2;  // status 0x91/0x81

// Relative-encoder values Mixxx recognises (0x41 = +1, 0x3F = -1).
constexpr uint8_t kRelCW  = 0x41;
constexpr uint8_t kRelCCW = 0x3F;

// Button press/release -> Note On/Off on the button channel, note = buttonid.
inline MidiOutMsg mapButton(int buttonid, bool pressed) {
    return { static_cast<uint8_t>(0x90 | (kButtonChannel - 1)),
             static_cast<uint8_t>(buttonid & 0x7F),
             static_cast<uint8_t>(pressed ? 0x7F : 0x00) };
}

// Pad hit/release -> Note On/Off on the pad channel, velocity = pressure (0..127).
inline MidiOutMsg mapPad(int padid, bool pressed, int cpressure) {
    int vel = pressed ? (cpressure < 1 ? 1 : (cpressure > 127 ? 127 : cpressure)) : 0;
    return { static_cast<uint8_t>(0x90 | (kPadChannel - 1)),
             static_cast<uint8_t>(padid & 0x7F),
             static_cast<uint8_t>(vel) };
}

// Knob rotation tick -> relative CC on the button channel. `knobIndex` is 1..8.
inline MidiOutMsg mapKnobRotate(int knobIndex, bool clockwise) {
    return { static_cast<uint8_t>(0xB0 | (kButtonChannel - 1)),
             static_cast<uint8_t>(0x30 + (knobIndex - 1)),
             clockwise ? kRelCW : kRelCCW };
}

// Deck-focus model: the Studio has one transport, so PLAY/CUE/SYNC are routed by
// the bridge to the *focused* deck as dedicated per-deck notes (ch 1). Mixxx maps
// these to the matching deck.  note = base + action*2 + (deck-1):
//   play  deck1=0x60 deck2=0x61 ; cue 0x62/0x63 ; sync 0x64/0x65
enum class Transport { Play = 0, Cue = 1, Sync = 2 };
constexpr uint8_t kTransportBase = 0x60;

inline MidiOutMsg mapTransport(Transport t, int deck, bool pressed) {
    uint8_t note = static_cast<uint8_t>(kTransportBase +
                   static_cast<int>(t) * 2 + (deck - 1));
    return { static_cast<uint8_t>(0x90 | (kButtonChannel - 1)),
             note,
             static_cast<uint8_t>(pressed ? 0x7F : 0x00) };
}

}  // namespace mxb
