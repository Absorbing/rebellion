// Mixxx Studio Bridge — v0.0 Test 0.0.4 (SPEC §0 Session 4)
//
// Confirms whether engine.getValue("[ChannelN]", "file_path") returns a usable
// path on this Mixxx version. The SPEC's track-identification mechanism (§3.4)
// depends on it; if it returns undefined/empty we need the SQLite fallback.
//
// Usage:
//   1. Copy probe.js + probe.midi.xml to %APPDATA%\Mixxx\controllers\
//   2. Mixxx Preferences -> Controllers -> enable "Studio Bridge file_path probe"
//   3. Load a track on deck 1 (and optionally 2-4), then check Help -> Logs.
//   4. Record PASS/FAIL + Mixxx version in V00_RESULTS.md.

var Probe = {};

Probe.init = function (id, debug) {
    print("PROBE> init, controller id=" + id);

    // Probe immediately for anything already loaded...
    Probe.report();

    // ...and again whenever a track loads on any of the four decks.
    Probe.connections = [];
    for (var i = 1; i <= 4; i++) {
        var grp = "[Channel" + i + "]";
        Probe.connections.push(
            engine.makeConnection(grp, "track_loaded", Probe.onTrackLoaded)
        );
    }
};

Probe.onTrackLoaded = function (value, group, control) {
    if (value > 0) {
        var path = engine.getValue(group, "file_path");
        print("PROBE> " + group + " track_loaded; file_path returned: " +
              Probe.describe(path));
    }
};

Probe.report = function () {
    for (var i = 1; i <= 4; i++) {
        var grp = "[Channel" + i + "]";
        if (engine.getValue(grp, "track_loaded") > 0) {
            var path = engine.getValue(grp, "file_path");
            print("PROBE> (initial) " + grp + " file_path returned: " +
                  Probe.describe(path));
        }
    }
};

Probe.describe = function (path) {
    if (path === undefined) return "<undefined>  (FAIL: control not exposed)";
    if (path === null)      return "<null>       (FAIL)";
    if (path === "")        return "<empty>      (FAIL)";
    return "\"" + path + "\"  (PASS)";
};

Probe.shutdown = function () {
    if (Probe.connections) {
        for (var i = 0; i < Probe.connections.length; i++) {
            if (Probe.connections[i]) Probe.connections[i].disconnect();
        }
    }
};
