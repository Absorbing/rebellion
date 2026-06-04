// Mixxx Studio Bridge PoC — waveform decode library (impl)
// See waveform.hpp for the on-disk format. Protobuf is hand-parsed (the wire
// format is fully known), so the only external dep is zlib for inflate.

#include "waveform.hpp"

#include <cstdio>
#include <cstring>

#include <zlib.h>

namespace mxb {
namespace {

// --- minimal protobuf wire reader -------------------------------------------

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    bool eof() const { return p >= end; }

    uint64_t varint() {
        uint64_t result = 0;
        int shift = 0;
        while (p < end) {
            uint8_t b = *p++;
            result |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return result;
            shift += 7;
            if (shift >= 64) break;
        }
        ok = false;
        return result;
    }

    double fixed64() {
        if (end - p < 8) { ok = false; return 0.0; }
        double d;
        std::memcpy(&d, p, 8);  // little-endian on x86/x64
        p += 8;
        return d;
    }

    // Returns a view [start,len) for a length-delimited field and advances.
    const uint8_t* lenField(uint64_t& outLen) {
        uint64_t n = varint();
        if (!ok || static_cast<uint64_t>(end - p) < n) { ok = false; outLen = 0; return p; }
        const uint8_t* start = p;
        p += n;
        outLen = n;
        return start;
    }

    void skip(uint32_t wire) {
        switch (wire) {
            case 0: varint(); break;
            case 1: if (end - p >= 8) p += 8; else ok = false; break;
            case 5: if (end - p >= 4) p += 4; else ok = false; break;
            case 2: { uint64_t n; lenField(n); break; }
            default: ok = false; break;
        }
    }
};

// Parse a Signal sub-message: field 1 = repeated varint samples (interleaved by
// channels), field 2 = channels. Downmix to per-frame mono (max of channels).
bool parseSignal(const uint8_t* data, uint64_t len, std::vector<uint8_t>& mono,
                 int& channels) {
    Reader r{data, data + len};
    std::vector<uint8_t> interleaved;
    interleaved.reserve(len);  // upper bound
    channels = 0;
    while (!r.eof() && r.ok) {
        uint64_t key = r.varint();
        if (!r.ok) break;
        uint32_t field = static_cast<uint32_t>(key >> 3);
        uint32_t wire = static_cast<uint32_t>(key & 0x07);
        if (field == 1 && wire == 0) {
            uint64_t v = r.varint();
            interleaved.push_back(static_cast<uint8_t>(v > 255 ? 255 : v));
        } else if (field == 1 && wire == 2) {
            // packed repeated (handle just in case Mixxx ever packs them)
            uint64_t n;
            const uint8_t* s = r.lenField(n);
            Reader pr{s, s + n};
            while (!pr.eof() && pr.ok) {
                uint64_t v = pr.varint();
                interleaved.push_back(static_cast<uint8_t>(v > 255 ? 255 : v));
            }
        } else if (field == 2 && wire == 0) {
            channels = static_cast<int>(r.varint());
        } else {
            r.skip(wire);
        }
    }
    if (!r.ok) return false;
    if (channels <= 0) channels = 1;

    // Downmix interleaved -> mono frames (max across channels).
    size_t frames = interleaved.size() / static_cast<size_t>(channels);
    mono.resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        uint8_t m = 0;
        for (int c = 0; c < channels; ++c) {
            uint8_t s = interleaved[f * channels + c];
            if (s > m) m = s;
        }
        mono[f] = m;
    }
    return true;
}

// Parse signal_filtered (FilteredSignal): field 1 = low, 2 = mid, 3 = high, each
// a Signal sub-message; fields 4-7 are cutoff frequencies (skipped). Mixxx colors
// the waveform from these bands (low->R, mid->G, high->B).
void parseFiltered(const uint8_t* data, uint64_t len, Waveform& out) {
    Reader r{data, data + len};
    while (!r.eof() && r.ok) {
        uint64_t key = r.varint();
        if (!r.ok) break;
        uint32_t field = static_cast<uint32_t>(key >> 3);
        uint32_t wire = static_cast<uint32_t>(key & 0x07);
        if (wire == 2 && (field == 1 || field == 2 || field == 3)) {
            uint64_t n;
            const uint8_t* s = r.lenField(n);
            if (!r.ok) break;
            int ch = 0;
            if (field == 1)      parseSignal(s, n, out.low,  ch);
            else if (field == 2) parseSignal(s, n, out.mid,  ch);
            else                 parseSignal(s, n, out.high, ch);
        } else {
            r.skip(wire);
        }
    }
}

}  // namespace

bool decodeWaveformBytes(const uint8_t* data, size_t len, Waveform& out,
                         std::string& err) {
    if (len < 5) { err = "blob too small"; return false; }

    // qCompress header: 4-byte big-endian uncompressed length, then zlib.
    uint32_t declared = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                        (uint32_t(data[2]) << 8) | uint32_t(data[3]);

    std::vector<uint8_t> inflated(declared);
    uLongf destLen = declared;
    int zr = uncompress(inflated.data(), &destLen, data + 4,
                        static_cast<uLong>(len - 4));
    if (zr != Z_OK) {
        err = "zlib uncompress failed (code " + std::to_string(zr) + ")";
        return false;
    }
    if (destLen != declared) {
        err = "inflated size " + std::to_string(destLen) +
              " != declared " + std::to_string(declared);
        return false;
    }

    // Parse top-level Waveform message.
    Reader r{inflated.data(), inflated.data() + destLen};
    bool gotSignalAll = false;
    while (!r.eof() && r.ok) {
        uint64_t key = r.varint();
        if (!r.ok) break;
        uint32_t field = static_cast<uint32_t>(key >> 3);
        uint32_t wire = static_cast<uint32_t>(key & 0x07);
        if (field == 1 && wire == 1) {
            out.visual_sample_rate = r.fixed64();
        } else if (field == 2 && wire == 1) {
            out.audio_visual_ratio = r.fixed64();
        } else if (field == 3 && wire == 2) {  // signal_all
            uint64_t n;
            const uint8_t* s = r.lenField(n);
            if (!r.ok) break;
            if (!parseSignal(s, n, out.mono, out.channels)) {
                err = "failed to parse signal_all";
                return false;
            }
            gotSignalAll = true;
        } else if (field == 4 && wire == 2) {  // signal_filtered (low/mid/high)
            uint64_t n;
            const uint8_t* s = r.lenField(n);
            if (!r.ok) break;
            parseFiltered(s, n, out);  // optional: colour data, ignore failures
        } else {
            r.skip(wire);
        }
    }
    if (!gotSignalAll) { err = "signal_all (field 3) not found"; return false; }
    return true;
}

bool decodeWaveformFile(const std::string& path, Waveform& out,
                        std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); err = "empty file"; return false; }
    std::vector<uint8_t> raw(static_cast<size_t>(sz));
    size_t rd = std::fread(raw.data(), 1, raw.size(), f);
    std::fclose(f);
    if (rd != raw.size()) { err = "short read"; return false; }
    return decodeWaveformBytes(raw.data(), raw.size(), out, err);
}

}  // namespace mxb
