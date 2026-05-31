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
    TrackIdentity, // SysEx 0x01: deck got a track; `fp` = numeric fingerprint
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

// Numeric track fingerprint (replaces the SPEC §3.4 file-path mechanism, which
// is infeasible: Mixxx controller scripting exposes no string controls and has
// no `file_path` control — confirmed against the 2.4 control reference). These
// are all read-only numeric [ChannelN] controls Mixxx *does* expose on load; the
// bridge matches them against library rows (track_resolver). `samples` is the
// near-unique key (exact audio sample count); the rest disambiguate/corroborate.
struct TrackFingerprint {
    uint32_t samples    = 0;  // [ChannelN],track_samples
    uint32_t samplerate = 0;  // [ChannelN],track_samplerate
    uint32_t durationMs = 0;  // [ChannelN],duration * 1000
    uint16_t bpmCenti   = 0;  // [ChannelN],file_bpm * 100
    bool empty() const { return samples == 0 && samplerate == 0; }
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
    TrackFingerprint fp;     // populated for TrackIdentity
};

// BPM CC mapping (SPEC §3.3.2): bpm sent as value over a 60..187.5 range across
// 0..127. These mirror the <minimum>/<maximum> in the Mixxx mapping (§11.3).
constexpr double kBpmMin = 60.0;
constexpr double kBpmMax = 187.5;

// Raw byte length of the fingerprint payload carried inside the identity SysEx,
// before 7-bit packing: samples(4) + samplerate(3) + durationMs(4) + bpm(2),
// all big-endian. The Mixxx-side encoder (the .scripts.js) must match this.
constexpr size_t kFingerprintBytes = 13;

class MidiDecoder {
public:
    using Sink = std::function<void(const BridgeEvent&)>;

    explicit MidiDecoder(Sink sink) : sink_(std::move(sink)) {}

    // Feed one complete MIDI message (one RtMidi callback delivery). Channel
    // Voice messages are 2-3 bytes; SysEx is F0..F7. Emits 0..1 events.
    void onMessage(const uint8_t* bytes, size_t len);
    void onMessage(const std::vector<uint8_t>& m) { onMessage(m.data(), m.size()); }

    // Reverse the 7-bit MSB-pack (SPEC §3.4 transport): `body` runs from
    // <msg_type> to just before F7. Fills msgType/deck and the unpacked payload
    // bytes. Exposed for unit testing the pack. Returns false if too short.
    static bool unpackSysex(const uint8_t* body, size_t len,
                            int& msgType, int& deck, std::vector<uint8_t>& payload);

    // Interpret an unpacked identity payload (kFingerprintBytes, big-endian).
    static bool parseFingerprint(const std::vector<uint8_t>& payload,
                                 TrackFingerprint& out);

private:
    void onChannelVoice(uint8_t status, uint8_t d1, uint8_t d2);
    Sink sink_;
    // 14-bit play position arrives as CC 0x12 (MSB) then CC 0x32 (LSB) per
    // channel; stash the MSB until the LSB completes it. Index by 1..16 channel.
    uint8_t posMsb_[17] = {0};
};

}  // namespace mxb
