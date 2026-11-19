// Native pixel ownership for both scene effects and reconstructed geometry.
#pragma once
#include "emu/wsc.h"

namespace sawgod::game {
// Window cells can use every upper palette, including comparison portraits and reward art.
// This is the SCR1 replacement/underlay set, not the pixel UI set: battle enemies use 4-6,
// native meat uses 12, and field objects may use 15. Pixel ownership also depends on provenance.
inline constexpr uint16_t kSceneWindowPalettes = 0xFF80;
inline constexpr uint16_t kUiPalettes = (1u << 7) | (1u << 15);

struct UiOwnership {
    bool field = false;
    bool battle = false;
    // Effects restore enabled spell sprites exactly through the protected-source channel.
    // Geometry has no spell-light pass and preserves all native battle OAM 0-26 as UI.
    bool spell_source = false;
    // Geometry keeps script-only OAM at native screen coordinates. Actual field actors
    // are identified by the native writers' latched card records, not a tile-number range.
    bool preserve_native_field_sprites = false;

    bool owns(const emu::RasterPixel &p, int layer, bool field_actor = true) const {
        const bool sprite = layer == emu::FrameLayers::SpriteBehind || layer == emu::FrameLayers::SpriteFront;
        const bool field_object = field && sprite && p.tile >= 0x18 && p.tile < 0x98;
        return (field && preserve_native_field_sprites && sprite && !field_actor) ||
               layer == emu::FrameLayers::Host || (p.flags & emu::RasterPixel::HostText) ||
               ((kUiPalettes >> p.palette & 1) && !(p.palette == 15 && field_object)) ||
               (battle && sprite && p.sprite < 27 && !spell_source) ||
               (!sprite && p.tile >= 0x100 && p.tile < 0x200);
    }
    bool panel(int y) const { return battle && y >= 80; }
};
} // namespace sawgod::game
