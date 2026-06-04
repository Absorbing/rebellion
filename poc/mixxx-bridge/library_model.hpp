// Mixxx Studio Bridge — library browse model.
//
// Holds the track list (from mixxxdb) + a cursor, and the visible-window math
// for the list screen. Pure logic, unit-tested. The cursor lock-steps Mixxx's
// library selection: move() returns the actual delta moved so the caller can
// send that many [Library] MoveUp/MoveDown to Mixxx.

#pragma once
#include <string>
#include <vector>

#include "mixxxdb.hpp"

namespace mxb {

class LibraryModel {
public:
    void setTracks(std::vector<LibRow> tracks) {
        tracks_ = std::move(tracks);
        cursor_ = 0;
    }
    int  size()   const { return static_cast<int>(tracks_.size()); }
    bool empty()  const { return tracks_.empty(); }
    int  cursor() const { return cursor_; }

    // Move the cursor by `delta`, clamped to [0, size-1]. Returns the actual
    // delta applied (for lock-stepping Mixxx's selection).
    int move(int delta) {
        if (tracks_.empty()) return 0;
        int target = cursor_ + delta;
        if (target < 0) target = 0;
        if (target > size() - 1) target = size() - 1;
        int applied = target - cursor_;
        cursor_ = target;
        return applied;
    }

    void toTop() { cursor_ = 0; }

    const LibRow& selected() const { return tracks_.at(cursor_); }

    // First visible row so the cursor stays on screen within `rows` rows.
    int windowStart(int rows) const {
        if (rows <= 0 || size() <= rows) return 0;
        int start = cursor_ - rows / 2;
        if (start < 0) start = 0;
        if (start > size() - rows) start = size() - rows;
        return start;
    }

    const LibRow& at(int i) const { return tracks_.at(i); }

private:
    std::vector<LibRow> tracks_;
    int cursor_ = 0;
};

}  // namespace mxb
