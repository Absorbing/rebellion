"""
mk3.py — Maschine MK3 Engine
=============================
Core module that handles the rebellion library lifecycle, event parsing,
display drawing, and LED control. Import this to build MK3 apps.

Place this file in your rebellion root directory (next to build/).

Usage:
    from mk3 import MK3, Display

    mk3 = MK3()
    mk3.on_pad = my_pad_handler
    mk3.start()
"""

import ctypes
import json
import os
import sys
import math
import struct
import time
from collections import deque
import pathlib
_REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
_DEFAULT_LIB = str(_REPO_ROOT / "build" / "lib" / "librebellion.dylib")

# ─────────────────────────────────────────────────────────────────
# Constants
# ─────────────────────────────────────────────────────────────────

SCREEN_WIDTH  = 480
SCREEN_HEIGHT = 272

# Pad layout → MIDI note mapping (pads 1-16 → notes 36-51, GM drum standard)
PAD_TO_NOTE = {i: 35 + i for i in range(1, 17)}

# LED color palette (0-17), matches rebellion's niproto CONST_COLORS
LED_OFF     = 0
LED_RED     = 1
LED_ORANGE  = 2
LED_LIGHT_ORANGE = 3
LED_WARM_YELLOW  = 4
LED_YELLOW  = 5
LED_LIME    = 6
LED_GREEN   = 7
LED_MINT    = 8
LED_CYAN    = 9
LED_TURQUOISE = 10
LED_BLUE    = 11
LED_PLUM    = 12
LED_VIOLET  = 13
LED_PURPLE  = 14
LED_MAGENTA = 15
LED_FUCHSIA = 16
LED_WHITE   = 17

# LED intensity levels
LED_DIM    = 0
LED_LOW    = 1
LED_MED    = 2
LED_BRIGHT = 3

# Pad LED indices: pad 1-16 → LED array index
# rebellion uses: idx = 88 + niproto._pad_num_to_code(padid)
_PAD_NUM_TO_CODE = {
    13: 0, 14: 1, 15: 2, 16: 3,
     9: 4, 10: 5, 11: 6, 12: 7,
     5: 8,  6: 9,  7:10,  8:11,
     1:12,  2:13,  3:14,  4:15,
}

# ─────────────────────────────────────────────────────────────────
# Pressure helpers
# ─────────────────────────────────────────────────────────────────

CURVE_POWER = 0.3  # <1 = more sensitive to light touch

def pressure_int_to_float(pressure_int):
    """Reinterpret raw uint32 pressure as IEEE 754 float (0.0-1.0)."""
    try:
        raw = struct.pack('<I', pressure_int & 0xFFFFFFFF)
        return struct.unpack('<f', raw)[0]
    except:
        return 0.0

def pressure_to_velocity(pressure_int, curve=CURVE_POWER):
    """Convert raw pressure integer to MIDI velocity (0-127)."""
    if pressure_int <= 0:
        return 0
    p = pressure_int_to_float(pressure_int)
    curved = math.pow(min(p, 1.0), curve)
    velocity = int(curved * 126) + 1
    return min(velocity, 127)

def min(a, b):
    return a if a < b else b

# ─────────────────────────────────────────────────────────────────
# Display class
# ─────────────────────────────────────────────────────────────────

