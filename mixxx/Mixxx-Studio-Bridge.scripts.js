// Mixxx Studio Bridge — state emitter script (Mixxx -> bridge daemon).
//
// Streams, per deck:
//   * track IDENTITY on load: a numeric fingerprint (samples/samplerate/
//     duration/file_bpm) — NOT the file path, which Mixxx scripting can't read.
//     The bridge matches the fingerprint to a library row and scrapes the rest.
//   * transport state on change: play / sync / loop.
//   * play position: polled at ~20 Hz (a CC connection would be too chatty).
// Plus a periodic heartbeat that re-sends identity so a (re)started bridge
// resyncs without the user reloading tracks.
//
// Wire format documented in Mixxx-Studio-Bridge.midi.xml; the decoder is
// poc/mixxx-bridge/mixxx_midi.cpp. Deck N -> MIDI channel N (status nibble N-1).

var StudioBridge = {};

StudioBridge.DECKS = 4;            // probe up to 4 decks; skip ones that don't exist
StudioBridge.HOTCUES = 8;          // hotcues streamed per deck (markers on the waveform)
StudioBridge.POSITION_MS = 50;     // position poll interval (~20 Hz)
StudioBridge.HEARTBEAT_MS = 2000;  // re-send identity this often

StudioBridge.MANUF = 0x7D;         // non-commercial SysEx id (matches the bridge)
StudioBridge.MSG_IDENTITY = 0x01;
StudioBridge.MSG_CLEARED  = 0x02;
StudioBridge.MSG_HOTCUE   = 0x03;  // realtime cue: [num, enabled, posHi, posLo, r, g, b]

StudioBridge._conns = [];
StudioBridge._posTimer = 0;
StudioBridge._hbTimer = 0;

StudioBridge.init = function (id, debug) {
    print("StudioBridge> init id=" + id);

    for (var n = 1; n <= StudioBridge.DECKS; n++) {
        var grp = "[Channel" + n + "]";
        // Decks beyond the configured count have no track_loaded control.
        if (engine.getValue(grp, "track_loaded") === undefined) continue;

        StudioBridge._connect(grp, "track_loaded", StudioBridge.onTrackLoaded);
        StudioBridge._connect(grp, "play",          StudioBridge.onPlay);
        StudioBridge._connect(grp, "sync_enabled",  StudioBridge.onSync);
        StudioBridge._connect(grp, "loop_enabled",  StudioBridge.onLoop);

        // Stream hotcue edits live: set/clear (status), move (position), recolor.
        for (var h = 1; h <= StudioBridge.HOTCUES; h++) {
            StudioBridge._connect(grp, "hotcue_" + h + "_status",   StudioBridge.onHotcueChange);
            StudioBridge._connect(grp, "hotcue_" + h + "_position", StudioBridge.onHotcueChange);
            StudioBridge._connect(grp, "hotcue_" + h + "_color",    StudioBridge.onHotcueChange);
        }

        if (engine.getValue(grp, "track_loaded") > 0)
            StudioBridge.sendIdentity(n);  // catch tracks already loaded at startup
    }

    StudioBridge._posTimer = engine.beginTimer(StudioBridge.POSITION_MS, StudioBridge.tickPosition);
    StudioBridge._hbTimer  = engine.beginTimer(StudioBridge.HEARTBEAT_MS, StudioBridge.tickHeartbeat);
};

StudioBridge.shutdown = function () {
    if (StudioBridge._posTimer) engine.stopTimer(StudioBridge._posTimer);
    if (StudioBridge._hbTimer)  engine.stopTimer(StudioBridge._hbTimer);
    for (var i = 0; i < StudioBridge._conns.length; i++)
        if (StudioBridge._conns[i]) StudioBridge._conns[i].disconnect();
};

// ---- helpers ----------------------------------------------------------------

StudioBridge._connect = function (grp, key, cb) {
    var c = engine.makeConnection(grp, key, cb);
    if (c) StudioBridge._conns.push(c);
};

StudioBridge._deckOf = function (group) {
    var m = group.match(/\[Channel(\d+)\]/);
    return m ? parseInt(m[1], 10) : 0;
};

// Append `value` as `nbytes` big-endian bytes. Uses div/mod (not bit shifts) so
// sample counts above 2^31 stay correct in JS's 32-bit bitwise world.
StudioBridge._pushBE = function (arr, value, nbytes) {
    value = Math.max(0, Math.round(value));
    var tmp = [];
    for (var k = 0; k < nbytes; k++) { tmp.unshift(value % 256); value = Math.floor(value / 256); }
    for (var k = 0; k < nbytes; k++) arr.push(tmp[k]);
};

// 7-bit MSB-pack: 1 collector byte then up to 7 data bytes; bit j of the
// collector holds the top bit of data byte j (matches MidiDecoder::unpackSysex).
StudioBridge._pack7 = function (raw) {
    var out = [];
    for (var i = 0; i < raw.length; i += 7) {
        var msb = 0;
        for (var j = 0; j < 7 && i + j < raw.length; j++)
            if (raw[i + j] & 0x80) msb |= (1 << j);
        out.push(msb);
        for (var j = 0; j < 7 && i + j < raw.length; j++)
            out.push(raw[i + j] & 0x7F);
    }
    return out;
};

