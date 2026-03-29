"""
mk3_app.py — Maschine MK3 App Framework
=========================================
Subclass MK3App to build interactive applications for the MK3.
Handles the event loop, frame timing, and clean shutdown.

Place this file next to mk3.py in your rebellion root directory.

Usage:
    from mk3_app import MK3App

    class MyApp(MK3App):
        def on_ready(self):
            self.left.clear().text(100, 100, "Hello!", 255, 255, 255, 3).flush()

        def on_update(self, dt):
            pass

    MyApp(fps=20).run()
"""

import time
from mk3 import (
    MK3, Display, SCREEN_WIDTH, SCREEN_HEIGHT, PAD_TO_NOTE,
    LED_OFF, LED_RED, LED_ORANGE, LED_LIGHT_ORANGE, LED_WARM_YELLOW,
    LED_YELLOW, LED_LIME, LED_GREEN, LED_MINT, LED_CYAN,
    LED_TURQUOISE, LED_BLUE, LED_PLUM, LED_VIOLET, LED_PURPLE,
    LED_MAGENTA, LED_FUCHSIA, LED_WHITE,
    LED_DIM, LED_LOW, LED_MED, LED_BRIGHT,
    pressure_int_to_float, pressure_to_velocity,
)


class MK3App:
    """Base class for MK3 applications.

    Subclass this and override any of the on_* methods.
    Call .run() to start the event loop.

    Attributes:
        left   — Display instance for the left screen (Display 0)
        right  — Display instance for the right screen (Display 1)
        mk3    — The underlying MK3 engine (for advanced use / LEDs)
        width  — Screen width  (480)
        height — Screen height (272)
    """

    def __init__(self, fps=20, lib_path="build/lib/librebellion.dylib"):
        self.mk3 = MK3(lib_path=lib_path)
        self.fps = fps
        self.left  = self.mk3.left
        self.right = self.mk3.right
        self.width  = SCREEN_WIDTH
        self.height = SCREEN_HEIGHT
        self._running = False
        self._frame_interval = 1.0 / fps if fps > 0 else 0
        self._last_frame = 0.0

        # Wire MK3 callbacks to our methods
        self.mk3.on_pad     = self._handle_pad
        self.mk3.on_button  = self._handle_button
        self.mk3.on_knob    = self._handle_knob
        self.mk3.on_encoder = self._handle_encoder
        self.mk3.on_ready   = self._handle_ready

    # ── Internal event routing ──────────────────────────────────

    def _handle_ready(self):
        self.on_ready()

    def _handle_pad(self, pad_id, pressure, velocity, state):
        self.on_pad(pad_id, pressure, velocity, state)

    def _handle_button(self, button, state):
        self.on_button(button, state)

    def _handle_knob(self, knob, direction, value):
        self.on_knob(knob, direction, value)

    def _handle_encoder(self, direction):
        self.on_encoder(direction)

    # ── Override these in your subclass ─────────────────────────

    def on_ready(self):
        """Called once when the MK3 device connects and is ready."""
        pass

    def on_update(self, dt):
        """Called every frame (at target fps). dt = seconds since last frame."""
        pass

    def on_pad(self, pad_id, pressure, velocity, state):
        """Called when a pad is pressed or released.

        Args:
            pad_id:   1-16 (physical pad number)
            pressure: 0.0-1.0 float (0.0 on release)
            velocity: 0-127 MIDI velocity (0 on release)
            state:    "PRESSED" or "RELEASED"
        """
        pass

    def on_button(self, button, state):
        """Called when any button is pressed or released.

        Args:
            button: e.g. "PLAY", "REC", "STOP", "SHIFT",
                    "DBTN1"-"DBTN8", "GROUP_A"-"GROUP_H",
                    "PAD_MODE", "KEYBOARD", "BROWSE", etc.
            state:  "PRESSED" or "RELEASED"
        """
        pass

    def on_knob(self, knob, direction, value):
        """Called when a knob is turned.

        Args:
            knob:      "KNOB1"-"KNOB8"
            direction: "CLOCKWISE" or "COUNTER_CLOCKWISE"
            value:     Absolute position (0-1023, endless potentiometer)
        """
        pass

    def on_encoder(self, direction):
        """Called when the main push encoder is rotated."""
        pass

    def on_stop(self):
        """Called during shutdown, before cleanup."""
        pass

    # ── Control ─────────────────────────────────────────────────

    def stop(self):
        """Stop the app gracefully. Can be called from any handler."""
        self._running = False

    def set_pad_led(self, pad_id, color=LED_OFF, intensity=LED_BRIGHT):
        """Set a pad LED color."""
        self.mk3.set_pad_led(pad_id, color, intensity)

    def set_led(self, index, color=LED_OFF, intensity=LED_BRIGHT):
        """Set any LED by raw index."""
        self.mk3.set_led(index, color, intensity)

    # ── Main loop ───────────────────────────────────────────────

    def run(self):
        """Start the app and block until stopped."""
        self._running = True
        self.mk3.start()
        print(f"[MK3App] Waiting for device... (Ctrl+C to quit)")

        self._last_frame = time.monotonic()

        try:
            while self._running:
                ready = self.mk3.tick()

                if ready and self.fps > 0:
                    now = time.monotonic()
                    if now - self._last_frame >= self._frame_interval:
                        dt = now - self._last_frame
                        self._last_frame = now
                        try:
                            self.on_update(dt)
                        except Exception as e:
                            print(f"[MK3App] on_update error: {e}")

        except KeyboardInterrupt:
            print("\n[MK3App] Shutting down...")

        try:
            self.on_stop()
        except Exception as e:
            print(f"[MK3App] on_stop error: {e}")

        self.mk3.shutdown()
        print("[MK3App] Done.")
