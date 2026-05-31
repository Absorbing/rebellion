// Mixxx Studio Bridge — Maschine Studio LED index map.
//
// Reverse-engineered on hardware via the probe (MXB_LED_PROBE), since rebellion's
// mappings.lua LED indices for the Studio were placeholders. Indices are 1-based,
// as passed to rebellion.sendLedData(serial, index, color, intensity).
//
//   1..24   pads 1-8, RGB triples   (pad p: R=1+(p-1)*3, G=R+1, B=R+2)
//   25..32  pads 1-8, white         (pad p: 25+(p-1))
//   33..40  top row (white), left->right
//   41 CHANNEL/MIDI  42 PLUGIN  43 ARRANGE  44 MIX  45 BROWSE  46 SAMPLING
//   47 left-arrow    48 right-arrow
//   49 ALL           50 AUTO
//   51..54  (no visible output)
//   55..58  IN 1-4
//   59 MST  60 GRP  61 SND  62 CUE
//   63..86  pads 9-16, RGB triples  (pad p: R=63+(p-9)*3, G=R+1, B=R+2)
//   87..94  pads 9-16, white        (pad p: 87+(p-9))
//   95 COPY 96 PASTE 97 NOTE 98 NUDGE 99 UNDO 100 REDO 101 QUANTIZE 102 CLEAR
//   103     display-related
//
// NOT addressable within 1..103: the transport row (PLAY/REC/RESTART/...),
// GROUP A-H, and knob LEDs. They live beyond ledcnt (=103) or aren't exposed
// this way — reaching them needs a larger ledcnt + a re-probe. So LED feedback
// uses the 16 RGB pads (which is the surface we want for hotcues/samplers).

#pragma once

namespace mxb {
namespace studioled {

// Per-pad RGB channel index. pad = 1..16, ch = 0(R)/1(G)/2(B). 0 if out of range.
inline int padRGB(int pad, int ch) {
    if (pad >= 1 && pad <= 8)  return 1  + (pad - 1) * 3 + ch;
    if (pad >= 9 && pad <= 16) return 63 + (pad - 9) * 3 + ch;
    return 0;
}

// Per-pad single white LED index. pad = 1..16. 0 if out of range.
inline int padWhite(int pad) {
    if (pad >= 1 && pad <= 8)  return 25 + (pad - 1);
    if (pad >= 9 && pad <= 16) return 87 + (pad - 9);
    return 0;
}

// Named section-button LED indices (white).
constexpr int CHANNEL = 41, PLUGIN = 42, ARRANGE = 43, MIX = 44,
              BROWSE = 45, SAMPLING = 46, ARROW_L = 47, ARROW_R = 48,
              ALL = 49, AUTO = 50,
              IN1 = 55, IN2 = 56, IN3 = 57, IN4 = 58,
              MST = 59, GRP = 60, SND = 61, CUE = 62,
              COPY = 95, PASTE = 96, NOTE = 97, NUDGE = 98,
              UNDO = 99, REDO = 100, QUANTIZE = 101, CLEAR = 102;

constexpr int LEDCNT = 103;  // mappings.lua Studio ledcnt

}  // namespace studioled
}  // namespace mxb