StudioBridge._note = function (deck, num, on) {
    midi.sendShortMsg(0x90 + (deck - 1), num, on ? 0x7F : 0x00);
};

// ---- senders ----------------------------------------------------------------

StudioBridge.sendIdentity = function (deck) {
    var grp = "[Channel" + deck + "]";
    var samples    = engine.getValue(grp, "track_samples");
    var samplerate = engine.getValue(grp, "track_samplerate");
    var duration   = engine.getValue(grp, "duration");   // seconds
    var fileBpm    = engine.getValue(grp, "file_bpm");
    if (!samples || !samplerate) return;                 // not ready yet

    var raw = [];
    StudioBridge._pushBE(raw, samples, 4);
    StudioBridge._pushBE(raw, samplerate, 3);
    StudioBridge._pushBE(raw, duration * 1000.0, 4);
    StudioBridge._pushBE(raw, (fileBpm || 0) * 100.0, 2);

    var msg = [0xF0, StudioBridge.MANUF, StudioBridge.MSG_IDENTITY, deck]
                  .concat(StudioBridge._pack7(raw), [0xF7]);
    midi.sendSysexMsg(msg, msg.length);

    // push current transport state right after identity so the screen is correct
    StudioBridge._note(deck, 0x10, engine.getValue(grp, "play") > 0);
    StudioBridge._note(deck, 0x13, engine.getValue(grp, "sync_enabled") > 0);
    StudioBridge._note(deck, 0x18, engine.getValue(grp, "loop_enabled") > 0);
    StudioBridge.sendAllHotcues(deck);   // and the track's hotcues
};

// Send one hotcue's full state: position as a fraction of track_samples + colour.
// Disabled (no cue) sends enabled=0 so the bridge removes any stale marker.
StudioBridge.sendHotcue = function (deck, n) {
    var grp = "[Channel" + deck + "]";
    var total = engine.getValue(grp, "track_samples");
    var pos   = engine.getValue(grp, "hotcue_" + n + "_position");
    var on    = (pos !== undefined && pos >= 0 && total > 0);
    var v     = on ? Math.round(Math.max(0, Math.min(1, pos / total)) * 16383) : 0;

    var col = engine.getValue(grp, "hotcue_" + n + "_color");
    if (col === undefined || col < 0) col = 0xFFFFFF;   // unset/unsupported -> white
    col = Math.round(col);
    var r = Math.floor(col / 65536) % 256, g = Math.floor(col / 256) % 256, b = col % 256;

    var raw = [(n - 1) & 0x7F, on ? 1 : 0, (v >> 7) & 0x7F, v & 0x7F, r, g, b];
    var msg = [0xF0, StudioBridge.MANUF, StudioBridge.MSG_HOTCUE, deck]
                  .concat(StudioBridge._pack7(raw), [0xF7]);
    midi.sendSysexMsg(msg, msg.length);
};

StudioBridge.sendAllHotcues = function (deck) {
    for (var n = 1; n <= StudioBridge.HOTCUES; n++) StudioBridge.sendHotcue(deck, n);
};

StudioBridge.onHotcueChange = function (value, group, key) {
    var deck = StudioBridge._deckOf(group);
    var m = key.match(/hotcue_(\d+)_/);
    if (deck && m) StudioBridge.sendHotcue(deck, parseInt(m[1], 10));
};

StudioBridge.sendCleared = function (deck) {
    midi.sendSysexMsg([0xF0, StudioBridge.MANUF, StudioBridge.MSG_CLEARED, deck, 0xF7], 5);
};

// ---- connection callbacks ---------------------------------------------------

StudioBridge.onTrackLoaded = function (value, group) {
    var deck = StudioBridge._deckOf(group);
    if (value > 0) StudioBridge.sendIdentity(deck);
    else           StudioBridge.sendCleared(deck);
};

StudioBridge.onPlay = function (value, group) { StudioBridge._note(StudioBridge._deckOf(group), 0x10, value > 0); };
StudioBridge.onSync = function (value, group) { StudioBridge._note(StudioBridge._deckOf(group), 0x13, value > 0); };
StudioBridge.onLoop = function (value, group) { StudioBridge._note(StudioBridge._deckOf(group), 0x18, value > 0); };

// ---- timers -----------------------------------------------------------------

StudioBridge.tickPosition = function () {
    for (var n = 1; n <= StudioBridge.DECKS; n++) {
        var grp = "[Channel" + n + "]";
        if (engine.getValue(grp, "track_loaded") > 0) {
            var p = engine.getValue(grp, "playposition");      // 0..1 (can over/undershoot)
            p = Math.max(0, Math.min(1, p));
            // 14-bit so a zoomed-in scroll is smooth: CC 0x12 = MSB, 0x32 = LSB.
            var v = Math.round(p * 16383);
            midi.sendShortMsg(0xB0 + (n - 1), 0x12, (v >> 7) & 0x7F);
            midi.sendShortMsg(0xB0 + (n - 1), 0x32, v & 0x7F);
        }
    }
};

StudioBridge.tickHeartbeat = function () {
    for (var n = 1; n <= StudioBridge.DECKS; n++) {
        var grp = "[Channel" + n + "]";
        if (engine.getValue(grp, "track_loaded") > 0) StudioBridge.sendIdentity(n);
    }
};
