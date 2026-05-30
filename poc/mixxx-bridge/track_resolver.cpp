// Mixxx Studio Bridge — fingerprint -> analysed waveform (impl). SPEC §3.4 (revised).

#include "track_resolver.hpp"

#include <sqlite3.h>

namespace mxb {

static std::string joinPath(const std::string& dir, const std::string& leaf) {
    std::string p = dir;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/';
    p += leaf;
    return p;
}

bool lookupAnalysisForFingerprint(const std::string& mixxx_dir,
                                  const TrackFingerprint& fp,
                                  int& library_id, int& analysis_id,
                                  std::string& artist, std::string& title,
                                  std::string& location, double& bpm,
                                  std::string& err) {
    if (fp.empty()) { err = "empty fingerprint"; return false; }

    const std::string uri = "file:" + joinPath(mixxx_dir, "mixxxdb.sqlite") +
                            "?mode=ro&immutable=1";
    sqlite3* h = nullptr;
    if (sqlite3_open_v2(uri.c_str(), &h, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI,
                        nullptr) != SQLITE_OK) {
        err = std::string("sqlite open failed: ") + (h ? sqlite3_errmsg(h) : "?");
        sqlite3_close(h);
        return false;
    }

    // Match on samplerate (exact) + duration (within 100ms), then rank by closest
    // exact sample count (duration*samplerate*channels vs reported track_samples),
    // preferring the newest detailed waveform. The duration window keeps the match
    // robust to float rounding while staying near-unique in a real library.
    const double durSec = fp.durationMs / 1000.0;
    const char* sql =
        "SELECT l.id, COALESCE(l.artist,''), COALESCE(l.title,''), "
        "       COALESCE(tl.location,''), COALESCE(l.bpm,0.0), ta.id "
        "FROM library l "
        "JOIN track_analysis ta ON ta.track_id = l.id AND ta.type = 1 "
        "LEFT JOIN track_locations tl ON tl.id = l.location "
        "WHERE l.samplerate = ?1 "
        "  AND abs(l.duration - ?2) < 0.10 "
        "ORDER BY abs(l.duration * l.samplerate * COALESCE(l.channels, 2) - ?3) ASC, "
        "         (ta.version = 'Waveform-6.1') DESC, ta.id DESC "
        "LIMIT 1;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(h, sql, -1, &st, nullptr) != SQLITE_OK) {
        err = std::string("sqlite prepare failed: ") + sqlite3_errmsg(h);
        sqlite3_close(h);
        return false;
    }
    sqlite3_bind_int(st, 1, static_cast<int>(fp.samplerate));
    sqlite3_bind_double(st, 2, durSec);
    sqlite3_bind_double(st, 3, static_cast<double>(fp.samples));

    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        library_id = sqlite3_column_int(st, 0);
        auto txt = [&](int c) {
            const unsigned char* s = sqlite3_column_text(st, c);
            return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
        };
        artist      = txt(1);
        title       = txt(2);
        location    = txt(3);
        bpm         = sqlite3_column_double(st, 4);
        analysis_id = sqlite3_column_int(st, 5);
        found = true;
    }
    sqlite3_finalize(st);
    sqlite3_close(h);

    if (!found) {
        err = "no analysed track matching fingerprint (samples=" +
              std::to_string(fp.samples) + " sr=" + std::to_string(fp.samplerate) +
              " dur=" + std::to_string(durSec) + "s)";
        return false;
    }
    return true;
}

bool resolveTrackByFingerprint(const std::string& mixxx_dir,
                               const TrackFingerprint& fp,
                               ResolvedTrack& out, std::string& err) {
    if (!lookupAnalysisForFingerprint(mixxx_dir, fp, out.library_id, out.analysis_id,
                                      out.artist, out.title, out.location, out.bpm, err))
        return false;

    const std::string blob = joinPath(joinPath(mixxx_dir, "analysis"),
                                       std::to_string(out.analysis_id));
    if (!decodeWaveformFile(blob, out.waveform, err)) return false;
    return true;
}

}  // namespace mxb
