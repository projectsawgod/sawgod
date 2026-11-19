// Offline pitch alignment for audio_spike.cpp. All ticks are observed on the WSC frame clock.
using Pitches = std::array<int, 4>; // MIDI note, -1 for silence (GB wave's doubled harmonic)

int midi_note(double hz) { return int(std::lround(69 + 12 * std::log2(hz / 440))); }

Pitches wsc_pitches(const emu::Wsc &w, int first, int count) {
    Pitches p = {-1, -1, -1, -1};
    for (int c = 0; c < count; ++c) {
        const int b = 0x62C + (first + c) * 0x32;
        const int hw = w.ram[size_t(b + 2)] & 3;
        const int period = (w.io[size_t(0x80 + hw * 2)] | w.io[size_t(0x81 + hw * 2)] << 8) & 0x7FF;
        if (w.io[size_t(0x88 + hw)] && period && (w.ram[size_t(b)] | w.ram[size_t(b + 1)] << 8) != 0xFFFF)
            p[size_t(c)] = midi_note(3072000.0 / ((2048 - period) * 32));
    }
    return p;
}

Pitches gb_pitches(const std::array<emu::GbSongRender::ChannelFrame, 4> &frame) {
    Pitches p = {-1, -1, -1, -1};
    for (int c = 0; c < 3; ++c) {
        const auto &ch = frame[size_t(c)];
        if (ch.on && ch.volume && ch.freq < 2048)
            p[size_t(c)] = midi_note(131072.0 / (2048 - ch.freq));
    }
    return p;
}

// Exact notes get full weight; octave changes still identify the voice and musical position. Both
// silent contributes nothing, so an empty ending cannot win an alignment.
double note_score(int a, int b) {
    if (a < 0 && b < 0)
        return 0;
    if (a < 0 || b < 0)
        return -0.4;
    if (a == b)
        return 1;
    return (a - b) % 12 == 0 ? 0.5 : -0.4;
}

double pitch_score(const Pitches &w, const Pitches &g, const std::array<uint8_t, 3> &pair) {
    double score = 0;
    for (size_t c = 0; c < 3; ++c)
        score += note_score(w[pair[c]], g[c]);
    return score;
}

using Anchors = std::vector<std::pair<double, double>>;

// Simplify a tick path to legible piecewise-linear anchors, bounding the error in GB ticks.
void simplify_path(const std::vector<int> &g, int lo, int hi, double error, Anchors &out) {
    double worst = error;
    int at = -1;
    for (int w = lo + 1; w < hi; ++w) {
        const double line = g[size_t(lo)] + double(w - lo) * (g[size_t(hi)] - g[size_t(lo)]) / (hi - lo);
        const double e = std::abs(line - g[size_t(w)]);
        if (e > worst) {
            worst = e;
            at = w;
        }
    }
    if (at >= 0) {
        simplify_path(g, lo, at, error, out);
        simplify_path(g, at, hi, error, out);
    } else {
        out.emplace_back(hi, g[size_t(hi)]);
    }
}

struct GbReference {
    std::vector<Pitches> pitches = {Pitches{-1, -1, -1, -1}};
    int intro = -1, period = 0;
    std::array<int, 3> voice_intro = {-1, -1, -1}, voice_period{};
};

GbReference gb_reference(const std::vector<uint8_t> &rom, uint8_t song, int ticks) {
    GbReference r;
    const auto driver = emu::saga1_sound_driver();
    auto host = std::make_unique<emu::GbSoundHost>(rom, driver);
    host->init();
    host->request(song);
    std::map<std::vector<uint8_t>, int> whole;
    std::array<std::map<std::vector<uint8_t>, int>, 3> voices;
    host->cpu.watch_pc = driver.tick_pc;
    int tick = 0;
    host->cpu.on_watch = [&] {
        ++tick;
        if (r.intro < 0) {
            auto [it, fresh] = whole.emplace(host->driver_state(), tick);
            if (!fresh) {
                r.intro = it->second;
                r.period = tick - it->second;
                whole.clear();
            }
        }
        for (size_t c = 0; c < 3; ++c) {
            if (r.voice_intro[c] >= 0)
                continue;
            std::vector<uint8_t> state;
            for (int i = 0; i < 0x20; ++i)
                state.push_back(host->read(uint16_t(0xCE00 + c * 0x20 + size_t(i))));
            auto [it, fresh] = voices[c].emplace(std::move(state), tick);
            if (!fresh) {
                r.voice_intro[c] = it->second;
                r.voice_period[c] = tick - it->second;
                voices[c].clear();
            }
        }
    };
    constexpr double cycles = double(emu::GbApu::kClock) / kWscFrameRate;
    for (int t = 0; t < ticks; ++t) {
        host->frame_locked(1, cycles);
        std::array<emu::GbSongRender::ChannelFrame, 4> frame{};
        for (int c = 0; c < 3; ++c)
            frame[size_t(c)] = {uint16_t(host->apu.frequency(c)), uint8_t(host->apu.volume(c)),
                                host->apu.channel_on(c), false};
        r.pitches.push_back(gb_pitches(frame));
        host->apu.take();
    }
    return r;
}

