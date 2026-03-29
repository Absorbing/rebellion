"""
example_pong.py — Pong on the Maschine MK3
============================================
Controls:
    KNOB1:  Move left paddle
    KNOB8:  Move right paddle (disables AI)
    PAD1:   Reset ball
    PLAY:   Pause/unpause
    SHIFT:  Quit

Usage:
    python3 example_pong.py
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from mk3_app import MK3App, LED_GREEN, LED_BRIGHT


class Pong(MK3App):

    def on_ready(self):
        self.ball_x = 240.0
        self.ball_y = 136.0
        self.ball_dx = 120.0
        self.ball_dy = 80.0
        self.paddle_l = 136.0
        self.paddle_r = 136.0
        self.paddle_h = 40
        self.score_l = 0
        self.score_r = 0
        self.paused = False
        self.ai_right = True

        self.set_pad_led(1, LED_GREEN, LED_BRIGHT)
        print("[Pong] Game started! KNOB1=paddle, PAD1=reset, PLAY=pause")

    def on_update(self, dt):
        if self.paused:
            return

        # AI
        if self.ai_right:
            diff = self.ball_y - self.paddle_r
            speed = 150.0 * dt
            if abs(diff) > 5:
                self.paddle_r += speed if diff > 0 else -speed
            self.paddle_r = max(self.paddle_h, min(272 - self.paddle_h, self.paddle_r))

        # Move ball
        self.ball_x += self.ball_dx * dt
        self.ball_y += self.ball_dy * dt

        # Top/bottom bounce
        if self.ball_y <= 4 or self.ball_y >= 268:
            self.ball_dy *= -1
            self.ball_y = max(4, min(268, self.ball_y))

        # Left paddle
        if self.ball_x <= 20 and self.ball_dx < 0:
            if abs(self.ball_y - self.paddle_l) < self.paddle_h:
                self.ball_dx = abs(self.ball_dx) * 1.05
                self.ball_dy = (self.ball_y - self.paddle_l) / self.paddle_h * 150
            else:
                self.score_r += 1
                self._reset_ball()

        # Right paddle
        if self.ball_x >= 460 and self.ball_dx > 0:
            if abs(self.ball_y - self.paddle_r) < self.paddle_h:
                self.ball_dx = -abs(self.ball_dx) * 1.05
                self.ball_dy = (self.ball_y - self.paddle_r) / self.paddle_h * 150
            else:
                self.score_l += 1
                self._reset_ball()

        # Draw
        self.left.clear()
        pl, pr = int(self.paddle_l), int(self.paddle_r)
        self.left.fill_rect(8, pl - self.paddle_h, 8, self.paddle_h * 2, 100, 200, 255)
        self.left.fill_rect(464, pr - self.paddle_h, 8, self.paddle_h * 2, 255, 100, 100)

        for y in range(0, 272, 16):
            self.left.fill_rect(238, y, 4, 8, 40, 40, 40)

        bx, by = int(self.ball_x), int(self.ball_y)
        self.left.fill_circle(bx, by, 5, 255, 255, 255)

        self.left.text(180, 10, str(self.score_l), 100, 200, 255, 3)
        self.left.text(280, 10, str(self.score_r), 255, 100, 100, 3)
        self.left.flush()

    def _reset_ball(self):
        self.ball_x, self.ball_y = 240.0, 136.0
        self.ball_dx = 120.0 if self.score_l > self.score_r else -120.0
        self.ball_dy = 80.0

    def on_knob(self, knob, direction, value):
        y = max(self.paddle_h, min(272 - self.paddle_h, int(value / 1023 * 272)))
        if knob == "KNOB1":
            self.paddle_l = y
        elif knob == "KNOB8":
            self.ai_right = False
            self.paddle_r = y

    def on_pad(self, pad_id, pressure, velocity, state):
        if state == "PRESSED" and pad_id == 1:
            self._reset_ball()

    def on_button(self, button, state):
        if state != "PRESSED":
            return
        if button == "PLAY":
            self.paused = not self.paused
        elif button == "SHIFT":
            self.stop()


if __name__ == "__main__":
    Pong(fps=20).run()
