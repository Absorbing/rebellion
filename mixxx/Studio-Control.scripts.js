// Mixxx Studio Bridge — control-side script (library jump-to-top).
//
// Most controls map directly in the XML; this only handles the one action that
// isn't a simple control: jumping the library selection to the top, which the
// bridge sends when you open browse so its on-device list and Mixxx's selection
// start aligned. (Up/Down and LoadSelectedTrack are plain XML bindings.)

var StudioCtl = {};

StudioCtl.init = function () {};
StudioCtl.shutdown = function () {};

// Note on (value > 0) -> scroll the library selection far up (clamps at top).
StudioCtl.libTop = function (channel, control, value) {
    if (value > 0) engine.setValue("[Library]", "MoveVertical", -100000);
};