app::GbSongMap derive_map(const std::vector<Pitches> &w, const std::vector<Pitches> &g, const WscScan &scan,
                          int gb_period, uint8_t ws, uint8_t gs) {
    app::GbSongMap m;
    m.wsc_song = ws;
    m.gb_song = gs;
    m.clock_slot = uint8_t(scan.first_slot);
    std::array<uint8_t, 3> pair = {0, 1, 2};
    double best = -1e30;
    int offset = 0;
    const int hi = std::min(int(w.size()) - 1,
                            scan.loop_ticks > 1 ? scan.intro_ticks + scan.loop_ticks : int(w.size()) - 1);
    const int lo = std::min(300, std::max(1, hi / 5));
    // One-to-one voice pairing, fitted over the music rather than just its short run-up.
    double alignment_slope = 1;
    const std::vector<double> slopes =
        scan.ended ? std::vector<double>{1, 1.5, 2, 2.5, 3} : std::vector<double>{1, 1.5};
    for (double slope : slopes)
        for (int d = -8; d <= 8; ++d)
            for (int a = 0; a < scan.channels; ++a)
                for (int b = 0; b < scan.channels; ++b)
                    for (int c = 0; c < scan.channels; ++c) {
                        if (a == b || a == c || b == c)
                            continue;
                        const std::array<uint8_t, 3> p = {uint8_t(a), uint8_t(b), uint8_t(c)};
                        double total = 0;
                        for (int t = lo; t < std::min(hi, lo + 1200); ++t) {
                            const int gt = int(std::floor(slope * t + d));
                            if (gt > 0 && gt < int(g.size()))
                                total += pitch_score(w[size_t(t)], g[size_t(gt)], p);
                        }
                        if (total > best) {
                            best = total;
                            alignment_slope = slope;
                            m.slope =
                                slope == 1 ? 1 : 1.5; // finite jingles are described entirely by anchors
                            offset = d;
                            pair = p;
                        }
                    }
    // A narrow dynamic-programming band catches run-up lengths, added rests and the ending's changed
    // phrasing. Changing the affine offset costs eight matching voice-ticks: avoid chasing envelopes.
    constexpr int radius = 64, width = radius * 2 + 1;
    std::vector<std::array<int8_t, width>> back(size_t(hi + 1));
    std::array<double, width> prev, next;
    prev.fill(-1e30);
    prev[size_t(2 - int(std::floor(alignment_slope)) + radius)] = 0; // GB start tick + first note
    for (int t = 2; t <= hi; ++t) {
        next.fill(-1e30);
        const int base = int(std::floor(alignment_slope * t));
        const int old_base = int(std::floor(alignment_slope * (t - 1)));
        for (int d = -radius; d <= radius; ++d) {
            const int gt = base + d;
            if (gt < 2 || gt >= int(g.size()))
                continue;
            for (int delta = -2; delta <= 2; ++delta) {
                const int old_d = d + delta, advance = base - old_base - delta;
                if (old_d < -radius || old_d > radius || advance < 0 || advance > 3)
                    continue;
                const double value = prev[size_t(old_d + radius)] - 8 * std::abs(delta) +
                                     pitch_score(w[size_t(t)], g[size_t(gt)], pair);
                if (value > next[size_t(d + radius)]) {
                    next[size_t(d + radius)] = value;
                    back[size_t(t)][size_t(d + radius)] = int8_t(delta);
                }
            }
        }
        prev = next;
    }
    int d = offset;
    for (int o = -radius; o <= radius; ++o)
        if (prev[size_t(o + radius)] > prev[size_t(d + radius)])
            d = o;
    std::vector<int> path(size_t(hi + 1));
    path[0] = 1;
    for (int t = hi; t >= 2; --t) {
        path[size_t(t)] = int(std::floor(alignment_slope * t)) + d;
        d += back[size_t(t)][size_t(d + radius)];
    }
    path[1] = 2;
    // A loop is a musical phrase, not the GB driver's whole-state LCM. Repeating its captured state
    // keeps every GB voice on this phrase even when the GB channels naturally have unequal periods.
    if (!scan.ended && scan.loop_ticks > 1) {
        m.loop_start = scan.intro_ticks;
        m.loop_ticks = scan.loop_ticks;
        m.loop_phase = m.slope * m.loop_start - std::floor(m.slope * m.loop_start);
        m.gb_loop_ticks = int(std::lround(m.slope * m.loop_ticks));
        if (gb_period > 1 && std::abs(gb_period - m.gb_loop_ticks) <= 32)
            m.gb_loop_ticks = gb_period;
        // Anchor the end to the GB phrase's start, rather than carrying an extra WSC rest into the next
        // phrase. The band has already located those rests within this loop.
        path[size_t(hi)] = path[size_t(m.loop_start)] + m.gb_loop_ticks;
        for (int t = hi - 1; t >= m.loop_start; --t)
            path[size_t(t)] = std::min(path[size_t(t)], path[size_t(t + 1)]);
    }
    m.anchors = {{0, 1}, {1, 2}};
    const int intro_end = m.loop_ticks > 0 ? m.loop_start : hi;
    if (intro_end > 1)
        simplify_path(path, 1, intro_end, 0.75, m.anchors);
    if (m.loop_ticks > 0) {
        Anchors loop = {{double(m.loop_start), double(path[size_t(m.loop_start)])}};
        simplify_path(path, m.loop_start, hi, 0.75, loop);
        for (const auto &[wt, gt] : loop)
            m.loop_anchors.emplace_back(wt - m.loop_start, gt - path[size_t(m.loop_start)]);
    }
    for (size_t c = 0; c < 3; ++c)
        m.voices[c] = uint8_t(pair[c] + scan.first_slot);
    return m;
}

