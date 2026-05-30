// Mixxx Studio Bridge — deck state model.
//
// Folds decoded BridgeEvents (mixxx_midi) into per-deck state the renderer reads.
// This is the "View state" subscriber in SPEC §1.2's event router. Track-path
// events trigger a resolve+decode (via a caller-supplied loader so this stays
// testable without a DB), giving each deck a ready-to-draw waveform.

#pragma once
#include <array>
#include <functional>
#include <string>

#include "mixxx_midi.hpp"
#include "waveform.hpp"

namespace mxb {

struct DeckState {
    bool        loaded   = false;
    std::string artist;
    std::string title;
    std::string path;          // last path Mixxx reported
    double      bpm      = 0.0;
    double      position = 0.0;  // 0..1
    double      rate     = 1.0;
    double      vu       = 0.0;  // 0..1
    bool        playing  = false;
    bool        sync     = false;
    bool        keylock  = false;
    bool        loop     = false;
    Waveform    waveform;        // decoded on track load
    bool        hasWaveform = false;
    bool        dirty    = true;  // renderer clears after drawing
};

class DeckModel {
public:
    // Loader: given a track path, fill artist/title/waveform. Returns false if
    // unresolved (DeckState keeps whatever Mixxx sent, just no waveform).
    using Loader = std::function<bool(const std::string& path, std::string& artist,
                                      std::string& title, Waveform& wf)>;

    explicit DeckModel(Loader loader) : loader_(std::move(loader)) {}

    void apply(const BridgeEvent& e);

    // Decks are 1..4; index 0 unused so deck N maps to decks_[N].
    const DeckState& deck(int n) const { return decks_[clamp(n)]; }
    DeckState&       deck(int n)       { return decks_[clamp(n)]; }

    bool anyDirty() const {
        for (int i = 1; i <= 4; ++i) if (decks_[i].dirty) return true;
        return false;
    }

private:
    static int clamp(int n) { return (n >= 1 && n <= 4) ? n : 0; }

    Loader loader_;
    std::array<DeckState, 5> decks_{};
};

}  // namespace mxb
