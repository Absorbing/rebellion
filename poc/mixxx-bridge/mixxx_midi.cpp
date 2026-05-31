// Mixxx Studio Bridge — Mixxx → daemon MIDI decode (impl). See header + SPEC §3.3/§3.4.

#include "mixxx_midi.hpp"

namespace mxb {

bool MidiDecoder::unpackSysex(const uint8_t* body, size_t len,
                              int& msgType, int& deck, std::vector<uint8_t>& payload) {
    // body starts at <msg_type> <deck> <packed payload...> (F0 7D and F7 already
    // stripped). Need at least the two header bytes.
    if (len < 2) return false;
    msgType = body[0];
    deck    = body[1];
    payload.clear();

    // 7-bit MSB-pack (SPEC §3.4 transport): each group is 1 MSB byte then up to 7
    // data bytes; bit j of the MSB byte is the top bit of data byte j.
    size_t i = 2;
    while (i < len) {
        uint8_t msb = body[i++];
        for (int j = 0; j < 7 && i < len; ++j) {
            uint8_t b = body[i++] & 0x7F;
            if (msb & (1 << j)) b |= 0x80;
            payload.push_back(b);
        }
    }
    return true;
}

bool MidiDecoder::parseFingerprint(const std::vector<uint8_t>& p, TrackFingerprint& out) {
    if (p.size() < kFingerprintBytes) return false;
    auto u = [&](size_t off, int n) {
        uint32_t v = 0;
        for (int k = 0; k < n; ++k) v = (v << 8) | p[off + k];
        return v;
    };
    out.samples    = u(0, 4);
    out.samplerate = u(4, 3);
    out.durationMs = u(7, 4);
    out.bpmCenti   = static_cast<uint16_t>(u(11, 2));
    return true;
}

void MidiDecoder::onMessage(const uint8_t* bytes, size_t len) {
    if (len == 0) return;

    // SysEx: F0 7D <type> <deck> <packed fingerprint> F7
    if (bytes[0] == 0xF0) {
        if (len < 2 || bytes[1] != 0x7D) return;       // not our manufacturer id
        size_t end = len;
        if (end > 0 && bytes[end - 1] == 0xF7) --end;   // strip trailing F7
        const uint8_t* body = bytes + 2;                // skip F0 7D
        size_t blen = (end > 2) ? end - 2 : 0;
        int msgType = 0, deck = 0;
        std::vector<uint8_t> payload;
        if (!unpackSysex(body, blen, msgType, deck, payload)) return;
        BridgeEvent e;
        e.target  = (deck >= 1 && deck <= 4) ? Target::Deck : Target::Sampler;
        e.channel = deck;
        e.deck    = deck;
        if (msgType == 0x01) {
            if (!parseFingerprint(payload, e.fp)) return;
            e.type = BridgeEventType::TrackIdentity;
        } else if (msgType == 0x02) {
            e.type = BridgeEventType::TrackCleared;
        } else {
            return;  // unknown/future sub-type
        }
        sink_(e);
        return;
    }

    // Channel Voice: high nibble = message, low nibble = channel (0-based).
    if (bytes[0] >= 0x80 && bytes[0] < 0xF0) {
        uint8_t d1 = len > 1 ? bytes[1] : 0;
        uint8_t d2 = len > 2 ? bytes[2] : 0;
        onChannelVoice(bytes[0], d1, d2);
    }
}

void MidiDecoder::onChannelVoice(uint8_t status, uint8_t d1, uint8_t d2) {
    const uint8_t kind = status & 0xF0;
    const int channel  = (status & 0x0F) + 1;   // 1..16

    BridgeEvent e;
    e.channel = channel;

    // Channel role (SPEC §3.3.1).
    if (channel >= 1 && channel <= 4) { e.target = Target::Deck; e.deck = channel; }
    else if (channel >= 5 && channel <= 8) {
        e.target = Target::Sampler;
        // sampler index = (bank-1)*16 + (note/cc low nibble)+1 (§3.3.3)
        e.deck = (channel - 5) * 16 + (d1 & 0x0F) + 1;
    } else if (channel == 16) { e.target = Target::Master; }
    else { e.target = Target::None; }

    const bool isNote = (kind == 0x90 || kind == 0x80);
    const bool noteOn = (kind == 0x90 && d2 > 0);

    if (e.target == Target::Deck && isNote) {
        switch (d1) {
            case 0x10: e.type = BridgeEventType::Play;         e.value = noteOn; break;
            case 0x11: e.type = BridgeEventType::TrackLoaded;  e.value = noteOn; break;
            case 0x12: e.type = BridgeEventType::CueIndicator; e.value = noteOn; break;
            case 0x13: e.type = BridgeEventType::SyncEnabled;  e.value = noteOn; break;
            case 0x14: e.type = BridgeEventType::Keylock;      e.value = noteOn; break;
            case 0x18: e.type = BridgeEventType::LoopEnabled;  e.value = noteOn; break;
            default:
                if (d1 >= 0x21 && d1 <= 0x28) {     // hotcue_N_activate
                    e.type = BridgeEventType::HotcueActivate;
                    e.index = d1 - 0x20;
                    e.value = noteOn;
                } else if (d1 >= 0x31 && d1 <= 0x38) { // hotcue_N_enabled
                    e.type = BridgeEventType::HotcueEnabled;
                    e.index = d1 - 0x30;
                    e.value = noteOn;
                } else return;
        }
        sink_(e);
        return;
    }

    if (e.target == Target::Deck && kind == 0xB0) {     // CC
        switch (d1) {
            case 0x10: e.type = BridgeEventType::Bpm;
                       e.value = kBpmMin + (d2 / 127.0) * (kBpmMax - kBpmMin); break;
            case 0x11: e.type = BridgeEventType::Rate;         e.value = d2 / 127.0; break;
            case 0x12: posMsb_[channel] = d2; return;          // 14-bit position MSB; wait for LSB
            case 0x32: {                                        // 14-bit position LSB -> emit
                int v14 = (static_cast<int>(posMsb_[channel]) << 7) | d2;
                e.type = BridgeEventType::PlayPosition;
                e.value = v14 / 16383.0;
                break;
            }
            case 0x16: e.type = BridgeEventType::VuMeter;      e.value = d2 / 127.0; break;
            default: return;
        }
        sink_(e);
        return;
    }

    if (e.target == Target::Master && kind == 0xB0) {
        switch (d1) {
            case 0x10: e.type = BridgeEventType::Crossfader; e.value = (d2 / 127.0) * 2.0 - 1.0; break;
            case 0x13: e.type = BridgeEventType::MasterBpm;
                       e.value = kBpmMin + (d2 / 127.0) * (kBpmMax - kBpmMin); break;
            default: return;
        }
        sink_(e);
        return;
    }

    if (e.target == Target::Sampler && isNote) {
        // §3.3.3: 0x00+N play, 0x10+N track_loaded
        if (d1 <= 0x0F)      { e.type = BridgeEventType::Play;        e.value = noteOn; }
        else if (d1 >= 0x10 && d1 <= 0x1F) { e.type = BridgeEventType::TrackLoaded; e.value = noteOn; }
        else return;
        sink_(e);
        return;
    }
}

}  // namespace mxb