// Refine short run-ups against the actual driver, so simplification never replaces the field's
// four-note timing with a worse approximation. Try one intro segment joining the fitted main phrase.
void refine_intro(app::GbSongMap &map, const std::vector<Pitches> &frames, const std::vector<int> &ticks,
                  int first, const std::vector<uint8_t> &rom) {
    if (map.loop_start < 3 || map.loop_start > 32)
        return;
    auto agreement = [&](const app::GbSongMap &candidate) {
        auto host = std::make_unique<emu::GbSoundHost>(rom, emu::saga1_sound_driver());
        host->init();
        host->request(candidate.gb_song);
        host->set_tick_loop(candidate.gb_loop_start(), candidate.gb_loop_ticks);
        std::array<int, 3> match{}, count{};
        int previous = 0;
        for (size_t f = 0; f < ticks.size(); ++f) {
            const int target = candidate.gb_ticks_by(ticks[f]);
            if (target < previous || target - previous > 4)
                return -1.0;
            host->frame_locked(target - previous, double(emu::GbApu::kClock) / kWscFrameRate);
            previous = target;
            host->apu.take();
            if (f + 1 < ticks.size() && ticks[f] == ticks[f + 1])
                continue;
            for (size_t c = 0; c < 3; ++c) {
                const int a = frames[f][size_t(candidate.voices[c] - first)];
                const int b = host->apu.channel_on(int(c)) && host->apu.volume(int(c))
                                  ? midi_note(131072.0 / (2048 - host->apu.frequency(int(c))))
                                  : -1;
                if (a < 0 && b < 0)
                    continue;
                ++count[c];
                match[c] += a >= 0 && b >= 0 && a == b;
            }
        }
        double score = 0;
        for (size_t c = 0; c < 3; ++c)
            score += count[c] ? double(match[c]) / count[c] : 0;
        return score;
    };
    const double origin = map.gb_loop_start();
    auto best = map;
    double score = agreement(best);
    for (int end = 2; end <= map.loop_start; ++end) {
        const double gb_end = origin + (end - map.loop_start) * map.slope;
        if (gb_end < 2)
            continue;
        auto candidate = map;
        candidate.anchors = {{0, 1}, {1, 2}, {double(end), gb_end}};
        const double value = agreement(candidate);
        if (value > score + 1e-9 ||
            (std::abs(value - score) < 1e-9 && candidate.anchors.size() < best.anchors.size())) {
            score = value;
            best = candidate;
        }
    }
    map = std::move(best);
}

