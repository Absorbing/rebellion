// Mixxx Studio Bridge PoC — mixxxdb reader (header)
// Reads mixxxdb.sqlite (SPEC §3.1) read-only, even while Mixxx runs (WAL).

#pragma once
#include <string>
#include <vector>

namespace mxb {

struct TrackRow {
    int         analysis_id = 0;   // == filename in analysis/
    int         track_id = 0;
    std::string version;           // e.g. "Waveform-6.1"
    std::string artist;
    std::string title;
    std::string location;          // full audio file path
};

// Open <mixxx_dir>/mixxxdb.sqlite read-only and return up to `limit` tracks
// that have a detailed (type=1) waveform analysis, newest-format first.
// Returns false + err on failure.
bool queryAnalyzedTracks(const std::string& mixxx_dir, int limit,
                         std::vector<TrackRow>& out, std::string& err);

}  // namespace mxb
