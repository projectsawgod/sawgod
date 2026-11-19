// Accelerated isolated-driver demos, through the runtime's request hooks and mixer.
// sawgod-audio-spike gb-sfx WSC_ROM OUTPUT_DIRECTORY
int gb_sfx(int argc, char **argv) {
    if (argc != 4)
        return 2;
    const auto source = read_file(argv[2]);
    if (source.size() < 0x3E0000)
        return 2;
    using C = game::hooks::AudioContext;
    struct Demo {
        int effect, item;
        C context;
        const char *name;
        bool seam;
    };
    const Demo demos[] = {{146, 0, C(0), "wsc146_menu_move", false},
                          {147, 0, C(0), "wsc147_menu_confirm", false},
                          {148, 0, C(0), "wsc148_menu_cancel", false},
                          {42, 0, C(0), "wsc042_invalid_action", false},
                          {24, 32, C::BattleAction, "wsc024_action032_sword_hit", false},
                          {40, 103, C::BattleAction, "wsc040_action103_fire_spell", false},
                          {5, 0, C::BattleEntry, "wsc005_battle_entry", false},
                          {21, 0, C::Warp, "wsc021_destination_warp", false},
                          {50, 66, C::BattleAction, "wsc050_action066_chainsaw_loop_seam", true}};
    std::filesystem::create_directories(argv[3]);
    for (const auto &demo : demos) {
        std::array<std::vector<int16_t>, 2> halves;
        double event_at = 0, seam_at = 0;
        for (int version = 0; version < 2; ++version) {
            auto w = std::make_unique<emu::Wsc>(source, 0);
            app::GbMusic sound;
            if (!sound.load(source))
                return 1;
            sound.attach(game::Session::of(*w));
            game::Machine m(*w);
            m.cs() = 0xD000;
            m.ds() = 0;
            game::sound_init(m);
            const auto &map = *app::gb_song_map(144);
            if (demo.seam) {
                m.ax() = 144;
                game::sound_request_core(m);
            }
            w->apu.take();
            std::unique_ptr<game::hooks::EventScope> context;
            int ticks = 0, event_frame = -1;
            for (int frame = 0; frame < 20000; ++frame) {
                if (event_frame < 0 && ((!demo.seam && frame == 23) ||
                                        (demo.seam && ticks >= map.loop_start + map.loop_ticks - 3))) {
                    if (demo.context != C(0))
                        context = std::make_unique<game::hooks::EventScope>(
                            m, game::hooks::Event::SoundContext,
                            game::hooks::Args{int(demo.context), demo.item});
                    game::play_sound_effect_core(m, uint8_t(demo.effect));
                    event_frame = frame;
                    event_at = double(halves[size_t(version)].size() / 2) / 24000;
                }
                // The spell's second visual phase must not restart its GB action effect.
                if (demo.effect == 40 && event_frame >= 0 && frame == event_frame + 20)
                    game::play_sound_effect_core(m, 87);
                const int b = 0x62C + map.clock_slot * 0x32;
                const uint8_t phase = w->ram[size_t(b + 7)];
                const float gb = float(version);
                app::apply_channel_gains(*w, sound.playing() ? 1 - gb : 1, 1, sound.wsc_music_slots(*w),
                                         sound.mapped_wsc_slots(*w), gb);
                app::paused_tick(*w);
                if (demo.seam && (frame == 0 || w->ram[size_t(b + 7)] >= phase)) {
                    ++ticks;
                    if (ticks == map.loop_start + map.loop_ticks)
                        seam_at = double(halves[size_t(version)].size() / 2) / 24000;
                }
                auto samples = w->apu.take();
                sound.frame(*w, samples, gb, gb);
                halves[size_t(version)].insert(halves[size_t(version)].end(), samples.begin(), samples.end());
                if (event_frame >= 0 && frame >= event_frame + int(3 * kWscFrameRate))
                    break;
            }
            if (event_frame < 0)
                return 1;
        }
        const double half_seconds = double(halves[0].size() / 2) / 24000;
        std::vector<int16_t> both = std::move(halves[0]);
        both.insert(both.end(), 24000, 0); // half-second separator, stereo
        both.insert(both.end(), halves[1].begin(), halves[1].end());
        std::string stem = std::string(argv[3]) + "/" + demo.name;
        std::string path = stem + ".wav";
        for (int v = 2; std::filesystem::exists(path); ++v)
            path = stem + "_v" + std::to_string(v) + ".wav";
        if (!write_wav(path, both, 24000))
            return 1;
        std::printf("%s: WSC effect %.3fs; GB half %.3fs, effect %.3fs", path.c_str(), event_at,
                    half_seconds + 0.5, half_seconds + 0.5 + event_at);
        if (demo.seam)
            std::printf("; loop seams %.3fs / %.3fs", seam_at, half_seconds + 0.5 + seam_at);
        std::printf("\n");
    }
    return 0;
}
