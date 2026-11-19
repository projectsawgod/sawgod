// Enhanced-audio spike: the WSC driver's note data as a per-frame event log, for a MIDI
// rendering through FluidSynth + a General MIDI soundfont.
//
//   sawgod-audio-spike wsc-events WSC_ROM SONG OUT_PREFIX [LOOPS [SLOT INSTR]]
//       runs only the sound driver on a fresh machine (no game execution, as gb-cues), the song's intro and
//       LOOPS loops (default 2); writes OUT_PREFIX.wav (the WSC original, 24 kHz), OUT_PREFIX_events.csv
//       (one row per frame and song channel) and OUT_PREFIX_waves.csv (the 16 instrument waveforms)
//       SLOT INSTR isolates that pair using playback gains; all driver and APU
//       phases/readbacks keep advancing. Use the same frame crop for every stem.
//
// A host-side observer only: after each driver frame it reads the channel records (DS:0x62C + slot*0x32).
// A channel decoded new bytecode this frame when its step counter (+0x00) moved; the op that stopped the
// decoder sits just before the new step (word-sized ops; the tie 0x7F takes two words), which tells a
// note from a rest (pitch nibble >= 0xC) or a duration-only hold (0xA7).

int wsc_events(int argc, char **argv) {
    if (argc < 5)
        return 2;
    const std::vector<uint8_t> rom = read_file(argv[2]);
    const int song = std::atoi(argv[3]);
    const std::string prefix = argv[4];
    const int loops = argc > 5 ? std::atoi(argv[5]) : 2;
    const int isolate_slot = argc > 6 ? std::atoi(argv[6]) : -1;
    const int isolate_instrument = argc > 7 ? std::atoi(argv[7]) : -1;
    if (rom.size() < 0x400000 || song < 0 || song > 180 || loops < 2)
        return 2;
    if ((argc > 6 && argc != 8) || isolate_slot > 7 || isolate_instrument > 15 ||
        (argc == 8 && (isolate_slot < 0 || isolate_instrument < 0)))
        return 2;
    auto w = std::make_unique<emu::Wsc>(rom, 0);
    game::Machine machine(*w);
    const int record = machine.rd16(0xD000, uint16_t(0x02E0 + song * 3));
    const int first = machine.rd16(0xD000, uint16_t(0x0500 + record * 6)) / 0x32;
    const int channels = machine.rd8(0xD000, uint16_t(0x02E2 + song * 3));
    if (isolate_slot >= 0 && (isolate_slot < first || isolate_slot >= first + channels))
        return 2;

    if (FILE *f = std::fopen((prefix + "_waves.csv").c_str(), "w")) {
        std::fprintf(f, "instrument,samples\n");
        for (int instr = 0; instr < 16; ++instr) {
            std::fprintf(f, "%d,", instr);
            for (int i = 0; i < 16; ++i) { // 32 4-bit samples, low nibble first
                const uint8_t b = machine.rd8(0xD000, uint16_t(0x00F0 + instr * 16 + i));
                std::fprintf(f, "%d %d%s", b & 0xF, b >> 4, i < 15 ? " " : "\n");
            }
        }
        std::fclose(f);
    }

    FILE *log = std::fopen((prefix + "_events.csv").c_str(), "w");
    if (!log)
        return 1;
    // event: N note-on, R rest, H hold (0xA7), T tie (0x7F, absolute pitch), E end, '-' nothing decoded
    std::fprintf(log, "frame,ticks,slot,hw,event,op,dur,pitch,port_period,base_vol,env_vol,port_vol,instr,"
                      "noise,noise_param,vib,sweep,lfo,step,remaining,track\n");
    std::vector<int> steps(8, 0xFFFF);
    std::vector<int16_t> audio;
    auto rd = [&](int a) { return int(w->ram[size_t(a)]); };
    const WscScan r = scan_wsc_song(*w, song, int(kWscFrameRate * 900), loops, [&](int f, int ticks) {
        std::vector<int16_t> a = w->apu.take();
        audio.insert(audio.end(), a.begin(), a.end());
        for (int slot = first; slot < first + channels; ++slot) {
            const int base = 0x62C + slot * 0x32;
            const int step = rd(base) | rd(base + 1) << 8;
            const int hw = rd(base + 2) & 3;
            const int track = rd(base + 3) | rd(base + 4) << 8;
            char event = '-';
            int op = -1, dur = 0;
            if (step != steps[size_t(slot)]) {
                if (step == 0xFFFF) {
                    event = 'E';
                } else {
                    op = machine.rd8(0xD000, uint16_t(track + (step - 1) * 2));
                    dur = machine.rd8(0xD000, uint16_t(track + (step - 1) * 2 + 1));
                    const int tie = machine.rd8(0xD000, uint16_t(track + (step - 2) * 2));
                    if (step >= 2 && tie == 0x7F) {
                        op = 0x7F;
                        dur = machine.rd8(0xD000, uint16_t(track + (step - 2) * 2 + 1));
                        event = 'T';
                    } else if (op < 0xA0) {
                        event = (op & 0xF) >= 0xC ? 'R' : 'N';
                    } else if (op == 0xA7) {
                        event = 'H';
                    }
                }
            }
            steps[size_t(slot)] = step;
            const int pitch = rd(base + 0x10) | rd(base + 0x11) << 8;
            const int period = (w->io[size_t(0x80 + hw * 2)] | w->io[size_t(0x81 + hw * 2)] << 8) & 0x7FF;
            std::fprintf(log, "%d,%d,%d,%d,%c,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", f, ticks, slot, hw,
                         event, op, dur, pitch, period, rd(base + 8), rd(base + 0x28), w->io[size_t(0x88 + hw)],
                         rd(base + 0x27), rd(base + 0xB), rd(base + 0xC), rd(base + 2) >> 4, rd(base + 0xA),
                         rd(base + 6), step, rd(base + 9), track);
        }
    }, true, isolate_slot, isolate_instrument);
    std::fclose(log);
    // Report the observed first loop, rather than extrapolating from the initial tempo.
    const double loop_frames = r.loop_end_frame >= 0 ? r.loop_end_frame - r.loop_start_frame : 0;
    std::printf("song %d: slots %d-%d, rate %d, intro %d ticks, loop %d ticks; loop starts in frame %d, "
                "ends in frame %d (%.4f frames); frame rate %.6f Hz\n",
                song, first, first + channels - 1, r.rate, r.intro_ticks, r.loop_ticks, r.loop_start_frame,
                r.loop_end_frame, loop_frames, kWscFrameRate);
    std::printf("ended %d; exact state loop %d frames\n", int(r.ended), r.exact_loop_frames);
    return write_wav(prefix + ".wav", audio, emu::Apu::kSampleRate) ? 0 : 1;
}
