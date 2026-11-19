"""Authored drum patterns and MIDI encoding used by the live music table generator."""
from pathlib import Path
import re
import struct

FRAME_S = 159 * 256 / 3072000.0  # one WSC frame, in seconds
RATE = 44100

# Tick 1 is the first sounding bar. 143 ends in a partial bar; 142 = 6 + 32 bars.
SONGS = {145: dict(bar_ticks=64, first_tick=1, bars=24),
         # 143: after the 8-bar intro, three phrases of 15 beats and one of 16 (8 beats of gallop, then 8ths
         # grouped in threes), then 8 bars. Bars are numbered so each phrase starts on a bar line: bars 12, 16
         # and 20 have three beats. The last bar ends exactly at the loop.
         143: dict(bar_ticks=64, first_tick=1, bars=32, short={12: 48, 16: 48, 20: 48}),
         142: dict(bar_ticks=96, first_tick=1, bars=38)}
# U/M/L/F high/mid/low/floor tom, B ride bell, Z china, D crash 2, P splash, Q pedal hi-hat.
NOTES = dict(C=49, H=42, O=46, S=38, K=36, R=51, T=47, U=50, M=47, L=45, F=41, B=53, Z=52, D=57, P=55, Q=44)
LEVELS = dict(K=dict(X=110, x=100, g=60), S=dict(X=108, x=98, g=55),
              H=dict(X=82, x=64, g=45), C=dict(X=100, x=100, g=60),
              O=dict(X=84, x=84, g=55), R=dict(X=92, x=76, g=45),
              T=dict(X=100, x=85, g=55), U=dict(X=108, x=96, g=60), M=dict(X=108, x=96, g=60),
              L=dict(X=110, x=98, g=60), F=dict(X=112, x=100, g=62), B=dict(X=96, x=80, g=50),
              Z=dict(X=108, x=96, g=70), D=dict(X=118, x=104, g=76), P=dict(X=100, x=88, g=60),
              Q=dict(X=82, x=64, g=45))
ALIASES = {'kick':'K', 'snare':'S', 'hat':'H', 'crash':'C', 'open hat':'O', 'ride':'R', 'timpani':'T'}
KITS = dict(standard=0, room=8, power=16, orchestral=48)
RULER = '1...2...3...4...'