void print_anchors(const Anchors &anchors, bool json = false, FILE *f = stdout) {
    std::fprintf(f, json ? "[" : "{");
    for (size_t i = 0; i < anchors.size(); ++i) {
        if (i)
            std::fprintf(f, ",");
        std::fprintf(f, json ? "[%.8g,%.8g]" : "{%.8g,%.8g}", anchors[i].first, anchors[i].second);
    }
    std::fprintf(f, json ? "]" : "}");
}

int align(int argc, char **argv) {
    if (argc < 8)
        return 2;
    const auto gb_rom = read_file(argv[3]);
    if (read_file(argv[2]).size() < 0x400000 || gb_rom.size() != 0x20000) {
        std::fprintf(stderr, "align: missing or wrong-size ROM\n");
        return 1;
    }
    const int ws = std::atoi(argv[6]), gs = std::atoi(argv[7]);
    auto wp = wsc_boot(argv[2], argv[4], std::atoi(argv[5]));
    // Find this request's slots before recording (some jingles use 0-3).
    game::Machine machine(*wp);
    const int record = machine.rd16(0xD000, uint16_t(0x02E0 + ws * 3));
    const int first = machine.rd16(0xD000, uint16_t(0x0500 + record * 6)) / 0x32;
    const int channels = machine.rd8(0xD000, uint16_t(0x02E2 + ws * 3));
    if (ws < 1 || ws > 180 || gs < 1 || gs > 17 || channels < 3 || channels > 4 || first > 5)
        return 2;
    std::vector<Pitches> pitches(1, Pitches{-1, -1, -1, -1});
    std::vector<Pitches> frame_pitches;
    std::vector<int> ticks;
    std::vector<int16_t> wsc_audio;
    const WscScan scan = scan_wsc_song(*wp, ws, int(kWscFrameRate * 240), 2, [&](int, int t) {
        if (size_t(t) >= pitches.size())
            pitches.resize(size_t(t + 1));
        pitches[size_t(t)] = wsc_pitches(*wp, first, channels);
        frame_pitches.push_back(pitches[size_t(t)]);
        ticks.push_back(t);
        auto a = wp->apu.take();
        wsc_audio.insert(wsc_audio.end(), a.begin(), a.end());
    });
    const int reference_ticks = std::max(12000, int(pitches.size() * 1.5) + 128);
    constexpr double cycles = double(emu::GbApu::kClock) / kWscFrameRate;
    const auto driver = emu::saga1_sound_driver();
    const auto reference = gb_reference(gb_rom, uint8_t(gs), reference_ticks);
    app::GbSongMap map =
        derive_map(pitches, reference.pitches, scan, reference.period, uint8_t(ws), uint8_t(gs));
    refine_intro(map, frame_pitches, ticks, first, gb_rom);
    // Replay the generated map, including its loop-state repetition, and measure the actual follower's
    // output over two WSC loops (or the complete non-looping song). Agreement is exact MIDI pitch;
    // pitch-class agreement is printed separately, never counted as an exact match.
    auto host = std::make_unique<emu::GbSoundHost>(gb_rom, driver);
    host->apu.set_sample_rate(24000);
    host->init();
    host->request(uint8_t(gs));
    if (map.loop_ticks)
        host->set_tick_loop(map.gb_loop_start(), map.gb_loop_ticks);
    std::array<int, 3> agree{}, octave{}, count{}, gb_silent{}, wsc_silent{};
    std::vector<int16_t> gb_audio(318 * 2, 0); // pause renders: one frame from tick to WSC volume write
    int previous = 0;
    for (size_t f = 0; f < ticks.size(); ++f) {
        const int target = map.gb_ticks_by(ticks[f]);
        if (target < previous || target - previous > 4) {
            std::fprintf(stderr, "align: nonmonotonic map or more than four GB ticks per frame\n");
            return 1;
        }
        if (!host->frame_locked(target - previous, cycles)) {
            std::fprintf(stderr,
                         "align: GB driver exceeded its frame budget at frame %zu, WSC tick %d, GB %d -> %d, "
                         "PC %04x; loop %d/%d\n",
                         f, ticks[f], previous, target, host->cpu.r.pc, map.gb_loop_start(),
                         map.gb_loop_ticks);
            return 1;
        }
        previous = target;
        auto audio = host->apu.take();
        gb_audio.insert(gb_audio.end(), audio.begin(), audio.end());
        if (f + 1 < ticks.size() && ticks[f] == ticks[f + 1])
            continue; // last frame of each WSC tick, as in the alignment reference
        for (size_t c = 0; c < 3; ++c) {
            const int a = frame_pitches[f][size_t(map.voices[c] - first)];
            const int b = host->apu.channel_on(int(c)) && host->apu.volume(int(c))
                              ? midi_note(131072.0 / (2048 - host->apu.frequency(int(c))))
                              : -1;
            if (a < 0 && b < 0)
                continue;
            ++count[c];
            gb_silent[c] += b < 0;
            wsc_silent[c] += a < 0;
            agree[c] += a >= 0 && b >= 0 && a == b;
            octave[c] += a >= 0 && b >= 0 && (a - b) % 12 == 0;
        }
    }
    std::printf("WSC %d / GB %d: slope %.1f, GB whole-state intro/loop %d/%d; WSC intro/loop %d/%d; %s\n", ws,
                gs, map.slope, reference.intro, reference.period, scan.intro_ticks, scan.loop_ticks,
                scan.ended ? "ends" : "loops");
    for (size_t c = 0; c < 3; ++c)
        std::printf("GB voice %zu natural intro/loop: %d/%d\n", c, reference.voice_intro[c],
                    reference.voice_period[c]);
    std::printf("map: {%d,%d,", ws, gs);
    print_anchors(map.anchors);
    std::printf(",%.1f,%d,%d,%d,%.1f,", map.slope, map.loop_start, map.loop_ticks, map.gb_loop_ticks,
                map.loop_phase);
    print_anchors(map.loop_anchors);
    std::printf(",{%d,%d,%d},%d},\n", map.voices[0], map.voices[1], map.voices[2], map.clock_slot);
    for (size_t c = 0; c < 3; ++c)
        std::printf("voice %zu -> slot %d: exact %.2f%%, pitch class %.2f%% (%d ticks; GB silent %d, WSC "
                    "silent %d)\n",
                    c, map.voices[c], count[c] ? 100.0 * agree[c] / count[c] : 0,
                    count[c] ? 100.0 * octave[c] / count[c] : 0, count[c], gb_silent[c], wsc_silent[c]);
    if (argc > 9) {
        FILE *f = std::fopen(argv[9], "w");
        if (!f)
            return 1;
        std::fprintf(f, "{\"wsc\":%d,\"gb\":%d,\"slope\":%.1f,\"anchors\":", ws, gs, map.slope);
        print_anchors(map.anchors, true, f);
        std::fprintf(
            f,
            ",\"loop_start\":%d,\"loop_ticks\":%d,\"gb_loop_ticks\":%d,\"loop_phase\":%.1f,\"loop_anchors\":",
            map.loop_start, map.loop_ticks, map.gb_loop_ticks, map.loop_phase);
        print_anchors(map.loop_anchors, true, f);
        std::fprintf(
            f, ",\"gb_intro\":%d,\"gb_period\":%d,\"voices\":[%d,%d,%d],\"clock_slot\":%d,\"exact\":[",
            reference.intro, reference.period, map.voices[0], map.voices[1], map.voices[2], map.clock_slot);
        for (size_t c = 0; c < 3; ++c)
            std::fprintf(f, "%s%.2f", c ? "," : "", count[c] ? 100.0 * agree[c] / count[c] : 0);
        std::fprintf(f, "],\"pitch_class\":[");
        for (size_t c = 0; c < 3; ++c)
            std::fprintf(f, "%s%.2f", c ? "," : "", count[c] ? 100.0 * octave[c] / count[c] : 0);
        std::fprintf(f, "],\"counts\":[%d,%d,%d],\"gb_voice_periods\":[%d,%d,%d]}\n", count[0], count[1],
                     count[2], reference.voice_period[0], reference.voice_period[1],
                     reference.voice_period[2]);
        std::fclose(f);
    }
    if (argc > 8) {
        const size_t n = std::min(wsc_audio.size(), gb_audio.size());
        wsc_audio.resize(n);
        double ew = 0, eg = 0;
        for (size_t i = 0; i < n; ++i) {
            ew += double(wsc_audio[i]) * wsc_audio[i];
            eg += double(gb_audio[i]) * gb_audio[i];
        }
        const double level = eg > 0 ? std::sqrt(ew / eg) : 0;
        if (const char *stems = std::getenv("SAWGOD_ALIGN_STEMS")) { // PREFIX: also each side alone, RMS-matched
            std::vector<int16_t> g(n);
            for (size_t i = 0; i < n; ++i)
                g[i] = int16_t(std::clamp(gb_audio[i] * level, -32768.0, 32767.0));
            if (!write_wav(std::string(stems) + "_wsc.wav", wsc_audio, 24000) ||
                !write_wav(std::string(stems) + "_gb.wav", g, 24000))
                return 1;
        }
        const double duration = double(n / 2) / 24000;
        const double seam = scan.loop_end_frame / kWscFrameRate;
        const std::vector<std::pair<double, double>> windows =
            duration < 6 ? std::vector<std::pair<double, double>>{{duration * 0.05, duration * 0.55}}
                         : std::vector<std::pair<double, double>>{
                               {0.05, std::min(2.0, duration * 0.25)},
                               {duration * 0.35, duration * 0.48},
                               map.loop_ticks ? std::make_pair(seam - 1, seam + 2)
                                              : std::make_pair(duration * 0.65, duration * 0.85)};
        for (const auto &[in, out] : windows)
            std::printf("swap: GB at %.3fs, WSC at %.3fs (0.5s crossfades)\n", std::max(0.0, in), out);
        for (size_t i = 0; i < n; i += 2) {
            const double t = double(i / 2) / 24000;
            double weight = 0;
            for (const auto &[in, out] : windows) {
                const double enter = std::clamp((t - std::max(0.0, in)) / 0.5, 0.0, 1.0);
                const double leave = std::clamp((out + 0.5 - t) / 0.5, 0.0, 1.0);
                weight = std::max(weight, std::min(enter, leave));
            }
            const double gw = std::sin(weight * 1.5707963267948966) * level;
            const double ww = std::cos(weight * 1.5707963267948966);
            for (size_t ch = 0; ch < 2; ++ch)
                wsc_audio[i + ch] =
                    int16_t(std::clamp(wsc_audio[i + ch] * ww + gb_audio[i + ch] * gw, -32768.0, 32767.0));
        }
        if (!write_wav(argv[8], wsc_audio, 24000))
            return 1;
    }
    return 0;
}

