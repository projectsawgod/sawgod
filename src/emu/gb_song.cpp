#include "emu/gb_song.h"

#include <algorithm>
#include <map>
#include <memory>

namespace sawgod::emu {

GbSoundDriver saga1_sound_driver() {
    GbSoundDriver d;
    d.bank = 7;
    d.init = 0x4000;
    d.play = 0x4003;
    d.song_request = 0xFFE0;
    // UpdateAudio adds the tempo hFFEA to the accumulator wCEC2 each frame and runs one tick (7:40CD
    // channels, 7:44BC) per 60 it holds: 7:404B is the jump back after each tick.
    d.tick_pc = 0x404B;
    d.tempo = 0xFFEA;
    d.tempo_accumulator = 0xCEC2;
    d.tick_divisor = 60;
    d.effect_call = 0x404D;
    d.effect_service = 0x6AA8;
    d.jingle_call = 0x4034;
    d.jingle_service = 0x6A44;
    d.effect_request = 0xFFE4;
    d.pulse_owner = 0xCE00;
    d.noise_owner = 0xCEC7;
    d.music_write_gate = 0x43E2;
    d.pulse_cache = 0xCE18;
    d.empty_effect = 0x6C57;
    // CE00-CE5F: the three music channels (0x20 bytes each: sequence pointer, timers, loop counters,
    // envelope); FFE0/FFE1: requested/current song; FFEA: tempo. Left out: the accumulator wCEC2, the sound
    // effect bytes (CE60-CEC1 saved music state, FFE2-FFE5) and the per-channel scratch (FFE6-FFE9).
    d.state = {{0xCE00, 0xCE60}, {0xFFE0, 0xFFE2}, {0xFFEA, 0xFFEB}};
    return d;
}

GbSoundHost::GbSoundHost(std::vector<uint8_t> rom, GbSoundDriver driver)
    : rom_(std::move(rom)), driver_(std::move(driver)) {
    if (rom_.size() < 0x8000)
        rom_.resize(0x8000, 0xFF);
}

uint8_t GbSoundHost::read(uint16_t a) {
    if (a < 0x4000)
        return rom_[a];
    if (a < 0x8000) {
        if (preserve_pulse_ && live_pulse_ && driver_.music_write_gate && a >= driver_.music_write_gate &&
            a < driver_.music_write_gate + 2)
            return 0; // let music refresh its register cache; divert the writes below
        if (split_services_ && ((a >= driver_.effect_call && a < driver_.effect_call + 3) ||
                                (a >= driver_.jingle_call && a < driver_.jingle_call + 3)))
            return 0; // NOP the two CALLs only while executing the borrowed-clock service
        const size_t off = size_t(bank_) * 0x4000 + (a - 0x4000);
        return off < rom_.size() ? rom_[off] : 0xFF;
    }
    if (a < 0xA000)
        return vram_[a - 0x8000];
    if (a < 0xC000)
        return xram_[a - 0xA000];
    if (a < 0xFE00)
        return wram_[(a - 0xC000) & 0x1FFF];
    if (a < 0xFF00)
        return 0xFF;
    if (a >= 0xFF80)
        return hram_[a - 0xFF80];
    if (a >= 0xFF10 && a <= 0xFF3F) {
        sync_apu();
        return apu.read(a);
    }
    if (a == 0xFF44) // LY: drivers that wait for a line see VBlank
        return 0x90;
    return io_[a - 0xFF00];
}

void GbSoundHost::write(uint16_t a, uint8_t v) {
    if (preserve_pulse_ && driver_.pulse_cache) {
        for (int ch = 0; ch < 3; ++ch)
            if (a >= 0xFF11 + ch * 5 && a <= 0xFF14 + ch * 5)
                wram_[size_t(driver_.pulse_cache - 0xC000 + ch * 0x20 + a - (0xFF11 + ch * 5))] = v;
    }
    if (preserve_pulse_ && live_pulse_ && a >= 0xFF11 && a <= 0xFF14 && driver_.pulse_cache) {
        wram_[size_t(driver_.pulse_cache - 0xC000 + a - 0xFF11)] = v;
        return; // underlying music evolves without touching the stolen oscillator
    }
    if (preserve_pulse_ && a == driver_.pulse_owner)
        v = uint8_t((v & 0xFE) | live_pulse_);
    if (a < 0x8000) { // MBC1
        if (a >= 0x2000 && a < 0x4000) {
            bank_ = v & 0x1F;
            if (bank_ == 0)
                bank_ = 1;
            bank_ %= int(rom_.size() / 0x4000);
        }
        return;
    }
    if (a < 0xA000)
        vram_[a - 0x8000] = v;
    else if (a < 0xC000)
        xram_[a - 0xA000] = v;
    else if (a < 0xFE00)
        wram_[(a - 0xC000) & 0x1FFF] = v;
    else if (a < 0xFF00)
        return;
    else if (a >= 0xFF80)
        hram_[a - 0xFF80] = v;
    else if (a >= 0xFF10 && a <= 0xFF3F) {
        sync_apu();
        if (locked_writes_)
            locked_writes_->push_back({cpu.cycles, a, v, apu.output_gain, apu.effect_channels});
        apu.write(a, v);
        if ((a == 0xFF14 || a == 0xFF19 || a == 0xFF1E || a == 0xFF23) && (v & 0x80)) {
            const int ch = a == 0xFF14 ? 0 : a == 0xFF19 ? 1 : a == 0xFF1E ? 2 : 3;
            ++triggers[ch];
        }
    } else
        io_[a - 0xFF00] = v;
    if (a == driver_.pulse_owner || a == driver_.noise_owner)
        update_gains();
}

void GbSoundHost::sync_apu() {
    if (cpu.cycles > apu_cycles_) {
        apu.run(int(cpu.cycles - apu_cycles_));
        apu_cycles_ = cpu.cycles;
    }
}

bool GbSoundHost::init() {
    std::fill(wram_.begin(), wram_.end(), 0);
    std::fill(std::begin(hram_), std::end(hram_), 0);
    effect_carry_ = 0;
    effect_frames_ = 0;
    song_tempo_ = -1;
    set_tick_loop(0, 0);
    apu.reset();
    cpu.r.sp = 0xDFFF;
    write(0x2000, driver_.bank);
    const bool ok = cpu.call(driver_.init);
    // ResetAudio marks music voices FF (including bit 0), but no ordinary script
    // exists yet. Native UpdateAudio clears that sentinel through an empty script.
    if (driver_.pulse_owner)
        write(driver_.pulse_owner, uint8_t(read(driver_.pulse_owner) & 0xFE));
    sync_apu();
    return ok;
}

bool GbSoundHost::frame() {
    const uint64_t start = cpu.cycles;
    write(0x2000, driver_.bank);
    const bool ok = cpu.call(driver_.play, kFrameCycles);
    if (cpu.cycles < start + kFrameCycles)
        cpu.cycles = start + kFrameCycles; // the rest of the frame: the game, which we don't run
    sync_apu();
    return ok;
}

bool GbSoundHost::frame_locked(int ticks, double cycles) {
    if (song_tempo_ < 0)
        song_tempo_ = read(driver_.tempo);
    // the accumulator at 0 and the tempo byte at ticks x divisor: the per-frame routine runs exactly `ticks`
    const uint8_t forced = uint8_t(ticks * driver_.tick_divisor);
    write(driver_.tempo_accumulator, 0);
    write(driver_.tempo, forced);
    const uint64_t start = cpu.cycles, sound_start = apu_cycles_;
    const GbApu before = apu;
    std::vector<ApuWrite> writes;
    locked_writes_ = &writes;
    write(0x2000, driver_.bank);
    // Keep a caller's observation hook; looping happens after the tick, before the next one.
    const auto observer = cpu.on_watch;
    const uint16_t watched = cpu.watch_pc;
    if (loop_length_ > 0) {
        cpu.watch_pc = driver_.tick_pc;
        cpu.on_watch = [&] {
            loop_tick();
            if (observer && watched == driver_.tick_pc)
                observer();
        };
    }
    const bool ok = cpu.call(driver_.play);
    locked_writes_ = nullptr;
    cpu.on_watch = observer;
    cpu.watch_pc = watched;
    const uint8_t now = read(driver_.tempo);
    if (now != forced) // the song set its tempo during a tick
        song_tempo_ = now;
    write(driver_.tempo, uint8_t(song_tempo_));
    cycle_carry_ += cycles;
    const uint64_t whole = uint64_t(cycle_carry_);
    cycle_carry_ -= double(whole);
    if (cpu.cycles > sound_start + whole) {
        // A burst of borrowed ticks can take longer than this frame on a real SM83 (notably song 5's
        // start). Keep every tick, but fit its register writes into the borrowed frame. This prevents
        // both a truncated routine and extra audio samples that would drift away from the WSC clock.
        apu = before;
        const uint64_t spent = cpu.cycles - sound_start;
        uint64_t at = 0;
        for (const auto &event : writes) {
            const uint64_t time = (event.cycle - sound_start) * whole / spent;
            apu.run(int(time - at));
            apu.output_gain = event.gains;
            apu.effect_channels = event.effect_channels;
            apu.write(event.addr, event.value);
            at = time;
        }
        apu.run(int(whole - at));
        apu_cycles_ = cpu.cycles;
    } else {
        cpu.cycles = start + whole;
        sync_apu();
    }
    return ok;
}

uint8_t GbSoundHost::effect_owners() const {
    auto byte = [&](uint16_t a) { return a ? wram_[(a - 0xC000) & 0x1FFF] : uint8_t(0); };
    return uint8_t((byte(driver_.pulse_owner) & 1) | ((byte(driver_.noise_owner) & 1) << 3));
}

void GbSoundHost::update_gains() {
    const uint8_t owners = effect_owners();
    apu.effect_channels = owners;
    for (size_t ch = 0; ch < 4; ++ch)
        apu.output_gain[ch] = (owners & (1 << ch))                    ? effect_gain_
                              : ch == 3 || (music_mutes_ & (1 << ch)) ? 0
                                                                      : music_gain_ * music_levels_[ch];
}

void GbSoundHost::gains(float music, float effects, uint8_t music_mutes, std::array<float, 3> music_levels) {
    music_gain_ = music;
    effect_gain_ = effects;
    music_mutes_ = music_mutes;
    music_levels_ = music_levels;
    update_gains();
}

// Execute a service, then replay its writes within this slice of real APU time. A
// burst may take longer than the slice on an SM83; its writes are compressed in
// order, never allowed to add samples or move the independent effect deadline.
bool GbSoundHost::timed_call(uint16_t pc, int cycles) {
    const uint64_t start = cpu.cycles;
    const GbApu before = apu;
    std::vector<ApuWrite> writes;
    locked_writes_ = &writes;
    write(0x2000, driver_.bank);
    const bool ok = cpu.call(pc);
    sync_apu();
    const auto final_gains = apu.output_gain;
    const auto final_effects = apu.effect_channels;
    locked_writes_ = nullptr;
    apu = before;
    uint64_t at = 0;
    const uint64_t spent = cpu.cycles - start;
    for (const auto &event : writes) {
        const uint64_t time =
            spent > uint64_t(cycles) ? (event.cycle - start) * uint64_t(cycles) / spent : event.cycle - start;
        apu.run(int(time - at));
        apu.output_gain = event.gains;
        apu.effect_channels = event.effect_channels;
        apu.write(event.addr, event.value);
        at = time;
    }
    apu.run(cycles - int(at));
    apu.output_gain = final_gains;
    apu.effect_channels = final_effects;
    apu_cycles_ = cpu.cycles;
    return ok;
}

bool GbSoundHost::frame_dual(int ticks, double cycles) {
    if (!driver_.effect_service)
        return frame_locked(ticks, cycles);
    cycle_carry_ += cycles;
    int remaining = int(cycle_carry_);
    cycle_carry_ -= remaining;
    bool ok = true, first = true;
    while (remaining > 0) {
        const int span = std::min(remaining, kFrameCycles - int(effect_carry_));
        split_services_ = true;
        live_pulse_ = effect_owners() & 1;
        preserve_pulse_ = true;
        if (first) {
            if (song_tempo_ < 0)
                song_tempo_ = read(driver_.tempo);
            // Same borrowed-clock tick loop as frame_locked, with both services separated.
            write(driver_.tempo_accumulator, 0);
            const uint8_t forced = uint8_t(ticks * driver_.tick_divisor);
            write(driver_.tempo, forced);
            const auto observer = cpu.on_watch;
            const uint16_t watched = cpu.watch_pc;
            cpu.watch_pc = driver_.tick_pc;
            cpu.on_watch = [&] {
                if (loop_length_ > 0)
                    loop_tick();
                if (observer && watched == driver_.tick_pc)
                    observer();
            };
            ok &= timed_call(driver_.play, span);
            cpu.on_watch = observer;
            cpu.watch_pc = watched;
            const uint8_t now = read(driver_.tempo);
            if (now != forced)
                song_tempo_ = now;
            write(driver_.tempo, uint8_t(song_tempo_));
            first = false;
        } else {
            apu.run(span);
        }
        preserve_pulse_ = false;
        split_services_ = false;
        remaining -= span;
        effect_carry_ += span;
        if (effect_carry_ >= kFrameCycles) {
            effect_carry_ -= kFrameCycles;
            // Service at the deadline; its writes use zero elapsed output time.
            ok &= timed_call(driver_.effect_service, 0);
            ++effect_frames_;
        }
    }
    return ok;
}

bool GbSoundHost::begin_jingle(uint8_t song) {
    if (!driver_.jingle_service)
        return false;
    if (read(0xFFE3) == 0) {
        saved_loop_ = {loop_start_, loop_length_, locked_ticks_, loop_state_};
        for (size_t i = 0; i < saved_wave_.size(); ++i)
            saved_wave_[i] = apu.read(uint16_t(0xFF30 + i));
    }
    write(0xFFE2, song);
    live_pulse_ = effect_owners() & 1;
    preserve_pulse_ = true;
    const bool ok = timed_call(driver_.jingle_service, 0);
    preserve_pulse_ = false;
    song_tempo_ = read(driver_.tempo);
    set_tick_loop(0, 0);
    return ok;
}

bool GbSoundHost::end_jingle() {
    if (!driver_.jingle_service || read(0xFFE3) == 0)
        return false;
    // WSC lifetime is authoritative, including a jingle displaced by another cue.
    for (uint16_t a : {uint16_t(0xCE00), uint16_t(0xCE20), uint16_t(0xCE40)})
        write(a, uint8_t(read(a) | 0x80));
    live_pulse_ = effect_owners() & 1;
    preserve_pulse_ = true;
    const bool ok = timed_call(driver_.jingle_service, 0);
    preserve_pulse_ = false;
    song_tempo_ = read(driver_.tempo);
    loop_start_ = saved_loop_.start;
    loop_length_ = saved_loop_.length;
    locked_ticks_ = saved_loop_.ticks;
    loop_state_ = std::move(saved_loop_.state);
    // The driver restores bytecode state, but held notes can compare equal to
    // their saved caches and leave the jingle's hardware pitch playing.
    for (int ch = 0; ch < 3; ++ch) {
        if (ch == 0 && (effect_owners() & 1))
            continue;
        for (int r = 0; r < 4; ++r) {
            const uint8_t value = read(uint16_t(driver_.pulse_cache + ch * 0x20 + r));
            write(uint16_t(0xFF11 + ch * 5 + r), uint8_t(value | (r == 3 ? 0x80 : 0)));
        }
    }
    for (size_t i = 0; i < saved_wave_.size(); ++i)
        write(uint16_t(0xFF30 + i), saved_wave_[i]);
    return ok;
}

bool GbSoundHost::saved_music_ticks(int ticks) {
    if (read(0xFFE3) == 0 || ticks == 0)
        return true;
    // Evolve the saved music on the WSC clock while the jingle owns the audible
    // music voices. Ordinary effect registers/cursors and the APU never rewind.
    std::array<uint8_t, 0x60> current;
    const uint8_t pulse = effect_owners() & 1;
    for (size_t i = 0; i < current.size(); ++i) {
        current[i] = read(uint16_t(0xCE00 + i));
        write(uint16_t(0xCE00 + i), read(uint16_t(0xCE60 + i)));
    }
    write(0xCE00, uint8_t((read(0xCE00) & 0xFE) | pulse));
    const uint8_t song = read(0xFFE0), playing = read(0xFFE1), tempo = read(driver_.tempo);
    const int song_tempo = song_tempo_;
    const Loop loop = {loop_start_, loop_length_, locked_ticks_, loop_state_};
    write(0xFFE0, read(0xCEC1));
    write(0xFFE1, read(0xCEC1));
    write(driver_.tempo, read(0xCEC0));
    song_tempo_ = read(driver_.tempo);
    loop_start_ = saved_loop_.start;
    loop_length_ = saved_loop_.length;
    locked_ticks_ = saved_loop_.ticks;
    loop_state_ = saved_loop_.state;
    const GbApu before = apu;
    const std::array<uint32_t, 4> previous_triggers = {triggers[0], triggers[1], triggers[2], triggers[3]};
    const auto frames = effect_frames_;
    const double carry = effect_carry_, fractional = cycle_carry_;
    effect_carry_ = cycle_carry_ = 0;
    // frame_dual executes nothing for a zero span; use a one-cycle discarded slice.
    const bool ok = frame_dual(ticks, 1);
    for (size_t i = 0; i < saved_wave_.size(); ++i)
        saved_wave_[i] = apu.read(uint16_t(0xFF30 + i));
    apu = before;
    for (size_t ch = 0; ch < 4; ++ch)
        triggers[ch] = previous_triggers[ch];
    effect_frames_ = frames;
    effect_carry_ = carry;
    cycle_carry_ = fractional;
    saved_loop_ = {loop_start_, loop_length_, locked_ticks_, loop_state_};
    for (size_t i = 0; i < current.size(); ++i) {
        write(uint16_t(0xCE60 + i), read(uint16_t(0xCE00 + i)));
        write(uint16_t(0xCE00 + i), current[i]);
    }
    write(0xFFE0, song);
    write(0xFFE1, playing);
    write(driver_.tempo, tempo);
    song_tempo_ = song_tempo;
    loop_start_ = loop.start;
    loop_length_ = loop.length;
    locked_ticks_ = loop.ticks;
    loop_state_ = loop.state;
    update_gains();
    return ok;
}

void GbSoundHost::stop_music() {
    write(0xFFE2, 0);
    write(0xFFE3, 0);
    request(0);
    write(0xFFE1, 0xFF); // force silent song to replace even an already silent request
    set_tick_loop(0, 0);
}

void GbSoundHost::cancel_effects() {
    if (!driver_.empty_effect)
        return;
    write(driver_.effect_request, 0);
    for (uint16_t a : {uint16_t(0xFFEB), uint16_t(0xFFED)}) {
        write(a, uint8_t(driver_.empty_effect));
        write(uint16_t(a + 1), uint8_t(driver_.empty_effect >> 8));
    }
    write(0xCEC3, 1);
    write(0xCEC4, 1);
    timed_call(driver_.effect_service, 0);
}

void GbSoundHost::set_tick_loop(int start_ticks, int length_ticks) {
    loop_start_ = start_ticks;
    loop_length_ = length_ticks;
    locked_ticks_ = 0;
    loop_state_.clear();
    if (start_ticks == 0)
        loop_state_ = driver_state();
}

void GbSoundHost::loop_tick() {
    ++locked_ticks_;
    if (locked_ticks_ == loop_start_)
        loop_state_ = driver_state();
    if (loop_state_.empty() || locked_ticks_ <= loop_start_ ||
        (locked_ticks_ - loop_start_) % loop_length_ != 0)
        return;
    const uint8_t pulse = effect_owners() & 1;
    size_t i = 0;
    for (const auto &[from, to] : driver_.state)
        for (uint32_t a = from; a < to; ++a, ++i)
            if (a != driver_.tempo) // this frame's forced tempo must survive a wrap within the frame
                write(uint16_t(a),
                      a == driver_.pulse_owner ? uint8_t((loop_state_[i] & 0xFE) | pulse) : loop_state_[i]);
}

std::vector<uint8_t> GbSoundHost::driver_state() const {
    std::vector<uint8_t> s;
    for (const auto &[from, to] : driver_.state)
        for (uint32_t a = from; a < to; ++a)
            s.push_back(a >= 0xFF80 ? hram_[a - 0xFF80] : wram_[(a - 0xC000) & 0x1FFF]);
    return s;
}

GbSongRender render_gb_song(const std::vector<uint8_t> &rom, const GbSoundDriver &driver, uint8_t song,
                            int loops, int max_frames) {
    GbSongRender out;
    auto host = std::make_unique<GbSoundHost>(rom, driver);
    host->init();
    // one tick in the first frame (the song sets its own tempo in it): the reset tempo would run several,
    // and in the game the previous song's tempo does -- a start that varies with what played before
    host->write(driver.tempo, uint8_t(driver.tick_divisor));
    host->request(song);
    std::map<std::vector<uint8_t>, int> seen; // state after a tick -> tick number
    std::vector<int> tick_frame;              // the frame each tick ran in
    int ticks = 0, frame = 0, end_tick = -1;
    host->cpu.watch_pc = driver.tick_pc;
    host->cpu.on_watch = [&] {
        tick_frame.push_back(frame);
        ++ticks;
        if (out.intro_ticks >= 0)
            return;
        auto [it, fresh] = seen.emplace(host->driver_state(), ticks);
        if (fresh)
            return;
        out.intro_ticks = it->second;
        out.loop_ticks = ticks - it->second;
        out.tempo = host->read(driver.tempo);
        end_tick = out.intro_ticks + out.loop_ticks * loops;
        seen.clear();
    };
    std::vector<int64_t> sample_at; // stereo-sample count at the start of each frame
    uint32_t prev_trig[4] = {};
    for (; frame < max_frames; ++frame) {
        if (end_tick >= 0 && ticks >= end_tick)
            break;
        sample_at.push_back(int64_t(out.samples.size() / 2));
        host->frame();
        std::vector<int16_t> s = host->apu.take();
        out.samples.insert(out.samples.end(), s.begin(), s.end());
        std::array<GbSongRender::ChannelFrame, 4> cf{};
        for (int ch = 0; ch < 4; ++ch) {
            cf[size_t(ch)] = {uint16_t(host->apu.frequency(ch)), uint8_t(host->apu.volume(ch)),
                              host->apu.channel_on(ch), host->triggers[ch] != prev_trig[ch]};
            prev_trig[ch] = host->triggers[ch];
        }
        out.frames.push_back(cf);
        out.ticks_at_frame_end.push_back(ticks);
    }
    if (out.intro_ticks >= 0 && out.tempo > 0) {
        // tick n (1-based) ran in tick_frame[n - 1]: the state after tick intro_ticks repeats, so the loop's
        // first new tick is intro_ticks + 1
        out.loop_start_frame = tick_frame[size_t(out.intro_ticks)];
        out.loop_frames = double(out.loop_ticks) * driver.tick_divisor / out.tempo;
        out.loop_start_sample = sample_at[size_t(out.loop_start_frame)];
        out.loop_length_samples =
            int64_t(out.loop_frames * GbSoundHost::kFrameCycles * GbApu::kSampleRate / GbApu::kClock + 0.5);
    }
    return out;
}

GbSongRender render_gb_song_locked(const std::vector<uint8_t> &rom, const GbSoundDriver &driver, uint8_t song,
                                   const std::vector<uint8_t> &schedule, double frame_cycles) {
    GbSongRender out;
    auto host = std::make_unique<GbSoundHost>(rom, driver);
    host->init();
    host->request(song);
    std::map<std::vector<uint8_t>, int> seen;
    std::vector<int> tick_frame;
    int ticks = 0, frame = 0;
    host->cpu.watch_pc = driver.tick_pc;
    host->cpu.on_watch = [&] {
        tick_frame.push_back(frame);
        ++ticks;
        if (out.intro_ticks >= 0)
            return;
        auto [it, fresh] = seen.emplace(host->driver_state(), ticks);
        if (!fresh) {
            out.intro_ticks = it->second;
            out.loop_ticks = ticks - it->second;
        }
    };
    std::vector<int64_t> sample_at;
    uint32_t prev_trig[4] = {};
    for (; frame < int(schedule.size()); ++frame) {
        sample_at.push_back(int64_t(out.samples.size() / 2));
        host->frame_locked(schedule[size_t(frame)], frame_cycles);
        std::vector<int16_t> s = host->apu.take();
        out.samples.insert(out.samples.end(), s.begin(), s.end());
        std::array<GbSongRender::ChannelFrame, 4> cf{};
        for (int ch = 0; ch < 4; ++ch) {
            cf[size_t(ch)] = {uint16_t(host->apu.frequency(ch)), uint8_t(host->apu.volume(ch)),
                              host->apu.channel_on(ch), host->triggers[ch] != prev_trig[ch]};
            prev_trig[ch] = host->triggers[ch];
        }
        out.frames.push_back(cf);
        out.ticks_at_frame_end.push_back(ticks);
    }
    if (out.intro_ticks >= 0 && size_t(out.intro_ticks + out.loop_ticks) < tick_frame.size()) {
        out.loop_start_frame = tick_frame[size_t(out.intro_ticks)];
        out.loop_frames = tick_frame[size_t(out.intro_ticks + out.loop_ticks)] - out.loop_start_frame;
        out.loop_start_sample = sample_at[size_t(out.loop_start_frame)];
    }
    return out;
}

} // namespace sawgod::emu
