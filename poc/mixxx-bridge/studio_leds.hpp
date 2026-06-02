// Mixxx Studio Bridge — Maschine Studio LED + button-id map.
//
// Reverse-engineered on hardware via the interactive mapper (MXB_LED_PROBE).
// LED indices are 1-based, as passed to rebellion.sendLedData(serial, index,
// color, intensity). RGB controls use three consecutive indices (R, G, B); set
// a channel with color=WHITE + intensity 1..3, off with color=OFF.
//
//   LED INDEX MAP
//   1..24    pads 1-8  RGB   (pad p: R=1+(p-1)*3, G,B)
//   25..32   pads 1-8  white
//   33..40   top row (white), left->right
//   41 CHANNEL 42 PLUGIN 43 ARRANGE 44 MIX 45 BROWSE 46 SAMPLING 47 < 48 >
//   49 ALL 50 AUTO
//   51..54   (no visible output)
//   55..58   IN 1-4
//   59 MST 60 GRP 61 SND 62 CUE
//   63..86   pads 9-16 RGB   (pad p: R=63+(p-9)*3, G,B)
//   87..94   pads 9-16 white
//   95 COPY 96 PASTE 97 NOTE 98 NUDGE 99 UNDO 100 REDO 101 QUANTIZE 102 CLEAR
//   103 BACK(btn55) 104 <(btn54) 105 >(btn53) 106 ENTER(btn52)
//   107..130 GROUP A-H RGB  (group g=1..8: R=107+(g-1)*3, G,B)
//   131..138 GROUP A-H white
//   139 TAP 140 STEP 141 MACRO 142 NOTE_REPEAT 143 RESTART 144 METRO
//   145 EVENTS 146 GRID 147 PLAY 148 REC 149 ERASE 150 SHIFT
//   151..158 btn ids 35,34,33,32,39,38,37,36 (left function column)
//   159..174 peak meter LEFT  (174 = top)
//   175..190 peak meter RIGHT (190 = top)
//   191..192 (unmapped)
//   193 EDIT 194 CHANNEL 195 BROWSE 196 TUNE 197 SWING 198 VOLUME  (around jog)
//   199..213 jog ring (left->right)
//
//   BUTTON IDS (BTN_DATA buttonid; from mappings.lua + probe)
//   PLAY 29  REC 30  RESTART 28  GRID 27  METRO 31  EVENTS 24  ERASE 25
//   SHIFT 26  NOTE_REPEAT 11  STEP 9  MACRO 10  (TAP id: TBD)
//   GROUP_A 16 B 19 C 20 D 23 E 17 F 18 G 21 H 22
//   BACK 55  NAV_< 54  NAV_> 53  ENTER 52
//   KNOB1-8 touch 88..95 ; KNOB9 = big nav encoder (rotate only)
//   left function column btn ids 32..39 (names TBD)

#pragma once

namespace mxb {
namespace studioled {

// --- RGB / white element index helpers --------------------------------------
inline int padRGB(int pad, int ch) {           // pad 1..16, ch 0=R/1=G/2=B
    if (pad >= 1 && pad <= 8)  return 1  + (pad - 1) * 3 + ch;
    if (pad >= 9 && pad <= 16) return 63 + (pad - 9) * 3 + ch;
    return 0;
}
inline int padWhite(int pad) {
    if (pad >= 1 && pad <= 8)  return 25 + (pad - 1);
    if (pad >= 9 && pad <= 16) return 87 + (pad - 9);
    return 0;
}
inline int groupRGB(int group, int ch) {        // group 1..8 (A..H), ch 0/1/2
    if (group >= 1 && group <= 8) return 107 + (group - 1) * 3 + ch;
    return 0;
}
inline int groupWhite(int group) {
    if (group >= 1 && group <= 8) return 131 + (group - 1);
    return 0;
}
inline int peakLeft(int i)  { return (i >= 0 && i < 16) ? 159 + i : 0; }  // 0=bottom
inline int peakRight(int i) { return (i >= 0 && i < 16) ? 175 + i : 0; }
inline int jogRing(int i)   { return (i >= 0 && i < 15) ? 199 + i : 0; }

// --- single-LED indices ------------------------------------------------------
constexpr int CHANNEL = 41, PLUGIN = 42, ARRANGE = 43, MIX = 44, BROWSE = 45,
              SAMPLING = 46, NAV_PREV = 47, NAV_NEXT = 48, ALL = 49, AUTO = 50,
              IN1 = 55, IN2 = 56, IN3 = 57, IN4 = 58, MST = 59, GRP = 60,
              SND = 61, CUE = 62,
              COPY = 95, PASTE = 96, NOTE = 97, NUDGE = 98, UNDO = 99,
              REDO = 100, QUANTIZE = 101, CLEAR = 102,
              BACK = 103, ARROW_L = 104, ARROW_R = 105, ENTER = 106,
              TAP = 139, STEP = 140, MACRO = 141, NOTE_REPEAT = 142,
              RESTART = 143, METRO = 144, EVENTS = 145, GRID = 146,
              PLAY = 147, REC = 148, ERASE = 149, SHIFT = 150,
              JOG_EDIT = 193, JOG_CHANNEL = 194, JOG_BROWSE = 195,
              JOG_TUNE = 196, JOG_SWING = 197, JOG_VOLUME = 198;

// --- button ids (BTN_DATA buttonid) ------------------------------------------
namespace btn {
constexpr int PLAY = 29, REC = 30, RESTART = 28, GRID = 27, METRO = 31,
              EVENTS = 24, ERASE = 25, SHIFT = 26, NOTE_REPEAT = 11,
              STEP = 9, MACRO = 10, TAP = 8,
              GROUP_A = 16, GROUP_B = 19, GROUP_C = 20, GROUP_D = 23,
              GROUP_E = 17, GROUP_F = 18, GROUP_G = 21, GROUP_H = 22,
              // nav cluster + jog (jog = KNOB9 rotate; JOG_CLICK = its push)
              BACK = 55, NAV_PREV = 54, NAV_NEXT = 53, ENTER = 52, JOG_CLICK = 51,
              // left column (LED 41-50)
              CHANNEL = 7, PLUGIN = 0, ARRANGE = 6, MIX = 1, BROWSE = 5,
              SAMPLING = 2, COL_PREV = 4, COL_NEXT = 3, ALL = 13, AUTO = 12,
              // I/O + level section (LED 55-62)
              IN1 = 72, IN2 = 73, IN3 = 74, IN4 = 75,
              MST = 79, GRP = 78, SND = 77, CUE = 76;
// Top row above the screens (LED 33-40, left->right): ids 64,71,70,65,69,66,68,67.
}

constexpr int LEDCNT = 213;  // mappings.lua Studio ledcnt (true count)

}  // namespace studioled
}  // namespace mxb
