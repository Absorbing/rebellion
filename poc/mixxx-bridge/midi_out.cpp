// Mixxx Studio Bridge — outbound MIDI port (impl). See midi_out.hpp.

#include "midi_out.hpp"

#include <cstdio>

#ifdef MXB_HAVE_RTMIDI
#include <RtMidi.h>
#endif

namespace mxb {

struct MidiOut::Impl {
#ifdef MXB_HAVE_RTMIDI
    std::unique_ptr<RtMidiOut> midiout;
#endif
};

MidiOut::MidiOut() : impl_(std::make_unique<Impl>()) {}
MidiOut::~MidiOut() { close(); }

#ifdef MXB_HAVE_RTMIDI

std::vector<std::string> MidiOut::listOutputPorts() {
    std::vector<std::string> out;
    try {
        RtMidiOut o;
        unsigned n = o.getPortCount();
        for (unsigned i = 0; i < n; ++i) out.push_back(o.getPortName(i));
    } catch (...) {}
    return out;
}

bool MidiOut::open(const std::string& port_name_substr, std::string& err) {
    try {
        impl_->midiout = std::make_unique<RtMidiOut>();
        unsigned n = impl_->midiout->getPortCount();
        int match = -1;
        for (unsigned i = 0; i < n; ++i) {
            if (impl_->midiout->getPortName(i).find(port_name_substr) != std::string::npos) {
                match = static_cast<int>(i);
                break;
            }
        }
        if (match < 0) {
            err = "no MIDI output port matching \"" + port_name_substr +
                  "\" (create a second loopMIDI port for Studio->Mixxx)";
            impl_->midiout.reset();
            return false;
        }
        impl_->midiout->openPort(static_cast<unsigned>(match));
        return true;
    } catch (RtMidiError& e) {
        err = std::string("RtMidi error: ") + e.what();
        return false;
    } catch (...) {
        err = "unknown RtMidi failure";
        return false;
    }
}

bool MidiOut::isOpen() const { return impl_ && impl_->midiout && impl_->midiout->isPortOpen(); }

void MidiOut::send(const MidiOutMsg& m) {
    if (!impl_ || !impl_->midiout) return;
    std::vector<unsigned char> bytes{m.status, m.data1, m.data2};
    try { impl_->midiout->sendMessage(&bytes); } catch (...) {}
}

void MidiOut::close() {
    if (impl_ && impl_->midiout) {
        try { impl_->midiout->closePort(); } catch (...) {}
        impl_->midiout.reset();
    }
}

#else  // !MXB_HAVE_RTMIDI

std::vector<std::string> MidiOut::listOutputPorts() { return {}; }
bool MidiOut::open(const std::string&, std::string& err) {
    err = "built without RtMidi (MXB_HAVE_RTMIDI off)";
    return false;
}
bool MidiOut::isOpen() const { return false; }
void MidiOut::send(const MidiOutMsg&) {}
void MidiOut::close() {}

#endif

}  // namespace mxb
