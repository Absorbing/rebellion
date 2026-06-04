// Mixxx Studio Bridge — outbound MIDI port (RtMidi). Sends Studio control events
// to Mixxx via a virtual port (a second loopMIDI port, e.g. "Studio-Control").
// Thin shell, mirrors mixxx_listener; compiled only with RtMidi.

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "control_map.hpp"

namespace mxb {

class MidiOut {
public:
    MidiOut();
    ~MidiOut();

    // Open the first OUTPUT port whose name contains `port_name_substr`.
    // Returns false + err if RtMidi is unavailable or no matching port exists.
    bool open(const std::string& port_name_substr, std::string& err);
    void close();
    bool isOpen() const;

    void send(const MidiOutMsg& m);

    static std::vector<std::string> listOutputPorts();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mxb