int pair_songs(int argc, char **argv) {
    if (argc < 7)
        return 2;
    const int song = std::atoi(argv[6]);
    const auto rom = read_file(argv[3]);
    auto wp = wsc_boot(argv[2], argv[4], std::atoi(argv[5]));
    game::Machine machine(*wp);
    const int record = machine.rd16(0xD000, uint16_t(0x02E0 + song * 3));
    const int first = machine.rd16(0xD000, uint16_t(0x0500 + record * 6)) / 0x32;
    const int channels = machine.rd8(0xD000, uint16_t(0x02E2 + song * 3));
    if (channels < 3 || channels > 4 || first > 5)
        return 2;
    std::vector<Pitches> pitches(1, Pitches{-1, -1, -1, -1});
    const auto scan = scan_wsc_song(*wp, song, int(kWscFrameRate * 240), 1, [&](int, int t) {
        if (size_t(t) >= pitches.size())
            pitches.resize(size_t(t + 1));
        pitches[size_t(t)] = wsc_pitches(*wp, first, channels);
        wp->apu.take();
    });
    for (int gb = 1; gb <= 17; ++gb) {
        const auto reference =
            gb_reference(rom, uint8_t(gb), std::max(12000, int(pitches.size() * 1.5) + 128));
        const auto map =
            derive_map(pitches, reference.pitches, scan, reference.period, uint8_t(song), uint8_t(gb));
        int exact = 0, pitch_class = 0, count = 0;
        for (size_t t = 1; t < pitches.size(); ++t) {
            int g = map.gb_ticks_by(int(t));
            if (map.loop_ticks && int(t) >= map.loop_start + map.loop_ticks)
                g = map.gb_ticks_by(int(t) - map.loop_ticks);
            if (g < 1 || g >= int(reference.pitches.size()))
                continue;
            for (size_t c = 0; c < 3; ++c) {
                const int a = pitches[t][size_t(map.voices[c] - first)], b = reference.pitches[size_t(g)][c];
                if (a < 0 && b < 0)
                    continue;
                ++count;
                exact += a >= 0 && b >= 0 && a == b;
                pitch_class += a >= 0 && b >= 0 && (a - b) % 12 == 0;
            }
        }
        std::printf("WSC %d / GB %2d: exact %.2f%%, pitch class %.2f%%\n", song, gb,
                    count ? 100.0 * exact / count : 0, count ? 100.0 * pitch_class / count : 0);
    }
    return 0;
}
