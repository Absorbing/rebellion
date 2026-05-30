// Mixxx Studio Bridge — resolve a loaded track to its analysed waveform.
//
// Identity is a numeric fingerprint, not a path (SPEC §3.4's path mechanism is
// infeasible — Mixxx exposes no `file_path` control). Mixxx sends, on load, the
// read-only [ChannelN] numerics track_samples / track_samplerate / duration /
// file_bpm; we match those against `library` rows:
//
//   samples ≈ duration * samplerate * channels   (per-deck control vs stored cols)
//
// then take the newest detailed (type=1) analysis for that library.id and decode
// its blob. Read-only + immutable open, safe while Mixxx runs (WAL).

#pragma once
#include <string>

#include "mixxx_midi.hpp"   // TrackFingerprint
#include "waveform.hpp"

namespace mxb {

struct ResolvedTrack {
    int         library_id  = 0;
    int         analysis_id = 0;
    std::string artist;
    std::string title;
    std::string location;     // for logging/diagnostics only
    double      bpm = 0.0;    // library.bpm (authoritative; reflects user edits)
    Waveform    waveform;
};

// Resolve + decode in one call. Returns false + err on no match / decode fail.
bool resolveTrackByFingerprint(const std::string& mixxx_dir,
                               const TrackFingerprint& fp,
                               ResolvedTrack& out, std::string& err);

// Lower-level: fingerprint -> library.id + analysis_id + artist/title/location.
bool lookupAnalysisForFingerprint(const std::string& mixxx_dir,
                                  const TrackFingerprint& fp,
                                  int& library_id, int& analysis_id,
                                  std::string& artist, std::string& title,
                                  std::string& location, double& bpm,
                                  std::string& err);

}  // namespace mxb
