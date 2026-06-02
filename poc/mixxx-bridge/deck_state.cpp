// Mixxx Studio Bridge — deck state model (impl).

#include "deck_state.hpp"

namespace mxb {

void DeckModel::apply(const BridgeEvent& e) {
    // Only deck-targeted events update deck state here. (Sampler/master handled
    // by their own models when those views land; ignored for now.)
    if (e.target != Target::Deck) return;
    int n = e.deck;
    if (n < 1 || n > 4) return;
    DeckState& d = decks_[n];

    // Only repaint when something actually changes — the Mixxx script streams
    // position at 20Hz and re-sends transport state on a 2s heartbeat, so
    // unconditional dirtying would repaint constantly (and flicker while paused).
    auto setB = [&](bool& f, bool v)   { if (f != v) { f = v; d.dirty = true; } };
    auto setD = [&](double& f, double v) { if (f != v) { f = v; d.dirty = true; } };

    switch (e.type) {
        case BridgeEventType::TrackIdentity: {
            // The heartbeat re-sends identity for the loaded track; if it's the
            // same track we already resolved, do nothing — no re-decode, no
            // position reset, no repaint. Only a genuinely new track reloads.
            if (d.loaded && d.hasWaveform && d.fp == e.fp) break;

            d.fp = e.fp;
            d.loaded = true;
            d.hasWaveform = false;
            d.artist.clear();
            d.title.clear();
            d.hotcues.clear();
            // bpm from the DB by default; fall back to the fingerprint's file_bpm.
            d.bpm = e.fp.bpmCenti / 100.0;
            if (loader_) {
                Waveform wf;
                std::string artist, title;
                double bpm = d.bpm;
                std::vector<Hotcue> hotcues;
                if (loader_(e.fp, artist, title, bpm, wf, hotcues)) {
                    d.artist = std::move(artist);
                    d.title  = std::move(title);
                    d.bpm    = bpm;
                    d.waveform = std::move(wf);
                    d.hotcues  = std::move(hotcues);
                    d.hasWaveform = true;
                }
            }
            d.position = 0.0;
            d.dirty = true;
            break;
        }
        case BridgeEventType::TrackCleared:
            d = DeckState{};        // reset, leaves dirty = true
            break;
        case BridgeEventType::Play:         setB(d.playing, e.value != 0.0); break;
        case BridgeEventType::TrackLoaded:  setB(d.loaded,  e.value != 0.0); break;
        case BridgeEventType::SyncEnabled:  setB(d.sync,    e.value != 0.0); break;
        case BridgeEventType::Keylock:      setB(d.keylock, e.value != 0.0); break;
        case BridgeEventType::LoopEnabled:  setB(d.loop,    e.value != 0.0); break;
        case BridgeEventType::Bpm:          setD(d.bpm,      e.value); break;
        case BridgeEventType::Rate:         setD(d.rate,     e.value); break;
        case BridgeEventType::PlayPosition: setD(d.position, e.value); break;  // no change while paused -> no repaint
        case BridgeEventType::VuMeter:      setD(d.vu,       e.value); break;
        default: break;  // hotcues / cue indicator: tracked when those widgets land
    }
}

}  // namespace mxb
