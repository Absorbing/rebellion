// Mixxx Studio Bridge PoC — mixxxdb reader (impl)

#include "mixxxdb.hpp"

#include <sqlite3.h>

namespace mxb {

bool queryAnalyzedTracks(const std::string& mixxx_dir, int limit,
                         std::vector<TrackRow>& out, std::string& err) {
    std::string db = mixxx_dir;
    if (!db.empty() && db.back() != '/' && db.back() != '\\') db += '/';
    db += "mixxxdb.sqlite";

    // Read-only + immutable so we never block or get blocked by a running Mixxx.
    std::string uri = "file:" + db + "?mode=ro&immutable=1";

    sqlite3* h = nullptr;
    int rc = sqlite3_open_v2(uri.c_str(), &h,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
    if (rc != SQLITE_OK) {
        err = std::string("sqlite open failed: ") + sqlite3_errmsg(h);
        sqlite3_close(h);
        return false;
    }

    // SPEC §3.2 lookup, prefer detailed + newest-format waveforms.
    const char* sql =
        "SELECT ta.id, ta.track_id, ta.version, "
        "       COALESCE(l.artist,''), COALESCE(l.title,''), "
        "       COALESCE(tl.location,'') "
        "FROM track_analysis ta "
        "JOIN library l          ON l.id = ta.track_id "
        "JOIN track_locations tl ON tl.id = l.location "
        "WHERE ta.type = 1 "
        "ORDER BY (ta.version = 'Waveform-6.1') DESC, ta.id DESC "
        "LIMIT ?;";

    sqlite3_stmt* st = nullptr;
    rc = sqlite3_prepare_v2(h, sql, -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        err = std::string("sqlite prepare failed: ") + sqlite3_errmsg(h);
        sqlite3_close(h);
        return false;
    }
    sqlite3_bind_int(st, 1, limit);

    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        TrackRow r;
        r.analysis_id = sqlite3_column_int(st, 0);
        r.track_id    = sqlite3_column_int(st, 1);
        auto txt = [&](int c) {
            const unsigned char* s = sqlite3_column_text(st, c);
            return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
        };
        r.version  = txt(2);
        r.artist   = txt(3);
        r.title    = txt(4);
        r.location = txt(5);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    sqlite3_close(h);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        err = "sqlite step error";
        return false;
    }
    return true;
}

bool queryLibraryTracks(const std::string& mixxx_dir, int limit,
                        std::vector<LibRow>& out, std::string& err) {
    std::string db = mixxx_dir;
    if (!db.empty() && db.back() != '/' && db.back() != '\\') db += '/';
    db += "mixxxdb.sqlite";
    std::string uri = "file:" + db + "?mode=ro&immutable=1";

    sqlite3* h = nullptr;
    if (sqlite3_open_v2(uri.c_str(), &h, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI,
                        nullptr) != SQLITE_OK) {
        err = std::string("sqlite open failed: ") + (h ? sqlite3_errmsg(h) : "?");
        sqlite3_close(h);
        return false;
    }

    // Visible, present tracks ordered by Artist then Title — mirrors a Mixxx
    // library sorted by Artist so the on-device cursor can lock-step it.
    const char* sql =
        "SELECT l.id, COALESCE(l.artist,''), COALESCE(l.title,'') "
        "FROM library l "
        "JOIN track_locations tl ON tl.id = l.location "
        "WHERE COALESCE(l.mixxx_deleted,0)=0 AND COALESCE(tl.fs_deleted,0)=0 "
        "ORDER BY l.artist COLLATE NOCASE, l.title COLLATE NOCASE "
        "LIMIT ?;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(h, sql, -1, &st, nullptr) != SQLITE_OK) {
        err = std::string("sqlite prepare failed: ") + sqlite3_errmsg(h);
        sqlite3_close(h);
        return false;
    }
    sqlite3_bind_int(st, 1, limit);

    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        LibRow r;
        r.id = sqlite3_column_int(st, 0);
        auto txt = [&](int c) {
            const unsigned char* s = sqlite3_column_text(st, c);
            return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
        };
        r.artist = txt(1);
        r.title  = txt(2);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    sqlite3_close(h);
    if (rc != SQLITE_DONE) { err = "sqlite step error"; return false; }
    return true;
}

}  // namespace mxb
