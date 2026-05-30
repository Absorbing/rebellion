// Mixxx Studio Bridge PoC — waveform decode library (header)
//
// Decodes a Mixxx analysis blob (SPEC §3.2) into a per-frame mono amplitude
// envelope. Wire format confirmed empirically via poc/waveform-inspect:
//   file = [4-byte big-endian uncompressed length][zlib stream]
//   inflated = protobuf Waveform:
//     field 1 (double) visual_sample_rate
//     field 2 (double) audio_visual_ratio
//     field 3 (len)    signal_all  = Signal sub-message:
//                        field 1 (repeated varint) samples 0..255, interleaved
//                        field 2 (varint)          channels (=2 stereo)
//                        field 3 (varint)          units/flag
//     field 4 (len)    signal_filtered { low, mid, high } (not needed here)

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mxb {

struct Waveform {
    double visual_sample_rate = 0.0;  // visual samples per second of audio
    double audio_visual_ratio = 0.0;  // audio samples per visual sample
    int    channels = 0;              // from signal_all (2 = stereo)
    // Per-frame mono amplitude 0..255 (downmix = max over channels).
    std::vector<uint8_t> mono;

    double durationSeconds() const {
        return visual_sample_rate > 0.0
                   ? static_cast<double>(mono.size()) / visual_sample_rate
                   : 0.0;
    }
};

// Decode from an analysis blob file. Returns false and sets err on failure.
bool decodeWaveformFile(const std::string& path, Waveform& out, std::string& err);

// Decode from already-read raw blob bytes.
bool decodeWaveformBytes(const uint8_t* data, size_t len, Waveform& out,
                         std::string& err);

}  // namespace mxb
