// Mixxx Studio Bridge — loopMIDI listener (impl). Compiled only with RtMidi
// (guarded by MXB_HAVE_RTMIDI from CMake). The decode it drives is tested
// separately in test_mixxx_midi.cpp, so this stays deliberately thin.

#include "mixxx_listener.hpp"

#include <cstdio>
#include <cstdlib>

#ifdef MXB_HAVE_RTMIDI
#include <RtMidi.h>
#endif

namespace mxb {

struct MixxxListener::Impl {
#ifdef MXB_HAVE_RTMIDI
    std::unique_ptr<RtMidiIn> midiin;
#endif
};

MixxxListener::MixxxListener(MidiDecoder::Sink sink)
    : impl_(std::make_unique<Impl>()), decoder_(std::move(sink)) {}

MixxxListener::~MixxxListener() { close(); }

#ifdef MXB_HAVE_RTMIDI

static void rtCallback(double /*dt*/, std::vector<unsigned char>* msg, void* user) {
    if (!msg || msg->empty()) return;
    // Opt-in raw dump (set MXB_DEBUG_MIDI=1). Skip CC (0xB0) so the 20Hz position
    // stream doesn't bury SysEx/notes; identity = "F0 7D 01 ...".
    static const bool dbg = std::getenv("MXB_DEBUG_MIDI") != nullptr;
    if (dbg && ((*msg)[0] & 0xF0) != 0xB0) {
        std::fprintf(stderr, "MIDI<- ");
        for (unsigned char b : *msg) std::fprintf(stderr, "%02X ", b);
        std::fprintf(stderr, "\n");
    }
    auto* dec = static_cast<MidiDecoder*>(user);
    dec->onMessage(msg->data(), msg->size());
}

std::vector<std::string> MixxxListener::listInputPorts() {
    std::vector<std::string> out;
    try {
        RtMidiIn in;
        unsigned n = in.getPortCount();
        for (unsigned i = 0; i < n; ++i) out.push_back(in.getPortName(i));
    } catch (...) {}
    return out;
}

bool MixxxListener::open(const std::string& port_name_substr, std::string& err) {
    try {
        impl_->midiin = std::make_unique<RtMidiIn>();
        unsigned n = impl_->midiin->getPortCount();
        int match = -1;
        for (unsigned i = 0; i < n; ++i) {
            if (impl_->midiin->getPortName(i).find(port_name_substr) != std::string::npos) {
                match = static_cast<int>(i);
                break;
            }
        }
        if (match < 0) {
            err = "no MIDI input port matching \"" + port_name_substr +
                  "\" (is loopMIDI running with that port created?)";
            return false;
        }
        impl_->midiin->openPort(static_cast<unsigned>(match));
        // We need SysEx; don't filter it out. Also ignore timing/active-sensing.
        impl_->midiin->ignoreTypes(/*sysex=*/false, /*time=*/true, /*sense=*/true);
        impl_->midiin->setCallback(&rtCallback, &decoder_);
        return true;
    } catch (RtMidiError& e) {
        err = std::string("RtMidi error: ") + e.what();
        return false;
    } catch (...) {
        err = "unknown RtMidi failure";
        return false;
    }
}

void MixxxListener::close() {
    if (impl_ && impl_->midiin) {
        impl_->midiin->cancelCallback();
        impl_->midiin->closePort();
        impl_->midiin.reset();
    }
}

#else  // !MXB_HAVE_RTMIDI — build without MIDI I/O (decode core still usable)

std::vector<std::string> MixxxListener::listInputPorts() { return {}; }

bool MixxxListener::open(const std::string&, std::string& err) {
    err = "built without RtMidi (MXB_HAVE_RTMIDI off): no loopMIDI input available";
    return false;
}

void MixxxListener::close() {}

#endif

}  // namespace mxb
