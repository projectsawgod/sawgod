// Render the load-time transcriptions on the native WSC clock; no game execution needed.
// sawgod-audio-spike gb-cues WSC_ROM OUTPUT_DIRECTORY
int gb_cues(int argc, char **argv) {
    if (argc != 4)
        return 2;
    const auto source = read_file(argv[2]);
    if (source.size() < 0x3E0000)
        return 2;
    std::vector<uint8_t> image(source.begin() + 0x320000, source.begin() + 0x340000);
    if (!app::gb_append_cues(image, source))
        return 1;
    auto driver = emu::saga1_sound_driver();
    driver.bank = 8;
    for (int song = 135; song <= 137; ++song) {
        auto w = std::make_unique<emu::Wsc>(source, 0);
        const auto &map = *app::gb_song_map(uint8_t(song));
        auto host = std::make_unique<emu::GbSoundHost>(image, driver);
        host->apu.set_sample_rate(24000);
        if (!host->init())
            return 1;
        host->apu.take();
        host->request(map.gb_song);
        std::vector<int16_t> wa, ga(318 * 2, 0);
        int previous = 0;
        bool ok = true;
        const auto scan = scan_wsc_song(*w, song, 16000, 2, [&](int, int tick) {
            auto a = w->apu.take();
            wa.insert(wa.end(), a.begin(), a.end());
            const int target = map.gb_ticks_by(tick);
            ok &= host->frame_locked(target - previous, double(emu::GbApu::kClock) / kWscFrameRate);
            previous = target;
            a = host->apu.take();
            ga.insert(ga.end(), a.begin(), a.end());
        });
        if (!ok)
            return 1;
        const size_t n = std::min(wa.size(), ga.size());
        wa.resize(n);
        double ew = 0, eg = 0;
        for (size_t i = 0; i < n; ++i) {
            ew += double(wa[i]) * wa[i];
            eg += double(ga[i]) * ga[i];
        }
        if (ew == 0 || eg == 0)
            return 1;
        const double level = std::sqrt(ew / eg), duration = double(n / 2) / 24000;
        if (!scan.loop_ticks) { // a short cue: each version whole too, RMS-matched, same headroom rule
            double peak = 1;
            for (size_t i = 0; i < n; ++i)
                peak = std::max({peak, std::abs(double(wa[i])), std::abs(ga[i] * level)});
            const double g = std::min(1.0, 30000 / peak);
            for (const char *side : {"wsc", "gb"}) {
                std::vector<int16_t> out(n);
                for (size_t i = 0; i < n; ++i)
                    out[i] = int16_t(std::lround((side[0] == 'w' ? double(wa[i]) : ga[i] * level) * g));
                char whole[96];
                std::snprintf(whole, sizeof whole, "/wsc%03d_gb%02d_event_cue_%s_only.wav", song, map.gb_song,
                              side);
                if (!write_wav(std::string(argv[3]) + whole, out, 24000))
                    return 1;
            }
        }
        const double seam = scan.loop_end_frame / kWscFrameRate;
        const std::vector<std::pair<double, double>> windows =
            scan.loop_ticks ? std::vector<std::pair<double, double>>{{0.05, 2},
                                                                     {duration * 0.35, duration * 0.48},
                                                                     {seam - 1, seam + 2}}
                            : std::vector<std::pair<double, double>>{{duration * 0.05, duration * 0.55}};
        for (const auto &[in, out] : windows)
            std::printf("%d: GB %.3fs, WSC %.3fs (0.5s crossfades)\n", song, in, out);
        // Leave shared headroom for the equal-power fades; never hard-clip an audition.
        double peak = 1;
        std::vector<double> mix(n);
        for (size_t i = 0; i < n; ++i) {
            const double t = double(i / 2) / 24000;
            double weight = 0;
            for (const auto &[in, out] : windows)
                weight = std::max(weight, std::min(std::clamp((t - in) / 0.5, 0.0, 1.0),
                                                   std::clamp((out + 0.5 - t) / 0.5, 0.0, 1.0)));
            mix[i] = wa[i] * std::cos(weight * 1.5707963267948966) +
                     ga[i] * level * std::sin(weight * 1.5707963267948966);
            peak = std::max(peak, std::abs(mix[i]));
        }
        const double gain = std::min(1.0, 30000 / peak);
        for (size_t i = 0; i < n; ++i)
            wa[i] = int16_t(std::lround(mix[i] * gain));
        char name[96];
        std::snprintf(name, sizeof name, "/wsc%03d_gb%02d_event_cue.wav", song, map.gb_song);
        std::string path = std::string(argv[3]) + name;
        // Keep prior renders for A/B comparison.
        for (int version = 2; std::filesystem::exists(path); ++version)
            path = std::string(argv[3]) + std::string(name).substr(0, std::string(name).size() - 4) + "_v" +
                   std::to_string(version) + ".wav";
        if (!write_wav(path, wa, 24000))
            return 1;
        const auto cue = app::gb_read_cue(source, uint8_t(song));
        std::printf("%s: %.3fs, WSC intro %d / loop %d ticks; notes %zu/%zu/%zu; RMS scale %.3f\n",
                    path.c_str(), duration, scan.intro_ticks, scan.loop_ticks, cue->voices[0].notes.size(),
                    cue->voices[1].notes.size(), cue->voices[2].notes.size(), level);
    }
    return 0;
}
