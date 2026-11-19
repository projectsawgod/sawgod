#include "app/button_prompts.h"

#include <optional>

#include "engine/button_icons.h"
#include "game/title_menu_lettering.h"

namespace sawgod::app {

void install_button_prompts(ui::Input &input, ui::InputMap &input_map, Settings &settings, Fidelity &fidelity,
                            game::HostPicture &host_picture) {
    // (the callbacks outlive this call; they hold the caller's objects)
    ui::Input *const in = &input;
    ui::InputMap *const map = &input_map;
    Settings *const set = &settings;
    Fidelity *const fid = &fidelity;
    // Button tokens in text ({A}, {B}...) show what the player presses for them: the binding on the device
    // they last used (its first slot with an icon), in the Icon Type's family; else the WSC name.
    sawgod::set_button_token_icons([in, map, set](int token) {
        if (token == sawgod::kButtonTokenX || token == sawgod::kButtonTokenY) // a whole pad
            return sawgod::pad_group_icon(token == sawgod::kButtonTokenY, !in->last_used_pad());
        const auto wsc = sawgod::ui::WscButton(token);
        for (const bool pad : {in->last_used_pad(), !in->last_used_pad()})
            for (const sawgod::ui::Binding &b : map->effective_slots(wsc, pad))
                if (sawgod::ButtonIcon icon = sawgod::ui::binding_icon(
                        b, sawgod::ui::resolve_pad_icons(set->pad_icons->get(), in->gamepad_type()));
                    !icon.empty())
                    return icon;
        return sawgod::ButtonIcon{};
    });
    host_picture.set_title_step_hint([in, map, set, fid] {
        const bool pad = in->gamepad_count() > 0 && in->last_used_pad();
        const int icons = sawgod::ui::resolve_pad_icons(set->pad_icons->get(), in->gamepad_type());
        return sawgod::game::TitleMenuLettering::StepHint{
            sawgod::ui::kGfxSwapBuild ? fid->step_mode() : std::nullopt,
            sawgod::ui::bound_button_text(*map, sawgod::ui::WscButton::FidelityDown, pad, icons,
                                          in->gamepad_type()),
            sawgod::ui::bound_button_text(*map, sawgod::ui::WscButton::FidelityUp, pad, icons,
                                          in->gamepad_type())};
    });
}

} // namespace sawgod::app
