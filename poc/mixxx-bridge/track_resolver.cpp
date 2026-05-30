// Mixxx Studio Bridge — track path -> analysed waveform (impl). SPEC §3.4.

#include "track_resolver.hpp"

#include <sqlite3.h>

namespace mxb {

static std::string joinPath(const std::string& dir, const std::string& leaf) {
    std::string p = dir;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/';
    p += leaf;
    return p;
}

bool lookupAnalysisForPath(const std::string& mixxx_dir, const std::string& path,
                           int& library_id, int& analysis_id,
                           std::string& artist, std::string& title, std::string& err) {
    const std::string uri = "file:" + joinPath(mixxx_dir, "mixxxdb.sqlite") +
                            "?mode=ro&immutable=1";
    sqlite3* h = nullptr;
    if (sqlite3_open_v2(uri.c_str(), &h, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI,
                        nullptr) != SQLITE_OK) {
        err = std::string("sqlite open failed: ") + (h ? sqlite3_errmsg(h) : "?");
        sqlite3_close(h);
        return false;
    }

    // path -> library row (§3.4), then its newest detailed analysis (§3.2 order).
    const char* sql =
        "SELECT l.id, COALESCE(l.artist,''), COALESCE(l.title,''), "
        "       ta.id "
        "FROM track_locations tl "
        "JOIN library l          ON l.location = tl.id "
        "JOIN track_analysis ta  ON ta.track_id = l.id AND ta.type = 1 "
        "WHERE tl.location = ? "
        "ORDER BY (ta.version = 'Waveform-6.1') DESC, ta.id DESC "
        "LIMIT 1;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(h, sql, -1, &st, nullptr) != SQLITE_OK) {
        err = std::string("sqlite prepare failed: ") + sqlite3_errmsg(h);
        sqlite3_close(h);
        return false;
    }
    sqlite3_bind_text(st, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        library_id  = sqlite3_column_int(st, 0);
        auto txt = [&](int c) {
            const unsigned char* s = sqlite3_column_text(st, c);
            return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
        };
        artist = txt(1);
        title  = txt(2);
        analysis_id = sqlite3_column_int(st, 3);
        found = true;
    }
    sqlite3_finalize(st);
    sqlite3_close(h);

    if (!found) {
        err = "no analysed track for path: " + path;
        return false;
    }
    return true;
}

bool resolveTrackByPath(const std::string& mixxx_dir, const std::string& path,
                        ResolvedTrack& out, std::string& err) {
    if (!lookupAnalysisForPath(mixxx_dir, path, out.library_id, out.analysis_id,
                               out.artist, out.title, err))
        return false;

    const std::string blob = joinPath(joinPath(mixxx_dir, "analysis"),
                                       std::to_string(out.analysis_id));
    if (!decodeWaveformFile(blob, out.waveform, err)) return false;
    return true;
}

}  // namespace mxb
