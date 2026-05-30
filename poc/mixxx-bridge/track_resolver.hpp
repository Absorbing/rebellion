// Mixxx Studio Bridge — resolve a track file path to its analysed waveform.
//
// SPEC §3.4 lookup flow: Mixxx sends the file path on track_loaded → we resolve
// path -> library.id -> newest detailed (type=1) analysis id -> decode the blob.
// Read-only + immutable open, safe while Mixxx runs (WAL); mirrors mixxxdb.cpp.

#pragma once
#include <string>

#include "waveform.hpp"

namespace mxb {

struct ResolvedTrack {
    int         library_id  = 0;
    int         analysis_id = 0;
    std::string artist;
    std::string title;
    Waveform    waveform;
};

// Resolve and decode in one call. `mixxx_dir` is the data root (holds
// mixxxdb.sqlite and analysis/). `path` is the audio file location as Mixxx
// reports it (track_locations.location). Returns false + err on miss/decode fail.
bool resolveTrackByPath(const std::string& mixxx_dir, const std::string& path,
                        ResolvedTrack& out, std::string& err);

// Lower-level: path -> library.id + newest analysis_id + artist/title, no decode.
bool lookupAnalysisForPath(const std::string& mixxx_dir, const std::string& path,
                           int& library_id, int& analysis_id,
                           std::string& artist, std::string& title, std::string& err);

}  // namespace mxb
