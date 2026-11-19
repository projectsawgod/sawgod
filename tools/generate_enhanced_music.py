#!/usr/bin/env python3
"""Compile casting and authored percussion into host C++ tables; no ROM data."""
import argparse
import json
import math
from pathlib import Path
from audio_casting import lanes, prepared
from audio_enhanced_percussion import hits
from audio_drums import KITS


def number(value, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError('Invalid finite casting number')
    return value


def integer(value, low, high):
    number(value, low, high)
    if not isinstance(value, int):
        raise ValueError('Invalid casting integer')
    return value


def generate(source, target):
    maps = json.loads(Path(source).read_text(encoding='utf-8'))
    cues = [int(s) for s, e in maps.items() if isinstance(e, dict) and e.get('voices') and all(v['slot'] < 4 for v in e['voices'])]
    out = ['static constexpr std::array<int, %d> music_cues = {%s};' % (len(cues), ', '.join(map(str, cues)))]
    songs = []
    for song, entry in maps.items():
        if not isinstance(entry, dict) or not (entry.get('enabled') or entry.get('drums_only')):
            continue
        if not entry.get('locked') and not entry.get('drums_only'):
            raise ValueError('Only locked castings may be enabled live')
        gain = number(entry.get('gain', 1 if entry.get('drums_only') else 0), 1e-12, 100)
        original = [] if entry.get('drums_only') else prepared(entry)
        if original and len({v['slot'] < 4 for v in original}) != 1:
            raise ValueError('Mixed stream slots')
        if entry.get('sections') or any(v.get('drum_notes') for v in original):
            raise ValueError('Unsupported song/noise section')
        if len({(v['slot'], v['instr'], v.get('noise', False)) for v in original}) != len(original):
            raise ValueError('Duplicate voice')
        for v in original:
            previous_tick = -1
            for section in v.get('sections', []):
                if 'tick' not in section or 'from' in section:
                    raise ValueError('Live sections need absolute driver ticks')
                tick = integer(section['tick'],0,2**30)
                if tick <= previous_tick:
                    raise ValueError('Section ticks must increase')
                previous_tick = tick
        rendered = lanes(original)
        if len(rendered) > 15:
            raise ValueError('Too many live voices')
        layered = any(v.get('layers') or v.get('envelopes') or any(set(s) - {'tick','amplitude','ramp_frames'} for s in v.get('sections', [])) for v in original)
        values = []
        for vi, v in enumerate(rendered):
            if (v.get('sections') or v['changes'] or v.get('envelopes')) and not entry.get('isolated_voices'):
                raise ValueError('Section gains require isolated voices')
            for key, low, high, default in [('slot',0,7,0),('instr',0,15,0),('program',0,127,0),('volume',0,127,100),('reverb',0,127,40),('transpose',-127,127,0),('attack',0,127,64),('release_cc',0,127,64),('bank',0,127,0),('bend_range',1,24,2)]:
                integer(v.get(key, default), low, high)
            gate = number(v.get('gate',1),1e-12,1)
            if gate != 1 and (v.get('legato') or v.get('release') or v.get('noise')):
                raise ValueError('Shortened gates need unextended melodic notes')
            if max(v.get('legato',0),v.get('release',0)) > 2:
                raise ValueError('Long articulation')
            amplitude = number(v.get('amplitude',1),1e-12,16)
            prefix = f'voice_{song}_{vi}'
            amps = []
            casts = []
            for section in original[v['source']].get('sections', []):
                if not layered and (set(section) - {'tick','amplitude','ramp_frames'} or 'amplitude' not in section):
                    raise ValueError('Unsupported live amplitude section')
                if 'amplitude' in section:
                    amps.append('{%d, %.17g, %d}' % (integer(section['tick'],0,2**30), number(section['amplitude'],1e-12,16), integer(section.get('ramp_frames',0),0,8)))
            for c in v['changes']:
                casts.append('{%d, %d, %d, %d, %d, %.17g, %s}' % (integer(c['tick'],0,2**30), integer(c['program'],0,127), integer(c.get('bank',0),0,127), integer(c['volume'],0,127), integer(c.get('transpose',0),-127,127), c['seat'], str(c['enabled']).lower()))
            envs = []
            for ei, env in enumerate(v.get('envelopes', [])):
                points = []
                for point in env['points']:
                    points.append('{%d, %.17g, %.17g, %.17g}' % (integer(point['tick'],0,2**30), number(point['value'],0,32), number(point.get('ramp_ms',point.get('ramp_frames',0)*13.25),0,10000), number(point.get('offset_ms',0),-10000,10000)))
                pname = f'{prefix}_points{ei}'
                out.append(f'static constexpr EnhancedGainPoint {pname}[] = {{'+','.join(points)+'};')
                envs.append('{%.17g, %d, %d, %s}' % (env.get('initial',1),env.get('loop_tick',0),env.get('loop_ticks',0),pname))
            names = []
            for suffix, typ, items in [('amplitudes','EnhancedAmplitudeSection',amps),('casts','EnhancedCast',casts),('envelopes','EnhancedGainEnvelope',envs),('notes','int',list(map(str,v.get('note_ticks',[]))))]:
                name = prefix+'_'+suffix if items else '{}'
                names.append(name)
                if items:
                    out.append(f'static constexpr {typ} {name}[] = {{'+','.join(items)+'};')
            a,c,e,n = names
            fields = [v['slot'],v['instr'],v['program'],v['volume'],v['reverb'],v.get('legato',0),v.get('release',0),v.get('bend_range',2)]
            values.append('    {%s, %s, %s, {38,38,42,46,49,40,45,36}, %.17g, %d, %d, %d, %.17g, %s, %d, %s, %s, %s, %s, %.17g},' % (','.join(map(str,fields)),str(v['follow']).lower(),str(bool(v.get('noise'))).lower(),amplitude,v.get('transpose',0),v.get('attack',64),v.get('release_cc',64),gate,a,v.get('bank',0),str(v['enabled']).lower(),c,e,n,v['seat']))
        out.extend([f'static constexpr std::array<EnhancedVoice, {len(values)}> voices_{song} = {{{{',*values,'}};'])
        dname = '{}'
        if entry.get('drums'):
            d = entry['drums']; dname = 'drums_'+song
            out.append(f'static constexpr EnhancedDrumHit hits_{song}[] = {{')
            for h in hits(int(song),d):
                out.append('    {%.17g,%d,%d,%d},' % (h['tick'],h['channel'],h['note'],h['velocity']))
            out.append('};')
            out.append('static constexpr EnhancedDrums %s = {%d,%d,%d,%.17g,hits_%s,%.17g,%d,%d,%.17g};' % (dname,KITS[d['kit']],d['loop_tick'],d['loop_ticks'],number(d['pcm_gain'],1e-12,100),song,d.get('first_part_db',0),d.get('first_part_start',0),d.get('first_part_end',0),d.get('ramp_ms',20)))
        songs.append('    {%s, %.17ff, voices_%s, %s, %s, %.17g, %s, %d, %d, %s},' % (song,gain,song,str(bool(entry.get('isolated_voices'))).lower(),dname,entry.get('mix_gain',1),str(bool(entry.get('drums_only'))).lower(),entry.get('cast_loop_tick',0),entry.get('cast_loop_ticks',0),str(layered).lower()))
    out.extend(['static const EnhancedSong songs[] = {',*songs,'};',''])
    Path(target).write_text('\n'.join(out),encoding='utf-8',newline='')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source'); p.add_argument('target')
    args = p.parse_args(); generate(args.source,args.target)
