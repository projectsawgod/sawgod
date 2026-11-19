// Survey isolated effects from both drivers, without advancing either game's logic.
// sawgod-audio-spike sfx-survey WSC_ROM GB_ROM INPUTS FRAME OUT.csv
int sfx_survey(int argc, char **argv) {
    if (argc != 7)
        return 2;
    FILE *out = std::fopen(argv[6], "w");
    if (!out)
        return 1;
    std::fprintf(out, "system,effect,frame,channel,active,frequency,volume,trigger,noise\n");
    auto w = wsc_boot(argv[2], argv[4], std::atoi(argv[5]));
    const auto base = w->snapshot();
    for (int e = 0; e < 163; ++e) {
        w->restore(base);
        game::Machine m(*w);
        m.cs() = 0xD000;
        m.ds() = 0;
        game::sound_init(m);
        game::play_sound_effect_core(m, uint8_t(e));
        for (int f = 0; f < 240; ++f) {
            app::paused_tick(*w);
            w->apu.take();
            for (int slot = 0; slot < 8; ++slot) {
                const int b = 0x62C + slot * 0x32;
                const int hw = w->ram[size_t(b + 2)] & 3;
                const int step = w->ram[size_t(b)] | w->ram[size_t(b + 1)] << 8;
                const int period = (w->io[size_t(0x80 + hw * 2)] | w->io[size_t(0x81 + hw * 2)] << 8) & 0x7FF;
                if (step != 0xFFFF)
                    std::fprintf(out, "wsc,%d,%d,%d,1,%d,%d,%d,%d\n", e, f, slot, period,
                                 w->io[size_t(0x88 + hw)], step, (w->io[0x90] & 0x80) && hw == 3);
            }
        }
    }
    const auto rom = read_file(argv[3]);
    for (int e = 1; e <= 36; ++e) {
        auto h = std::make_unique<emu::GbSoundHost>(rom, emu::saga1_sound_driver());
        if (!h->init()) {
            std::fclose(out);
            return 1;
        }
        h->apu.take();
        h->write(0xFFE4, uint8_t(e));
        uint32_t previous[4] = {};
        for (int f = 0; f < 180; ++f) {
            if (!h->frame()) {
                std::fclose(out);
                return 1;
            }
            h->apu.take();
            for (int ch : {0, 3}) {
                std::fprintf(out, "gb,%d,%d,%d,%d,%d,%d,%d,%d\n", e, f, ch,
                             h->apu.channel_on(ch) && (h->apu.read(0xFF25) & (0x11 << ch)),
                             h->apu.frequency(ch), h->apu.volume(ch), h->triggers[ch] != previous[ch],
                             ch == 3 ? h->apu.read(0xFF22) : 0);
                previous[ch] = h->triggers[ch];
            }
        }
    }
    std::fclose(out);
    return 0;
}
