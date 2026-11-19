// Exact input snapshots for output-resolution UI geometry. No native-frame or pointer identity assumptions:
// held pictures can reuse records, and later compositors can change pixels without changing the records.
#pragma once
#include "emu/wsc.h"
#include "engine/presentation_diagnostics.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <span>
#include <type_traits>

namespace sawgod {
class UiDrawCache {
  public:
    bool update(const SDL_FRect &dst, const emu::Wsc::UiFill *fill, const emu::Wsc::UiText *text,
                std::span<const uint32_t> picture, float weight, std::span<const uint32_t> blended = {}) {
        const bool same =
            !presentation_diagnostics::fresh() && valid_ && !prepared_ && dst.x == dst_.x &&
            dst.y == dst_.y && dst.w == dst_.w && dst.h == dst_.h && weight == weight_ &&
            bool(fill) == has_fill_ && bool(text) == has_text_ && equal(picture_, picture) &&
            equal(blended_, blended) &&
            (!fill || (equal(fill_.windows, fill->windows) && equal(fill_.owner, fill->owner) &&
                       equal(fill_.color, fill->color) && equal(fill_.beneath, fill->beneath))) &&
            (!text || (equal(text_.pixels, text->pixels) && equal(text_.corners, text->corners) &&
                       equal(text_.bars.rectangles, text->bars.rectangles) &&
                       equal(text_.bars.pixels, text->bars.pixels)));
        if (same)
            return false;
        valid_ = true;
        prepared_ = nullptr;
        ++revision_;
        dst_ = dst;
        weight_ = weight;
        has_fill_ = fill != nullptr;
        has_text_ = text != nullptr;
        if (fill)
            fill_ = *fill;
        if (text)
            text_ = *text;
        picture_.assign(picture.begin(), picture.end());
        blended_.assign(blended.begin(), blended.end());
        return true;
    }
    // The app checks the shared inputs once before submitting both passes. Standalone callers can
    // continue using update(); neither pass has to trust a native-frame or pointer-only key.
    bool accept(const UiDrawCache &prepared) {
        const bool changed = presentation_diagnostics::fresh() || !valid_ || prepared_ != &prepared ||
                             prepared_revision_ != prepared.revision_;
        prepared_ = &prepared;
        prepared_revision_ = prepared.revision_;
        valid_ = true;
        return changed;
    }
    void invalidate() {
        valid_ = false;
        ++revision_;
    }

  private:
    template <class A, class B> static bool equal(const A &a, const B &b) {
        using T = typename A::value_type;
        static_assert(std::is_trivially_copyable_v<T>);
        // Padding can cause an unnecessary rebuild, never a stale result. Snapshot assignment preserves
        // object representations; this avoids a field-by-field scan of all 32,256 pixel records.
        return a.size() == b.size() &&
               (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
    }
    uint64_t revision_ = 0, prepared_revision_ = 0;
    const UiDrawCache *prepared_ = nullptr;
    bool valid_ = false, has_fill_ = false, has_text_ = false;
    SDL_FRect dst_{};
    float weight_ = 0;
    emu::Wsc::UiFill fill_;
    emu::Wsc::UiText text_;
    std::vector<uint32_t> picture_, blended_;
};
} // namespace sawgod
