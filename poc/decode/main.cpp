// Mixxx Studio Bridge PoC — Stage 1.5: C++ console decoder (no device).
//
// Proves the sqlite3 + zlib + hand-rolled-protobuf chain compiles and decodes
// correctly on Windows BEFORE we wire it to the displays. Its numbers should
// match poc/waveform-inspect/inspect_waveform.py exactly (visual_sample_rate,
// channels, frame count, duration).
//
//   waveform_decode "C:\Users\<you>\AppData\Local\Mixxx"
//
// Prints the analyzed tracks, decodes the first, and reports stats + an ASCII
// sparkline of the amplitude envelope as a sanity check.

#include <cstdio>
#include <string>
#include <vector>

#include "mixxxdb.hpp"
#include "waveform.hpp"

static std::string defaultMixxxDir() {
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA"))
        return std::string(la) + "\\Mixxx";
    if (const char* ra = std::getenv("APPDATA"))
        return std::string(ra) + "\\Mixxx";
#endif
    return ".";
}

static void sparkline(const std::vector<uint8_t>& mono, int cols) {
    static const char* ramp = " .:-=+*#%@";
    if (mono.empty() || cols <= 0) return;
    std::string line;
    line.reserve(cols);
    size_t per = mono.size() / static_cast<size_t>(cols);
    if (per == 0) per = 1;
    for (int c = 0; c < cols; ++c) {
        size_t start = static_cast<size_t>(c) * per;
        size_t endi = start + per;
        if (endi > mono.size()) endi = mono.size();
        int peak = 0;
        for (size_t i = start; i < endi; ++i)
            if (mono[i] > peak) peak = mono[i];
        int idx = peak * 9 / 255;  // 0..9
        line += ramp[idx];
    }
    std::printf("    |%s|\n", line.c_str());
}

int main(int argc, char** argv) {
    std::string mixxx = (argc > 1) ? argv[1] : defaultMixxxDir();
    std::printf("Mixxx dir: %s\n", mixxx.c_str());

    std::vector<mxb::TrackRow> tracks;
    std::string err;
    if (!mxb::queryAnalyzedTracks(mixxx, 5, tracks, err)) {
        std::printf("DB error: %s\n", err.c_str());
        return 1;
    }
    if (tracks.empty()) {
        std::printf("No analyzed (type=1) tracks found. Analyze one in Mixxx.\n");
        return 1;
    }

    std::printf("\nAnalyzed tracks:\n");
    for (auto& t : tracks)
        std::printf("  id=%-5d %-14s %s - %s\n", t.analysis_id,
                    t.version.c_str(), t.artist.c_str(), t.title.c_str());

    const mxb::TrackRow& t = tracks.front();
    std::string blob = mixxx;
    if (!blob.empty() && blob.back() != '/' && blob.back() != '\\') blob += '\\';
    blob += "analysis\\" + std::to_string(t.analysis_id);

    std::printf("\nDecoding: %s  (%s - %s)\n", blob.c_str(),
                t.artist.c_str(), t.title.c_str());

    mxb::Waveform wf;
    if (!mxb::decodeWaveformFile(blob, wf, err)) {
        std::printf("decode error: %s\n", err.c_str());
        return 1;
    }

    std::printf("  visual_sample_rate: %.3f\n", wf.visual_sample_rate);
    std::printf("  audio_visual_ratio: %.6f\n", wf.audio_visual_ratio);
    std::printf("  channels:           %d\n", wf.channels);
    std::printf("  mono frames:        %zu\n", wf.mono.size());
    std::printf("  duration:           %.1f s\n", wf.durationSeconds());
    std::printf("  amplitude envelope (whole track):\n");
    sparkline(wf.mono, 100);

    std::printf("\nOK — C++ decode chain matches the Python inspector if the\n");
    std::printf("sample rate / channels / frame count line up.\n");
    return 0;
}
