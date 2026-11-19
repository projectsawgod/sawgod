"""Shared authored drum event stream and fixed PCM gain; no ROM or synth at build time."""
from pathlib import Path
import numpy as np
import audio_drums as drums

TAB_ROOT = Path(__file__).resolve().parent / 'audio_enhanced_drums'


def hits(song, config, tab_root=TAB_ROOT):
    bars, notes, levels, _ = drums.parse(tab_root / f'{song}.tab')
    grid = drums.SONGS[song]
    end = config['loop_tick'] + config['loop_ticks']
    result = []
    for bar, lines in sorted(bars.items()):
        if bar > grid['bars']:
            continue
        for letter, (pattern, pitches) in lines.items():
            pi = 0
            for cell, mark in enumerate(pattern):
                if mark == '.':
                    continue
                pitch = pitches[pi] if letter == 'T' else notes[letter]
                pi += letter == 'T'
                offset = cell * grid['bar_ticks'] / len(pattern)
                tick = drums.bar_tick(grid, bar) + offset
                if offset >= grid.get('short', {}).get(bar, grid['bar_ticks']) or tick >= end:
                    continue
                result.append(dict(tick=tick, bar=bar, letter=letter, note=pitch,
                    channel=0 if letter == 'T' else 9,
                    program=47 if letter == 'T' else drums.KITS[config['kit']],
                    velocity=min(127, levels[letter].get(mark, 22 + 15 * int(mark) if mark.isdigit() else 0))))
    return sorted(result, key=lambda h: h['tick'])


def events(song, config, rows, frames):
    """Place hits on the observed driver clock, repeating from the authored downbeat.

    The downbeat can precede the observed melodic loop crop by one tick. Repeat
    ticks, never rounded seconds; tempo/phase differences are retained each pass.
    """
    anchors = {}
    for r in rows:
        anchors.setdefault(int(r['ticks']), int(r['frame']))
    ticks = np.array(sorted(anchors))
    positions = np.array([anchors[t] for t in ticks])
    result = []
    for hit in hits(song, config):
        tick = hit['tick']
        while tick <= ticks[-1]:
            frame = float(np.interp(tick, ticks, positions))
            if frame < frames:
                result.append(dict(hit, driver_tick=tick, frame=frame, seconds=frame * drums.FRAME_S))
            if hit['tick'] < config['loop_tick']:
                break
            tick += config['loop_ticks']
    return sorted(result, key=lambda h: h['frame'])


def render(song, config, rows, frames, args, target, render_fluid):
    stream = events(song, config, rows, frames)
    drums.midi(stream, target, drums.KITS[config['kit']], frames * drums.FRAME_S)
    raw = render_fluid(target, target.with_suffix('.raw'), args) * config['pcm_gain']
    return apply_gain(raw, config, rows)


def apply_gain(signal, config, rows):
    FRAME_S, RATE = drums.FRAME_S, drums.RATE
    gains = np.ones(len(signal))
    boost = 10 ** (config.get('first_part_db', 0) / 20)
    if boost == 1:
        return signal
    anchors = {}
    for r in rows:
        anchors.setdefault(int(r['ticks']), int(r['frame']))
    ramp = round(config.get('ramp_ms', 20) * RATE / 1000)
    for tick in range(config['first_part_start'], max(anchors) + 1, config['loop_ticks']):
        for at, value in ((tick, boost), (tick + config['first_part_end'] - config['first_part_start'], 1)):
            frame = next((anchors[t] for t in sorted(anchors) if t >= at), None)
            if frame is None:
                continue
            end = min(len(gains), round(frame * FRAME_S * RATE))
            start = max(0, end - ramp)
            gains[start:end] = np.linspace(gains[start], value, end-start, endpoint=False)
            gains[end:] = value
    return signal * gains[:, None]
