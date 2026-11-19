"""Resolve section/layer casts and linear PCM envelopes for offline and live music."""
from copy import deepcopy
import math
import numpy as np
from audio_seats import voice_seat


def prepared(entry):
    voices = deepcopy(entry['voices'])
    for v in voices:
        if entry.get('cast_loop_ticks'):
            v['cast_loop_tick'] = entry['cast_loop_tick']
            v['cast_loop_ticks'] = entry['cast_loop_ticks']
        if v.pop('section_lift', False):
            db = entry['section_lift_db'] - (3 if (v['slot'],v['instr']) == (5,5) else 0)
            v.setdefault('envelopes', []).append(dict(loop_tick=577,loop_ticks=3072,
                points=[dict(tick=577,value=1,ramp_ms=20),
                        dict(tick=2329,value=10**(db/20),ramp_frames=2)]))
    return voices


def lanes(voices):
    result = []
    for source, voice in enumerate(voices):
        count = max([len(voice.get('layers', [{}]))] + [len(s.get('layers', [{}])) for s in voice.get('sections', [])])
        for layer in range(count):
            def resolve(config):
                available = config.get('layers', [{}])
                v = dict(config)
                v.pop('sections', None)
                v.pop('layers', None)
                v['enabled'] = layer < len(available)
                if v['enabled']:
                    v.update(available[layer])
                v['seat'] = voice_seat(v)
                return v
            current = dict(voice)
            base = resolve(current)
            changes = []
            for section in voice.get('sections', []):
                current.update(section)
                changes.append(dict(resolve(current), tick=section['tick']))
            result.append(dict(base, source=source, layer=layer, changes=changes))
    return result


def envelope(signal, lane, rows, rate, frame_s):
    """Multiply independent envelopes, including tails, at exact observed tick anchors."""
    gain = np.full(len(signal), lane.get('amplitude', 1), dtype=float)
    anchors = {}
    for r in rows:
        if int(r['slot']) == lane['slot']:
            anchors.setdefault(int(r['ticks']), int(r['frame']))
    for env in lane.get('envelopes', []):
        values = np.full(len(signal), env.get('initial', 1), dtype=float)
        points = list(env['points'])
        if env.get('loop_ticks'):
            original = list(points)
            for repeat in range(1, 1 + math.ceil(max(anchors) / env['loop_ticks'])):
                points.extend(dict(p, tick=p['tick'] + repeat * env['loop_ticks']) for p in original
                              if p['tick'] >= env['loop_tick'])
        for point in sorted(points, key=lambda p: p['tick'] + p.get('offset_ms', 0) / 1000):
            frame = next((anchors[t] for t in sorted(anchors) if t >= point['tick']), None)
            if frame is None:
                continue
            end = round((frame * frame_s + point.get('offset_ms', 0) / 1000) * rate)
            ramp = round(point.get('ramp_ms', point.get('ramp_frames', 0) * frame_s * 1000) * rate / 1000)
            start = max(0, min(len(values), end - ramp))
            end = max(0, min(len(values), end))
            if end > start:
                values[start:end] = np.linspace(values[start], point['value'], end - start, endpoint=False)
            values[end:] = point['value']
        gain *= values
    return signal * gain[:, None]
