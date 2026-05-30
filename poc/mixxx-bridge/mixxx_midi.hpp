// Mixxx Studio Bridge — Mixxx → daemon MIDI decode (SPEC §3.3, §3.4)
//
// Pure logic, no I/O: feed it raw MIDI messages (as RtMidi delivers them) and it
// emits typed BridgeEvents. The RtMidi/loopMIDI listener is a thin shell on top
// (see mixxx_listener) — keeping decode separate makes it unit-testable off the
// hardware and keeps the device path out of the hot decode loop.
//
// Two message families arrive on the Mixxx-State port:
//   * Channel Voice (Note/CC) — per-deck / sampler / master state (§3.3 tables)
//   * SysEx F0 7D ...         — track file path on track_loaded (§3.4)

#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mxb {

// What a decoded message means to the bridge. One MIDI message -> one event.
enum class BridgeEventType {
    Unknown,
    TrackPath,     // SysEx 0x01: deck got a track; `text` = UTF-8 path
    TrackCleared,  // SysEx 0x02: deck unloaded
    Play,          // bool in `value` (0/1)
    TrackLoaded,   // bool
    CueIndicator,  // bool
    SyncEnabled,   // bool
    Keylock,       // bool
    LoopEnabled,   // bool
    HotcueActivate,// momentary; `index` = hotcue N (1..8)
    HotcueEnabled, // bool; `index` = hotcue N
    Bpm,           // float BPM in `value` (already un-mapped from 0..127)
    Rate,          // float, raw 0..127 normalised to 0..1 in `value`
    PlayPosition,  // float 0..1
    VuMeter,       // float 0..1
    Crossfader,    // float -1..+1 (master)
    MasterBpm,     // float (internal clock bpm)
};

// MIDI channel role (SPEC §3.3.1). 1-indexed channels map to these.
enum class Target { None, Deck, Sampler, Master };

struct BridgeEvent {
    BridgeEventType type = BridgeEventType::Unknown;
    Target  target = Target::None;
    int     channel = 0;     // raw 1..16
    int     deck    = 0;     // 1..4 for Deck target; sampler index 1..64 for Sampler
    int     index   = 0;     // hotcue / sub-index where relevant
    double  value   = 0.0;   // numeric payload (already converted to real units)
    std::string text;        // path for TrackPath
};

// BPM CC mapping (SPEC §3.3.2): bpm sent as value over a 60..187.5 range across
// 0..127. These mirror the <minimum>/<maximum> in the Mixxx mapping (§11.3).
constexpr double kBpmMin = 60.0;
constexpr double kBpmMax = 187.5;

class MidiDecoder {
public:
    using Sink = std::function<void(const BridgeEvent&)>;

    explicit MidiDecoder(Sink sink) : sink_(std::move(sink)) {}

    // Feed one complete MIDI message (one RtMidi callback delivery). Channel
    // Voice messages are 2-3 bytes; SysEx is F0..F7. Emits 0..1 events.
    void onMessage(const uint8_t* bytes, size_t len);
    void onMessage(const std::vector<uint8_t>& m) { onMessage(m.data(), m.size()); }

    // Decode a track-path SysEx body (the bytes between F0 7D and F7, i.e.
    // starting at <msg_type>). Exposed for unit testing the 7-bit unpack.
    // Returns true and fills type/deck/path on a well-formed message.
    static bool decodeTrackPathSysex(const uint8_t* body, size_t len,
                                     int& msgType, int& deck, std::string& path);

private:
    void onChannelVoice(uint8_t status, uint8_t d1, uint8_t d2);
    Sink sink_;
};

}  // namespace mxb