class Display:
    """Drawing API for one MK3 screen. Accumulates commands, sends on flush().

    All drawing methods return self for chaining:
        screen.clear().fill_rect(10, 10, 100, 50, 255, 0, 0).flush()

    Coordinate system: (0,0) = top-left, (479,271) = bottom-right.
    Colors are RGB 0-255 (converted to RGB565 on the Lua side).
    """
    WIDTH  = SCREEN_WIDTH
    HEIGHT = SCREEN_HEIGHT

    def __init__(self, display_num, mk3_instance):
        self.display_num = display_num
        self._mk3 = mk3_instance
        self._cmds = []

    def clear(self):
        """Clear screen to black."""
        self._cmds.append(["clear"])
        return self

    def fill(self, r, g, b):
        """Fill entire screen with a solid color."""
        self._cmds.append(["fill", r, g, b])
        return self

    def fill_rect(self, x, y, w, h, r, g, b):
        """Draw a filled rectangle."""
        self._cmds.append(["fillRect", x, y, w, h, r, g, b])
        return self

    def rect(self, x, y, w, h, r, g, b):
        """Draw a rectangle outline."""
        self._cmds.append(["rect", x, y, w, h, r, g, b])
        return self

    def line(self, x0, y0, x1, y1, r, g, b):
        """Draw a line between two points."""
        self._cmds.append(["line", x0, y0, x1, y1, r, g, b])
        return self

    def pixel(self, x, y, r, g, b):
        """Set a single pixel."""
        self._cmds.append(["pixel", x, y, r, g, b])
        return self

    def circle(self, x, y, radius, r, g, b):
        """Draw a circle outline."""
        self._cmds.append(["circle", x, y, radius, r, g, b])
        return self

    def fill_circle(self, x, y, radius, r, g, b):
        """Draw a filled circle."""
        self._cmds.append(["fillCircle", x, y, radius, r, g, b])
        return self

    def text(self, x, y, string, r, g, b, scale=1):
        """Draw text using the 5x7 bitmap font.

        Args:
            scale: 1 = native 5x7, 2 = 10x14, etc.
            Supported chars: A-Z, a-z, 0-9, space, #
        """
        self._cmds.append(["text", x, y, string, r, g, b, scale])
        return self

    def image(self):
        """Load the rebellion-480x272.png image."""
        self._cmds.append(["image"])
        return self

    def flush(self):
        """Send all queued draw commands to the hardware as one batch.

        Must be called from the main loop (not from a callback).
        Automatically skipped if no device is connected yet.
        """
        if not self._cmds or not self._mk3._serial:
            return
        self._mk3._rpc_call("rebellion.display.batch",
                            [self._mk3._serial, self.display_num, self._cmds])
        self._mk3._lib.rebellion_loop(ctypes.c_uint32(5))
        self._cmds = []


# ─────────────────────────────────────────────────────────────────
# MK3 engine
# ─────────────────────────────────────────────────────────────────

