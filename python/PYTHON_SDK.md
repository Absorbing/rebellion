# MK3 Python SDK

Python framework for building interactive applications on the **Native Instruments Maschine MK3** — dual displays, 16 velocity-sensitive pads, 8 knobs, buttons, and LEDs.

Built on top of [rebellion](https://github.com/terminar/rebellion), which communicates with the MK3 via NI's IPC protocol (no need to stop NIHA/NIHIA).

## Quick Start

```python
from mk3_app import MK3App

class MyApp(MK3App):
    def on_ready(self):
        self.left.clear().text(100, 100, "Hello MK3!", 255, 255, 255, 3).flush()

    def on_pad(self, pad_id, pressure, velocity, state):
        if state == "PRESSED":
            print(f"Pad {pad_id} hit! velocity={velocity}")

MyApp(fps=20).run()
```

Run from the `python/` directory:

```bash
cd rebellion/python
python3 my_app.py
```

## Prerequisites

- macOS (rebellion uses CFMessagePort IPC)
- NI drivers installed (NIHIA must be running)
- rebellion built: `mkdir build && cd build && cmake .. && make`
- Python 3.10+
- `pip install python-rtmidi` (only if using MIDI features — **not** `pip install rtmidi`, that's the wrong package)

---

## Architecture

```
┌─────────────────────────────────────────┐
│  Your App (subclass MK3App)             │  ← you write this
├─────────────────────────────────────────┤
│  mk3_app.py  — game loop framework     │  ← handles timing, events, lifecycle
├─────────────────────────────────────────┤
│  mk3.py  — core engine                 │  ← Display, LEDs, event parsing, RPC
├─────────────────────────────────────────┤
│  librebellion.dylib (C++ / Lua)         │  ← IPC to NIHA/NIHIA → hardware
└─────────────────────────────────────────┘
```

**mk3.py** — loads `librebellion.dylib` via ctypes, owns the rebellion lifecycle, parses JSON events from the hardware, provides the `Display` drawing class and LED control. Handles the critical constraint that RPC calls cannot happen inside the ctypes callback (macOS dispatch queue deadlock) by queuing them for the main loop.

**mk3_app.py** — thin game-loop framework on top. Subclass `MK3App`, override hooks, call `.run()`. Manages frame timing, device connection wait, and clean shutdown.

---

## Files

```
python/
├── mk3.py              # Core engine (Display, MK3, LEDs, pressure mapping)
├── mk3_app.py          # App framework (subclass this)
├── midi_bridge.py      # Pads → virtual MIDI port
└── examples/
    ├── clock.py         # Animated clock face demo
    └── pong.py          # Playable pong game
```

---

## MK3App — The Framework

### Constructor

```python
MK3App(fps=20, lib_path="build/lib/librebellion.dylib")
```

- `fps` — target frames per second for `on_update()`. Set to `0` to disable (event-only apps like the MIDI bridge).
- `lib_path` — path to librebellion. Auto-resolved relative to `mk3.py`'s location.

### Lifecycle

```python
app = MyApp(fps=20)
app.run()       # blocks until app.stop() or Ctrl+C
```

`run()` handles everything: rebellion init, waiting for device connection, calling `on_ready()`, frame-throttled `on_update(dt)` calls, event dispatch, and clean shutdown.

### Hooks (override these)

| Method | Called when | Args |
|--------|-----------|------|
| `on_ready()` | Device connects, once | — |
| `on_update(dt)` | Every frame at target fps | `dt`: seconds since last frame (float) |
| `on_pad(pad_id, pressure, velocity, state)` | Pad pressed or released | see below |
| `on_button(button, state)` | Button pressed or released | see below |
| `on_knob(knob, direction, value)` | Knob turned | see below |
| `on_encoder(direction)` | Main encoder rotated | `"CLOCKWISE"` or `"COUNTER_CLOCKWISE"` |
| `on_stop()` | Shutdown (before cleanup) | — |

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `self.left` | `Display` | Left screen (display 0) |
| `self.right` | `Display` | Right screen (display 1) |
| `self.mk3` | `MK3` | Underlying engine (advanced use) |
| `self.width` | `int` | 480 |
| `self.height` | `int` | 272 |

### Control methods

```python
self.stop()                                    # exit the run loop
self.set_pad_led(pad_id, color, intensity)     # light a pad
self.set_led(index, color, intensity)          # light any LED
```

---

## Display — Drawing API

Two screens, each **480×272 pixels**. Coordinate system: `(0,0)` = top-left, `(479,271)` = bottom-right. Colors are RGB `0-255` (converted to RGB565 on the hardware).

All drawing methods return `self` for chaining. Nothing is sent to hardware until `flush()`.

### Primitives

```python
screen.clear()                                  # black
screen.fill(r, g, b)                            # solid color
screen.fill_rect(x, y, w, h, r, g, b)          # filled rectangle
screen.rect(x, y, w, h, r, g, b)               # rectangle outline
screen.line(x0, y0, x1, y1, r, g, b)           # line
screen.pixel(x, y, r, g, b)                     # single pixel
screen.circle(x, y, radius, r, g, b)            # circle outline
screen.fill_circle(x, y, radius, r, g, b)       # filled circle
screen.text(x, y, string, r, g, b, scale=1)     # bitmap text
screen.image()                                   # load rebellion-480x272.png
screen.flush()                                   # send to hardware
```

### Text

5×7 bitmap font, scaled by integer factor. Supported characters: `A-Z`, `a-z`, `0-9`, space, `#`.

- `scale=1` → 5×7 pixels per character, 6px spacing
- `scale=2` → 10×14 pixels per character, 12px spacing
- `scale=3` → 15×21 pixels per character, 18px spacing

### Chaining example

```python
self.left.clear()\
    .fill_rect(0, 0, 480, 40, 40, 40, 40)\
    .text(10, 12, "Score 42", 255, 255, 255, 2)\
    .fill_circle(240, 160, 20, 0, 255, 0)\
    .flush()
```

### Performance

- ~20fps achievable with a full clear + redraw per frame
- Each `flush()` serializes all queued commands as one JSON batch RPC
- The pixel buffer lives in Lua (avoids serializing 130K pixels)
- For best results, keep draw commands under ~30 per frame

### Critical constraint

**Never call `flush()` from inside an event handler.** The event callback runs on macOS's dispatch queue. Calling RPC from there deadlocks. Always draw in `on_update()` or buffer state in your handlers and render next frame.

---

## Input Events

### Pads

```python
def on_pad(self, pad_id, pressure, velocity, state):
```

| Arg | Type | Description |
|-----|------|-------------|
| `pad_id` | `int` | 1–16, physical pad number |
| `pressure` | `float` | 0.0–1.0 (IEEE 754 float, not MIDI's 0-127) |
| `velocity` | `int` | 0–127 MIDI velocity (power curve, `CURVE_POWER=0.3`) |
| `state` | `str` | `"PRESSED"` or `"RELEASED"` |

Fires once on initial press, once on release. Continuous pressure updates while held are coalesced (no retriggering).

**Physical layout → pad IDs:**

```
 13  14  15  16      (top row)
  9  10  11  12
  5   6   7   8
  1   2   3   4      (bottom row, closest to you)
```

**Pad → MIDI note mapping** (GM drum standard):

```
 48  49  50  51
 44  45  46  47
 40  41  42  43
 36  37  38  39
```

Available as `PAD_TO_NOTE` dict: `from mk3 import PAD_TO_NOTE`.

### Buttons

```python
def on_button(self, button, state):
```

`state` is `"PRESSED"` or `"RELEASED"`. Button names include:

**Transport:** `PLAY`, `REC`, `STOP`, `RESTART`, `ERASE`, `TAP`, `FOLLOW`, `METRO`, `QUANTIZE`

**Display buttons (above screens):** `DBTN1` through `DBTN8` (4 per screen, left to right)

**Groups:** `GROUP_A` through `GROUP_H`

**Modes:** `PAD_MODE`, `KEYBOARD`, `CHORDS`, `STEP`, `FIXED_VEL`, `SCENE`, `PATTERN`, `EVENTS`, `VARIATION`, `DUPLICATE`, `SELECT`, `SOLO`, `MUTE`

**Navigation:** `NAVIGATE_LEFT`, `NAVIGATE_RIGHT`, `NAVIGATE_UP`, `NAVIGATE_DOWN`

**Other:** `SHIFT`, `LOCK`, `NOTES`, `VOLUME`, `SWING`, `TEMPO`, `NOTE_REPEAT`, `SAMPLING`, `BROWSE`, `AUTO`, `CHANNEL`, `PLUGIN`, `ARRANGER`, `MIXER`, `FILE`, `SETTINGS`, `MACRO`, `UNDO`, `REDO`, `CLEAR`, `PITCH`, `MOD`, `PERFORM`

### Knobs

```python
def on_knob(self, knob, direction, value):
```

| Arg | Type | Description |
|-----|------|-------------|
| `knob` | `str` | `"KNOB1"` through `"KNOB8"` |
| `direction` | `str` | `"CLOCKWISE"` or `"COUNTER_CLOCKWISE"` |
| `value` | `int` | 0–1023, absolute position |

The MK3 uses **endless potentiometers** (not incremental encoders). You always know the exact position.

### Main encoder

```python
def on_encoder(self, direction):
```

The large push-encoder on the right side. `direction` is `"CLOCKWISE"` or `"COUNTER_CLOCKWISE"`. Push is reported as a button event.

---

## LEDs

18-color indexed palette with 4 intensity levels. Queued for main-loop execution (safe to call from handlers).

```python
self.set_pad_led(pad_id, color, intensity)    # pad 1-16
self.mk3.set_led(led_index, color, intensity) # any LED by raw index (1-based)
```

### Colors (0–17)

```
LED_OFF=0  LED_RED=1  LED_ORANGE=2  LED_LIGHT_ORANGE=3
LED_WARM_YELLOW=4  LED_YELLOW=5  LED_LIME=6  LED_GREEN=7
LED_MINT=8  LED_CYAN=9  LED_TURQUOISE=10  LED_BLUE=11
LED_PLUM=12  LED_VIOLET=13  LED_PURPLE=14  LED_MAGENTA=15
LED_FUCHSIA=16  LED_WHITE=17
```

### Intensity (0–3)

```
LED_DIM=0  LED_LOW=1  LED_MED=2  LED_BRIGHT=3
```

### Example

```python
from mk3 import LED_RED, LED_GREEN, LED_BRIGHT

self.set_pad_led(1, LED_RED, LED_BRIGHT)     # pad 1 red
self.set_pad_led(5, LED_GREEN, LED_BRIGHT)   # pad 5 green
self.set_pad_led(1, LED_OFF)                 # pad 1 off
```

---

## Pressure Mapping

The MK3 reports pad pressure as an IEEE 754 float (0.0–1.0), not MIDI's limited 0-127. The SDK provides both:

```python
def on_pad(self, pad_id, pressure, velocity, state):
    # pressure = 0.0-1.0 float (raw hardware value)
    # velocity = 0-127 int (power curve mapped for MIDI)
```

The velocity mapping uses `CURVE_POWER = 0.3` (adjustable in `mk3.py`). Values below 1.0 are more sensitive to light touches. Set to 1.0 for linear mapping.

Utility functions available:

```python
from mk3 import pressure_int_to_float, pressure_to_velocity
```

---

## Complete Example: Bouncing Ball

```python
from mk3_app import MK3App, LED_RED, LED_BRIGHT

class BouncingBall(MK3App):
    def on_ready(self):
        self.x, self.y = 240.0, 136.0
        self.dx, self.dy = 150.0, 100.0
        self.radius = 10
        self.color = (0, 255, 0)

    def on_update(self, dt):
        # Move
        self.x += self.dx * dt
        self.y += self.dy * dt

        # Bounce
        if self.x <= self.radius or self.x >= 480 - self.radius:
            self.dx *= -1
        if self.y <= self.radius or self.y >= 272 - self.radius:
            self.dy *= -1

        # Draw
        r, g, b = self.color
        self.left.clear()
        self.left.fill_circle(int(self.x), int(self.y), self.radius, r, g, b)
        self.left.flush()

    def on_pad(self, pad_id, pressure, velocity, state):
        if state == "PRESSED":
            # Speed boost proportional to hit velocity
            self.dx *= 1.0 + velocity / 127
            self.dy *= 1.0 + velocity / 127
            self.set_pad_led(pad_id, LED_RED, LED_BRIGHT)
        else:
            self.set_pad_led(pad_id, 0)

    def on_button(self, button, state):
        if button == "SHIFT" and state == "PRESSED":
            self.stop()

BouncingBall(fps=20).run()
```

---

## Known Limitations

- **Single app at a time.** Rebellion holds one IPC connection to the MK3. Cannot run two apps simultaneously.
- **macOS only** currently (rebellion uses CFMessagePort). Linux/Raspberry Pi would need direct USB transport.
- **~20fps max** for display animation. Bottleneck is JSON RPC round-trip + Lua pixel iteration + full-frame IPC.
- **Fast pad rolls** on a single pad can miss events. The 5ms poll interval + active-pad guard means rapid lift-and-restrike can get swallowed.
- **Near-simultaneous hits** on two pads have a slight delay between them (one IPC message per rebellion loop cycle).
- **Font is limited** to A-Z, a-z, 0-9, space, and #. No punctuation, no Unicode.
- **NIHIA must be running.** If NI's services aren't active, rebellion can't connect.

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `OSError: dlopen ... no such file` | Build rebellion first: `cd build && cmake .. && make` |
| `ModuleNotFoundError: mk3_app` | Run from `python/` directory, or add path fix (see examples) |
| No events, app hangs | Make sure NIHIA is running and MK3 is connected via USB |
| `pip install rtmidi` doesn't work | Use `pip install python-rtmidi` (different package) |
| Display not updating | Make sure you're calling `flush()` in `on_update()`, not in event handlers |
| LEDs not responding | LED commands are queued; they execute on the next `tick()` cycle |
