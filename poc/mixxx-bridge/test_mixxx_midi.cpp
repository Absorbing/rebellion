// Unit tests for the Mixxx MIDI/SysEx decode + deck model. No hardware, no DB.
// Built and run on any platform; this is the off-device verification the SPEC's
// approach depends on (decode is pure logic).

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "mixxx_midi.hpp"
#include "deck_state.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_fail; } } while (0)

// Mirror of the Mixxx-side encoder (.scripts.js StudioBridge.encodeId): build the
// F0 7D <type> <deck> <MSB-packed 13-byte fingerprint> F7 stream. The 13-byte
// payload is big-endian: samples(4) samplerate(3) durationMs(4) bpmCenti(2).
static std::vector<uint8_t> packPayload(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < raw.size(); i += 7) {
        uint8_t msb = 0;
        for (size_t j = 0; j < 7 && i + j < raw.size(); ++j)
            if (raw[i + j] & 0x80) msb |= (1 << j);
        out.push_back(msb);
        for (size_t j = 0; j < 7 && i + j < raw.size(); ++j)
            out.push_back(raw[i + j] & 0x7F);
    }
    return out;
}
static std::vector<uint8_t> encodeIdSysex(int deck, const TrackFingerprint& fp) {
    auto be = [](std::vector<uint8_t>& v, uint32_t x, int n) {
        for (int k = n - 1; k >= 0; --k) v.push_back((x >> (8 * k)) & 0xFF);
    };
    std::vector<uint8_t> raw;
    be(raw, fp.samples, 4); be(raw, fp.samplerate, 3);
    be(raw, fp.durationMs, 4); be(raw, fp.bpmCenti, 2);
    std::vector<uint8_t> msg = {0xF0, 0x7D, 0x01, static_cast<uint8_t>(deck)};
    auto packed = packPayload(raw);
    msg.insert(msg.end(), packed.begin(), packed.end());
    msg.push_back(0xF7);
    return msg;
}

