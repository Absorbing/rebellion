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

    switch (e.type) {
        case BridgeEventType::TrackPath: {
            d.path = e.text;
            d.loaded = true;
            d.hasWaveform = false;
            d.artist.clear();
            d.title.clear();
            if (loader_) {
                Waveform wf;
                std::string artist, title;
                if (loader_(e.text, artist, title, wf)) {
                    d.artist = std::move(artist);
                    d.title  = std::move(title);
                    d.waveform = std::move(wf);
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
        case BridgeEventType::Play:         d.playing = e.value != 0.0; d.dirty = true; break;
        case BridgeEventType::TrackLoaded:  d.loaded  = e.value != 0.0; d.dirty = true; break;
        case BridgeEventType::SyncEnabled:  d.sync    = e.value != 0.0; d.dirty = true; break;
        case BridgeEventType::Keylock:      d.keylock = e.value != 0.0; d.dirty = true; break;
        case BridgeEventType::LoopEnabled:  d.loop    = e.value != 0.0; d.dirty = true; break;
        case BridgeEventType::Bpm:          d.bpm     = e.value; d.dirty = true; break;
        case BridgeEventType::Rate:         d.rate    = e.value; d.dirty = true; break;
        case BridgeEventType::PlayPosition: d.position = e.value; d.dirty = true; break;
        case BridgeEventType::VuMeter:      d.vu      = e.value; d.dirty = true; break;
        default: break;  // hotcues / cue indicator: tracked when those widgets land
    }
}

}  // namespace mxb
