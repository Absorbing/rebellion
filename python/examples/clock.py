"""
example_clock.py — Clock Hand Demo
====================================
Draws a circle with labeled points and a sweeping clock hand.
Demonstrates the MK3 display animation loop.

Controls:
    PLAY:   Pause/resume
    SHIFT:  Quit
    Pads:   Highlight points on the circle

Usage:
    python3 example_clock.py
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from mk3_app import MK3App
import math


# 12 labeled points evenly spaced around a circle (like a clock face)
POINTS = [
    ("12",   0), ("1",   30), ("2",   60), ("3",   90),
    ("4",  120), ("5",  150), ("6",  180), ("7",  210),
    ("8",  240), ("9",  270), ("10", 300), ("11", 330),
]

# Colors for each point (rainbow spread)
COLORS = [
    (220, 60, 60),  (220,140, 60),  (220,220, 60),  (140,220, 60),
    ( 60,220, 60),  ( 60,220,140),  ( 60,220,220),  ( 60,140,220),
    ( 60, 60,220),  (140, 60,220),  (220, 60,220),  (220, 60,140),
]


class ClockDemo(MK3App):

    def on_ready(self):
        self.clock_angle = 0.0
        self.paused = False
        self.active_points = set()  # indices 0-11
        print("[Clock] Running! PLAY=pause, SHIFT=quit, pads=highlight")

    def on_update(self, dt):
        CX, CY = 240, 136
        RADIUS = 100
        LABEL_R = 120
        DOT_R = 4

        self.left.clear()
        self.left.circle(CX, CY, RADIUS, 60, 60, 60)

        for i, (label, angle) in enumerate(POINTS):
            angle_rad = (angle - 90) * math.pi / 180
            active = i in self.active_points

            r, g, b = COLORS[i]
            if not active:
                r, g, b = r // 4, g // 4, b // 4

            # Dot on circle
            dot_x = int(CX + math.cos(angle_rad) * RADIUS)
            dot_y = int(CY + math.sin(angle_rad) * RADIUS)
            dr = DOT_R + 2 if active else DOT_R
            self.left.fill_circle(dot_x, dot_y, dr, r, g, b)

            # Line from center if active
            if active:
                self.left.line(CX, CY, dot_x, dot_y, r, g, b)

            # Label
            lx = int(CX + math.cos(angle_rad) * LABEL_R)
            ly = int(CY + math.sin(angle_rad) * LABEL_R)
            tx = lx - (len(label) * 6) // 2
            ty = ly - 3
            self.left.text(tx, ty, label, r, g, b, 1)

        # Sweeping hand
        if not self.paused:
            hand_rad = (self.clock_angle - 90) * math.pi / 180
            tip_x = int(CX + math.cos(hand_rad) * 85)
            tip_y = int(CY + math.sin(hand_rad) * 85)
            self.left.line(CX, CY, tip_x, tip_y, 255, 255, 255)
            self.left.fill_circle(tip_x, tip_y, 3, 255, 255, 255)
            self.clock_angle = (self.clock_angle + 6) % 360

        self.left.flush()

    def on_pad(self, pad_id, pressure, velocity, state):
        # Map pads 1-12 to points on the circle
        idx = pad_id - 1
        if idx < 0 or idx > 11:
            return
        if state == "PRESSED":
            self.active_points.add(idx)
        elif state == "RELEASED":
            self.active_points.discard(idx)

    def on_button(self, button, state):
        if state != "PRESSED":
            return
        if button == "PLAY":
            self.paused = not self.paused
        elif button == "SHIFT":
            self.stop()


if __name__ == "__main__":
    ClockDemo(fps=20).run()