int main() {
    // --- 1. 7-bit unpack: the SPEC §3.4 worked example -----------------------
    // Input [A1 B2 C3 04 05 06 07 08] -> MSB 0x07, data [21 32 43 04 05 06 07] + [08].
    {
        const uint8_t body[] = {0x01, 0x01,             // msgType=1, deck=1
                                0x07, 0x21,0x32,0x43,0x04,0x05,0x06,0x07,  // group 1
                                0x00, 0x08};            // group 2: msb=0, one byte 0x08
        int t=0, d=0; std::vector<uint8_t> p;
        bool ok = MidiDecoder::unpackSysex(body, sizeof(body), t, d, p);
        CHECK(ok, "worked-example unpack returns true");
        CHECK(t == 1 && d == 1, "worked-example type/deck");
        const uint8_t want[] = {0xA1,0xB2,0xC3,0x04,0x05,0x06,0x07,0x08};
        CHECK(p.size() == 8, "worked-example length");
        bool match = p.size() == 8;
        for (size_t i = 0; i < p.size() && i < 8; ++i)
            if (p[i] != want[i]) match = false;
        CHECK(match, "worked-example bytes reconstruct A1 B2 C3 04..08");
    }

    // --- 2. Round-trip fingerprints through encode -> decode -----------------
    {
        TrackFingerprint fps[] = {
            {0, 0, 0, 0},                              // zero (high-bit-free)
            {10584000, 44100, 240000, 12800},          // 4:00 @44.1k, 128.00 bpm
            {0xFFFFFFFF, 192000, 0xFFFFFFFF, 0xFFFF},  // all bits set (stresses MSB pack)
            {529200, 48000, 11025, 17499},             // odd values
        };
        for (const auto& fp : fps) {
            auto msg = encodeIdSysex(3, fp);
            TrackFingerprint got;
            bool sawIdentity = false;
            MidiDecoder dec([&](const BridgeEvent& e) {
                if (e.type == BridgeEventType::TrackIdentity) { got = e.fp; sawIdentity = true; }
            });
            dec.onMessage(msg);
            CHECK(sawIdentity, "identity event emitted");
            CHECK(got.samples == fp.samples && got.samplerate == fp.samplerate &&
                  got.durationMs == fp.durationMs && got.bpmCenti == fp.bpmCenti,
                  "fingerprint round-trips through SysEx pack");
        }
    }

    // --- 3. Channel-voice decode (SPEC §3.3.2) -------------------------------
    {
        std::vector<BridgeEvent> evs;
        MidiDecoder dec([&](const BridgeEvent& e){ evs.push_back(e); });

        dec.onMessage({0x90, 0x10, 0x7F});  // deck1 play on
        dec.onMessage({0xB0, 0x10, 64});    // deck1 bpm CC mid
        dec.onMessage({0xB0, 0x12, 64});    // deck1 playposition ~0.5
        dec.onMessage({0x92, 0x23, 0x7F});  // deck3 hotcue 3 activate
        dec.onMessage({0xBF, 0x10, 127});   // master crossfader full right

        CHECK(evs.size() == 5, "5 channel-voice events decoded");
        CHECK(evs[0].type == BridgeEventType::Play && evs[0].deck == 1 && evs[0].value == 1.0,
              "deck1 play on");
        CHECK(evs[1].type == BridgeEventType::Bpm && evs[1].deck == 1 &&
              evs[1].value > 120.0 && evs[1].value < 125.0, "deck1 bpm ~123");
        CHECK(evs[2].type == BridgeEventType::PlayPosition && evs[2].value > 0.49 &&
              evs[2].value < 0.51, "deck1 position ~0.5");
        CHECK(evs[3].type == BridgeEventType::HotcueActivate && evs[3].deck == 3 &&
              evs[3].index == 3, "deck3 hotcue3");
        CHECK(evs[4].type == BridgeEventType::Crossfader && evs[4].target == Target::Master &&
              evs[4].value > 0.99, "master crossfader +1");
    }

    // --- 4. Sampler channel mapping (SPEC §3.3.3) ----------------------------
    {
        std::vector<BridgeEvent> evs;
        MidiDecoder dec([&](const BridgeEvent& e){ evs.push_back(e); });
        dec.onMessage({0x94, 0x02, 0x7F});  // ch5 (bank1) note 0x02 -> sampler 3 play
        dec.onMessage({0x96, 0x12, 0x7F});  // ch7 (bank3) note 0x12 -> sampler 35 loaded
        CHECK(evs.size() == 2, "2 sampler events");
        CHECK(evs[0].target == Target::Sampler && evs[0].deck == 3, "sampler 3");
        CHECK(evs[1].target == Target::Sampler && evs[1].deck == 35 &&
              evs[1].type == BridgeEventType::TrackLoaded, "sampler 35 loaded");
    }

    // --- 5. Deck model folds events + invokes loader on track identity -------
    {
        int loaderCalls = 0;
        TrackFingerprint seen;
        DeckModel model([&](const TrackFingerprint& fp, std::string& artist,
                            std::string& title, double& bpm, Waveform& wf) {
            ++loaderCalls;
            seen = fp;
            if (fp.empty()) return false;
            artist = "Artist"; title = "Title"; bpm = 124.5;
            wf.visual_sample_rate = 441.0;
            wf.mono.assign(1000, 100);
            return true;
        });

        auto feed = [&](const BridgeEvent& e){ model.apply(e); };
        MidiDecoder dec(feed);

        TrackFingerprint fp{10584000, 44100, 240000, 12800};
        dec.onMessage(encodeIdSysex(1, fp));
        dec.onMessage({0x90, 0x10, 0x7F});  // play on

        const DeckState& d1 = model.deck(1);
        CHECK(loaderCalls == 1, "loader called once on track identity");
        CHECK(seen.samples == fp.samples, "loader received the fingerprint");
        CHECK(d1.loaded && d1.hasWaveform, "deck1 loaded with waveform");
        CHECK(d1.title == "Title", "deck1 title from loader");
        CHECK(d1.bpm == 124.5, "deck1 bpm from loader (DB)");
        CHECK(d1.playing, "deck1 playing after play-on");
        CHECK(d1.waveform.mono.size() == 1000, "deck1 waveform frames");

        // clear: F0 7D 02 <deck> F7
        dec.onMessage({0xF0, 0x7D, 0x02, 0x01, 0xF7});
        CHECK(!model.deck(1).loaded, "deck1 cleared");
    }

    if (g_fail == 0) std::printf("ALL MIXXX-BRIDGE TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
