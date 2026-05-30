// Mixxx Studio Bridge — loopMIDI listener (RtMidi shell over MidiDecoder).
//
// Thin I/O layer: opens a virtual MIDI input port by name (the loopMIDI
// "Mixxx-State" port, SPEC §11.1) and feeds every incoming message to a
// MidiDecoder. All decode logic lives in mixxx_midi.* (unit-tested off-device);
// this file is just the RtMidi plumbing, compiled only when RtMidi is present.

#pragma once
#include <functional>
#include <memory>
#include <string>

#include "mixxx_midi.hpp"

namespace mxb {

// Opens `port_name_substr` (first input port whose name contains it). Each MIDI
// message is decoded and forwarded to `sink`. RtMidi delivers messages on its
// own callback thread, so `sink` must be thread-safe w.r.t. the consumer (the
// daemon hands events to a queue per SPEC §4.5).
class MixxxListener {
public:
    explicit MixxxListener(MidiDecoder::Sink sink);
    ~MixxxListener();

    // Returns false + err if RtMidi can't open or no matching port exists.
    bool open(const std::string& port_name_substr, std::string& err);
    void close();

    // List available input ports (for diagnostics / "port not found" help).
    static std::vector<std::string> listInputPorts();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MidiDecoder decoder_;
};

}  // namespace mxb
