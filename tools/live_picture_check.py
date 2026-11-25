#!/usr/bin/env python3
"""Record/compare final RGB hashes through draw_frame and real native battle entry.

python3 tools/live_picture_check.py --binary build/linux-release/sawgod --output tmp/live-before --record
python3 tools/live_picture_check.py --binary build/linux-release/sawgod --output tmp/live-after --baseline tmp/live-before
Uses SAWGOD_WSC_ROM; the developer fixtures are required. Defaults to SDL's CPU
renderer under dummy video. --gpu uses offscreen Vulkan (software Vulkan is OK).
"""
import argparse
import csv
import hashlib
import json
import math
import re
import os
from pathlib import Path
import resource
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--record', action='store_true')
    parser.add_argument('--gpu', action='store_true')
    parser.add_argument('--walk', action='store_true', help='move during field play (town GPU motion proof)')
    parser.add_argument('--modes', nargs='+', default=['enhanced', 'wsc', 'gb'])
    parser.add_argument('--scenes', nargs='+', default=['town', 'world'])
    parser.add_argument('--check-reuse', action='store_true', help='compare reuse against forced re-description')
    parser.add_argument('--pacing', nargs='+', choices=['smooth', 'lospec'], default=['smooth'])
    parser.add_argument('--redescribe', action='store_true', help='reference: bypass scene description reuse')
    parser.add_argument('--rates', nargs='+', type=int, default=[60, 120])
    args = parser.parse_args()
    if args.check_reuse:
        base = args.output.resolve()
        common = [sys.executable, str(Path(__file__).resolve()), '--binary', str(args.binary.resolve()),
                  '--modes', *args.modes, '--scenes', *args.scenes, '--rates', *map(str, args.rates),
                  '--pacing', *args.pacing]
        if args.gpu:
            common.append('--gpu')
        if args.walk:
            common.append('--walk')
        for extra in (['--output', str(base / 'reference'), '--redescribe', '--record'],
                      ['--output', str(base / 'cached'), '--baseline', str(base / 'reference')]):
            result = subprocess.run(common + extra)
            if result.returncode:
                return result.returncode
        return 0
    if not args.record and not args.baseline:
        parser.error('use --record to establish a baseline, or --baseline to compare it')
    rom = os.environ.get('SAWGOD_WSC_ROM')
    if not rom:
        print('SKIP: SAWGOD_WSC_ROM is required')
        return 77
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixtures = ROOT / 'tests/fixtures/live_picture'
    expected = json.loads((args.baseline / 'pictures.json').read_text(encoding='utf-8')) if args.baseline else {}
    summary = {}
    for scene in args.scenes:
        for mode in args.modes:
            for pacing in args.pacing:
                for hz in args.rates:
                    key = f'{scene}-{mode}-{hz}' + ('' if pacing == 'smooth' else '-lospec')
                    fps = hz if pacing == 'smooth' else hz / max(2, 2 * math.ceil(hz / 120))
                    views = round(fps * 6)
                    config = output / (key + '.toml')
                    config.write_text(f'[video]\ngraphics="{mode}"\nscreen_mode="ips"\npacing="{pacing}"\n'
                                      '[audio]\nmusic_mode="wsc"\nsoundfont=""\n', encoding='utf-8', newline='')
                    trace = output / (key + '.csv')
                    # Enough real field play to include a steady sample, the complete forty-tick wipe,
                    # and the first battle pictures. Native event 8 is the ordinary battle dispatcher.
                    cmd = [str(args.binary.resolve()), '--rom', rom, '--no-save', '--config', str(config),
                           '--inputs', str(fixtures / ('walk.inputs' if args.walk else 'load.inputs')), '--shot-store', str(fixtures / scene),
                           '--scale', '1', '--frames', str(views), '--live-warmup', '2400',
                           '--shot-display-hz', str(hz), '--live-picture-log', str(trace),
                           '--shot-hold', 'D0AE=FF', '--shot-poke', 'C825=1@2700',
                           '--shot-poke', 'C826=8@2700', '--shot-poke', 'C827=B@2700']
                    if mode == 'swap':
                        cmd += ['--shot-step', f'{hz}:-1', '--shot-step', f'{hz}:-1',
                                '--shot-step', f'{hz * 4 - 10}:1', '--shot-step', f'{hz * 4 - 10}:1']
                    env = dict(os.environ, SDL_VIDEODRIVER='offscreen' if args.gpu else 'dummy',
                               SDL_AUDIODRIVER='dummy', LP_NUM_THREADS=os.environ.get('LP_NUM_THREADS', '1'), OMP_NUM_THREADS='1',
                               SAWGOD_HOME=str(output / 'data'))
                    if args.redescribe:
                        env['SAWGOD_SCENE_REDESCRIBE'] = '1'
                    if args.walk:
                        env['SAWGOD_MOTION_TRACE'] = str(output / (key + '-motion.csv'))
                    if args.gpu:
                        env.pop('SDL_RENDER_DRIVER', None)
                        cmd += ['--renderer', 'gpu']
                    else:
                        env['SDL_RENDER_DRIVER'] = 'software'
                        cmd += ['--renderer', 'legacy']
                    start = time.monotonic()
                    cpu_before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    with (output / (key + '.log')).open('w', encoding='utf-8', newline='') as log:
                        result = subprocess.run(cmd, env=env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
                    cpu_after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    if result.returncode:
                        raise RuntimeError(f'{key}: exit {result.returncode}, see log')
                    if args.gpu:
                        assert 'GPU scene passes ready' in (output / (key + '.log')).read_text(encoding='utf-8'), key
                    with trace.open(encoding='utf-8', newline='') as f:
                        rows = list(csv.DictReader(f))
                    identity = [{k: r[k] for k in ('view', 'frame', 'map', 'scene', 'wipe', 'picture')} for r in rows]
                    log_text = (output / (key + '.log')).read_text(encoding='utf-8')
                    descriptions = re.search(r'scene descriptions: (\d+), reused: (\d+)', log_text)
                    if descriptions:
                        assert int(descriptions[1]) <= int(rows[-1]['frame']) + 1, key
                    wipe = [r for r in rows if r['wipe'] == '1']
                    fields = [r for r in rows if r['retained'] == '1' and int(r['view']) >= 0]
                    if len(rows) < views + 1 or len(wipe) < fps // 3 or len(fields) < fps * 2:
                        raise AssertionError(f'{key}: missing loop/field/wipe coverage: {len(rows)}/{len(fields)}/{len(wipe)}')
                    assert all(int(r['map']) == (10 if scene == 'town' else 102) for r in fields), key
                    # The field must precede the wipe; the last wipe must be followed by battle.
                    assert int(fields[0]['view']) < int(wipe[0]['view'])
                    assert any(int(r['view']) > int(wipe[-1]['view']) and r['scene'] == '2' for r in rows)
                    if args.walk and args.gpu and scene == 'town':
                        motion = [line.split(',') for line in (output / (key + '-motion.csv')).read_text(encoding='utf-8').splitlines() if line.startswith('P,')]
                        assert max(float(r[3]) for r in motion) - min(float(r[3]) for r in motion) > 2, 'camera did not move'
                        last_field = int(fields[-1]['view'])
                        assert abs(float(motion[last_field][3]) - float(motion[last_field - 1][3])) > 0.001, 'camera stopped before wipe'
                        assert any(abs(float(r[3]) - round(float(r[3]))) > 0.001 for r in motion[last_field - 4:last_field + 1]), 'no fractional camera refresh before wipe'
                        if hz == 60:
                            assert abs(float(motion[last_field][3]) - round(float(motion[last_field][3]))) > 0.001, 'last retained 60 Hz camera was not subpixel'
                    digest = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
                    entry = dict(digest=digest, presents=len(rows), wipe_presents=len(wipe), field_presents=len(fields))
                    if expected:
                        assert entry == expected[key], f'{key}: pictures/coverage changed'
                        baseline_csv = args.baseline / (key + '.csv')
                        if baseline_csv.exists():
                            with baseline_csv.open(encoding='utf-8', newline='') as f:
                                baseline_rows = list(csv.DictReader(f))
                            for before, after in zip(baseline_rows, rows, strict=True):
                                assert all(before[k] == after[k] for k in identity[0]), (key, after['view'])
                    summary[key] = entry
                    metadata = dict(cmd=cmd, cpu_seconds=cpu_after.ru_utime + cpu_after.ru_stime - cpu_before.ru_utime - cpu_before.ru_stime,
                                    seconds=time.monotonic() - start)
                    (output / (key + '.json')).write_text(json.dumps(metadata, indent=2), encoding='utf-8', newline='')
                    print(key, entry, flush=True)
    (output / 'pictures.json').write_text(json.dumps(summary, indent=2), encoding='utf-8', newline='')


if __name__ == '__main__':
    raise SystemExit(main())