class MK3:
    """Core engine for the Maschine MK3.

    Handles:
        - Loading librebellion and managing its lifecycle
        - Parsing hardware events (pads, buttons, knobs)
        - Display drawing (via .left and .right Display instances)
        - LED control
        - Thread-safe command queuing (callback -> main loop)

    Events are dispatched through callback attributes:
        mk3.on_pad     = func(pad_id, pressure_float, velocity, state)
        mk3.on_button  = func(button_name, state)
        mk3.on_knob    = func(knob_name, direction, value)
        mk3.on_encoder = func(direction)
        mk3.on_ready   = func()
        mk3.on_raw     = func(parsed_dict)
    """

    CALLBACK_TYPE = ctypes.CFUNCTYPE(
        ctypes.c_int, ctypes.c_uint, ctypes.c_uint,
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32
    )

    def __init__(self, lib_path=_DEFAULT_LIB, suppress_output=True):
        self._lib_path = lib_path
        self._lib = None
        self._cb = None
        self._serial = None
        self._cmd_queue = deque()
        self._active_pads = set()
        self._ready_fired = False

        # Screens
        self.left  = Display(0, self)
        self.right = Display(1, self)

        # Event callbacks
        self.on_pad     = None
        self.on_button  = None
        self.on_knob    = None
        self.on_encoder = None
        self.on_ready   = None
        self.on_raw     = None

        self._load_lib(suppress_output)

    def _load_lib(self, suppress):
        """Load librebellion, optionally suppressing its C-level stdout spam."""
        if suppress:
            devnull = os.open(os.devnull, os.O_WRONLY)
            old_stdout = os.dup(1)
            os.dup2(devnull, 1)

        self._lib = ctypes.CDLL(self._lib_path)

        if suppress:
            sys.stdout = os.fdopen(os.dup(old_stdout), "w")
            os.dup2(old_stdout, 1)
            os.close(devnull)

    def _rpc_call(self, method, params):
        """Send a JSON-RPC call to rebellion. Must be called from main loop."""
        payload = json.dumps({"method": method, "params": params, "id": 1})
        data = payload.encode("utf-8")
        buf = ctypes.create_string_buffer(data)
        self._lib.rebellion_rpc(0, 0, buf, len(data))

    def _queue_rpc(self, method, params):
        """Queue an RPC call for the main loop (safe from callbacks)."""
        self._cmd_queue.append((method, params))

    def _process_queue(self):
        """Drain and execute queued RPC calls."""
        while self._cmd_queue:
            method, params = self._cmd_queue.popleft()
            try:
                self._rpc_call(method, params)
                self._lib.rebellion_loop(ctypes.c_uint32(5))
            except Exception as e:
                print(f"  [mk3] RPC error: {e}")

    # ── Event handling ──────────────────────────────────────────

    def _on_event(self, mf, mt, data, length):
        """Internal ctypes callback. Runs on macOS dispatch queue.
        DO NOT call RPC or flush() from here.
        """
        try:
            raw = bytes(data[:length])
            parsed = json.loads(raw.decode("utf-8"))

            event = parsed.get("event", "")

            if not event and ("result" in parsed or "error" in parsed):
                return 0

            outer = parsed.get("data", {})
            d = outer.get("data", outer)

            if self.on_raw:
                try:
                    self.on_raw(parsed)
                except Exception:
                    pass

            if event == "device.state":
                serial = outer.get("serial", "")
                state = outer.get("state", "")
                if state == "ON" and serial:
                    self._serial = serial
                return 0

            if event == "PAD_DATA":
                pad_id   = d.get("padid", 0)
                pressure = d.get("pressure", 0)
                state    = d.get("state", "")

                if state == "PRESSED" and pad_id not in self._active_pads:
                    self._active_pads.add(pad_id)
                    if self.on_pad:
                        pf = pressure_int_to_float(pressure)
                        vel = pressure_to_velocity(pressure)
                        try:
                            self.on_pad(pad_id, pf, vel, "PRESSED")
                        except Exception:
                            pass

                elif state == "RELEASED" and pad_id in self._active_pads:
                    self._active_pads.discard(pad_id)
                    if self.on_pad:
                        try:
                            self.on_pad(pad_id, 0.0, 0, "RELEASED")
                        except Exception:
                            pass

            elif event == "BTN_DATA":
                btn   = d.get("button", "?")
                state = d.get("state", "?")
                if self.on_button:
                    try:
                        self.on_button(btn, state)
                    except Exception:
                        pass

            elif event == "KNOB_DATA":
                knob = d.get("knob", "?")
                direction = d.get("direction", "?")
                value = d.get("value", 0)
                if self.on_knob:
                    try:
                        self.on_knob(knob, direction, value)
                    except Exception:
                        pass

            elif event == "ENCODER_DATA":
                direction = d.get("direction", "?")
                if self.on_encoder:
                    try:
                        self.on_encoder(direction)
                    except Exception:
                        pass

        except Exception as e:
            print(f"  [mk3] Event error: {e}")

        return 0

    # ── LED control ─────────────────────────────────────────────

    def set_led(self, led_index, color=LED_OFF, intensity=LED_BRIGHT):
        """Set any LED by its raw index (1-based). Queued for main loop."""
        if self._serial:
            self._queue_rpc("rebellion.sendLedData",
                           [self._serial, led_index, color, intensity])

    def set_pad_led(self, pad_id, color=LED_OFF, intensity=LED_BRIGHT):
        """Set a pad's LED by pad number (1-16)."""
        code = _PAD_NUM_TO_CODE.get(pad_id)
        if code is not None:
            self.set_led(88 + code + 1, color, intensity)

    # ── Lifecycle ───────────────────────────────────────────────

    def start(self):
        """Initialize rebellion and begin listening for events."""
        self._cb = self.CALLBACK_TYPE(self._on_event)
        self._lib.rebellion(self._cb)

    def tick(self):
        """Run one iteration of the rebellion event loop.
        Returns True if device is connected and ready.
        """
        self._lib.rebellion_loop(ctypes.c_uint32(5))

        if self._serial and not self._ready_fired:
            self._ready_fired = True
            if self.on_ready:
                try:
                    self.on_ready()
                except Exception as e:
                    print(f"  [mk3] on_ready error: {e}")

        self._process_queue()
        return self._serial is not None

    def shutdown(self):
        """Clean shutdown of rebellion."""
        try:
            self._lib.rebellion(None)
        except:
            pass

    @property
    def serial(self):
        """Device serial string, or None if not connected yet."""
        return self._serial

    @property
    def connected(self):
        return self._serial is not None

    @property
    def active_pads(self):
        """Set of currently held pad IDs."""
        return frozenset(self._active_pads)