def parse(path):
    source = Path(path).read_text(encoding='utf-8')
    bars, current = {}, []
    notes, levels = dict(NOTES), {k:dict(v) for k,v in LEVELS.items()}
    def fail(n, reason):
        raise ValueError(f'{path}:{n}: {reason}')
    for n, line in enumerate(source.splitlines(), 1):
        if line.lstrip().startswith('#'):
            comment = line.lstrip()[1:].strip()
            if comment.startswith('Notes:'):
                for letter, value in re.findall(r'([A-Z])\s*=\s*(\d+)', comment):
                    if letter not in notes or letter == 'T' or not 0 <= int(value) <= 127:
                        fail(n, f'invalid drum note {letter}={value}')
                    notes[letter] = int(value)
            # The original E header: "C crash (49) H closed hi-hat (42) ...".
            for letter, value in re.findall(r'([CHOSKR])\s+[^()]+?\((\d+)\)', comment):
                notes[letter] = int(value)
            if comment.startswith('Levels:'):
                for clause in comment[7:].split(';'):
                    clause = clause.strip().rstrip('.')
                    m = re.match(r'([A-Z]|open hat|timpani|kick|snare|crash|ride|hat)\b\s*(.*)', clause)
                    if not m:
                        fail(n, 'invalid level clause')
                    letter = ALIASES.get(m[1], m[1])
                    if letter not in levels:
                        fail(n, f'unknown drum letter {letter}')
                    pairs = re.findall(r'([Xxg])\s*(?:=|\s)\s*(\d+)', m[2])
                    if not pairs and re.fullmatch(r'\d+', m[2]):
                        pairs = [('X',m[2]),('x',m[2])]
                    if not pairs:
                        fail(n, 'expected X/x/g velocity')
                    for mark, value in pairs:
                        if not 1 <= int(value) <= 127:
                            fail(n, 'velocity must be 1..127')
                        levels[letter][mark] = int(value)
            continue
        text = line.split('#',1)[0].strip()
        if not text or text == RULER:
            continue
        if text.startswith('bar '):
            m = re.fullmatch(r'bar\s+([\d,\s]+?)(?:\s+' + re.escape(RULER) + r')?', text)
            if not m:
                fail(n, 'expected bar numbers and optional beat ruler')
            current = [int(b) for b in re.split(r'[,\s]+',m[1].strip()) if b]
            for b in current:
                if b < 1 or b in bars:
                    fail(n, f'bar {b} listed twice or nonpositive')
                bars[b] = {}
            continue
        cells = text.split()
        letter = cells[0]
        if letter not in notes:
            fail(n, f'unknown drum letter {letter}')
        if not current:
            fail(n, 'drum line before a bar declaration')
        if len(cells) < 2 or len(cells[1]) not in (12, 16):
            fail(n, 'drum line must contain exactly 16 characters (or 12: three to a beat, triplets)')
        pattern = cells[1]
        if re.search(r'[^.xXg1-8]',pattern):
            fail(n, 'only ., x, X, g and the levels 1-8 are allowed')
        pitches = []
        if letter == 'T':
            values = [x for x in cells[2:] if x != '|']
            count = sum(c != '.' for c in pattern)
            if len(values) == 16:
                if any((v == '.') != (c == '.') for c,v in zip(pattern, values)):
                    fail(n, 'T pitch rests must match its hit grid')
                values = [v for v in values if v != '.']
            if len(values) != count:
                fail(n, 'T needs one note name per hit (or 16 pitch/rest cells)')
            for v in values:
                m = re.fullmatch(r'([A-Ga-g])([#b]?)(-?\d+)',v)
                if not m:
                    fail(n, f'invalid pitch {v}; use C2, F#2, etc.')
                pitch = (int(m[3])+1)*12 + {'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}[m[1].upper()] + {'':0,'#':1,'b':-1}[m[2]]
                if not 0 <= pitch <= 127:
                    fail(n, f'pitch outside MIDI range: {v}')
                pitches.append(pitch)
        elif len(cells) != 2:
            fail(n, 'unexpected text after drum grid')
        for b in current:
            if letter in bars[b]:
                fail(n, f'duplicate {letter} line in bar {b}')
            bars[b][letter] = (pattern,pitches)
    return bars, notes, levels, source


def midi(hits, path, kit, end):
    events = [(0, b'\xff\x51\x03' + (1000000).to_bytes(3,'big')),
              (0, bytes([0xc9,kit])), (0, bytes([0xc0,47])),
              (0, bytes([0xb9,7,100])), (0, bytes([0xb9,91,25])),
              (0, bytes([0xb9,10,64]))]  # Center the kit; retain its normal SF2 width.
    for hit in hits:
        ms = round(hit['seconds']*1000)
        ch = 0 if hit['letter'] == 'T' else 9
        duration = 400 if ch == 0 else 120
        events += [(ms,bytes([0x90|ch,hit['note'],hit['velocity']])),
                   (ms+duration,bytes([0x80|ch,hit['note'],0]))]
    events.append((round(end*1000),b'\xff\x2f\x00'))
    events.sort(key=lambda e:(e[0],e[1][0]&0xf0 == 0x90))
    data,last = bytearray(),0
    for ms,payload in events:
        delta,last = ms-last,ms
        var = [delta&127]
        while delta > 127:
            delta >>= 7
            var.insert(0,(delta&127)|128)
        data.extend(bytes(var)+payload)
    Path(path).write_bytes(b'MThd'+struct.pack('>IHHH',6,0,1,1000)+b'MTrk'+struct.pack('>I',len(data))+data)


def bar_tick(config, bar):
    """First tick of a bar, allowing for a song's odd-length bars."""
    short = config.get('short', {})
    return config['first_tick'] + sum(short.get(b, config['bar_ticks']) for b in range(1, bar))
