"""
midi_bridge.py — MK3 MIDI Bridge
==================================
Maps MK3 pads to MIDI notes on a virtual port.

Usage:
    python3 midi_bridge.py

Then open your DAW and select "Maschine MK3 Bridge" as MIDI input.
"""

import rtmidi
from mk3_app import MK3App
from mk3 import PAD_TO_NOTE

MIDI_CHANNEL = 9  # Channel 10 = standard drum channel


class MidiBridge(MK3App):

    def on_ready(self):
        self.midi_out = rtmidi.MidiOut()
        self.midi_out.open_virtual_port("Maschine MK3 Bridge")
        print("[MidiBridge] Virtual MIDI port: 'Maschine MK3 Bridge'")
        print("[MidiBridge] Hit pads on the MK3. Ctrl+C to stop.\n")

        self.left.clear()
        self.left.text(120, 120, "MIDI Bridge", 255, 255, 255, 2)
        self.left.text(150, 150, "Ready", 0, 255, 0, 2)
        self.left.flush()

    def on_pad(self, pad_id, pressure, velocity, state):
        note = PAD_TO_NOTE.get(pad_id)
        if note is None:
            return

        if state == "PRESSED":
            self.midi_out.send_message([0x90 | MIDI_CHANNEL, note, velocity])
            print(f"  NOTE ON   pad={pad_id:2d}  note={note}  vel={velocity:3d}")
        elif state == "RELEASED":
            self.midi_out.send_message([0x80 | MIDI_CHANNEL, note, 0])
            print(f"  NOTE OFF  pad={pad_id:2d}  note={note}")

    def on_button(self, button, state):
        print(f"  BTN  {button:15s}  {state}")

    def on_stop(self):
        for pad_id in self.mk3.active_pads:
            note = PAD_TO_NOTE.get(pad_id)
            if note:
                self.midi_out.send_message([0x80 | MIDI_CHANNEL, note, 0])
        self.midi_out.close_port()


if __name__ == "__main__":
    MidiBridge(fps=0).run()
