"""Compare the shipped original-cartridge setup with unmodified native headless playback."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from movie import read_movie


def main():
    rom = os.environ.get('SAWGOD_WSC_ROM')
    if not rom:
        print('Set SAWGOD_WSC_ROM to run original-cartridge recording comparisons.')
        return 77
    if not Path(rom).is_file():
        raise RuntimeError('SAWGOD_WSC_ROM does not name a file')
    decomp, reference = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix='sawgod-decomp-') as temp:
        work = Path(temp)
        # Title/intro, character creation, and field/save-menu coverage. No host directives.
        for name, frames in [('newgame', 15000), ('story', 16700), ('continue', 3000)]:
            movie = ROOT / 'tests/movies' / f'{name}.txt'
            inputs = work / f'{name}.inputs'
            inputs.write_text(''.join(spec.replace(':', ' ', 1) + '\n' for spec in read_movie(movie)),
                              encoding='utf-8', newline='')
            outputs = []
            for label, binary, flags in [('reference', reference, ['--native', 'all']),
                                          ('decomp', decomp, [])]:
                out = work / f'{name}-{label}'
                out.mkdir()
                cmd = [binary, rom, '--frames', str(frames), '--inputs', str(inputs),
                       '--goldendir', str(out), '--goldenevery', str(frames),
                       '--savesram', str(out / 'final.sav'), *flags]
                if name == 'continue':
                    cmd += ['--sram', str(work / f'newgame-{label}' / 'final.sav')]
                subprocess.run(cmd, check=True, timeout=90, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                outputs.append(out)
            for extension in ('ram', 'sram', 'png'):
                filename = f'{frames:06d}.{extension}'
                a, b = [hashlib.sha256((out / filename).read_bytes()).hexdigest() for out in outputs]
                if a != b:
                    raise AssertionError(f'{name}: {extension} differs: {a} / {b}')
                print(f'{name}: {extension} SHA-256 {a}')
            if name == 'newgame':
                # Continue must exercise a real cartridge save, rather than two empty images.
                block = (outputs[1] / 'final.sav').read_bytes()[0x3301:0x373C]
                valid_magic = block[0x435:0x437] == bytes((0x1B, 0xE4))
                valid_sum = sum(block[:0x439]) % 65536 == int.from_bytes(block[-2:], 'little')
                if not valid_magic or not valid_sum:
                    raise AssertionError('newgame did not produce a valid original slot 1')
            if (outputs[0] / 'final.sav').read_bytes() != (outputs[1] / 'final.sav').read_bytes():
                raise AssertionError(f'{name}: exported SRAM differs')
    return 0


if __name__ == '__main__':
    sys.exit(main())
