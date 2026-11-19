"""Shared audience-facing GM seats for offline MIDI and generated live castings.

Programs are zero-based; seats are percent of the center-to-speaker distance.
Keep the SoundFont's own stereo spread unchanged.
"""
import math

# Inclusive GM program ranges, label, percent (negative = left).
SEAT_FAMILIES = (
    (0, 15, 'Piano, celesta, harpsichord, mallets and bells', -25),
    (16, 23, 'Organs, accordion and harmonica', 0),
    (24, 31, 'Acoustic and electric guitars', -12),
    (32, 39, 'Acoustic and electric basses', 0),
    (40, 40, 'Violin', -30),
    (41, 41, 'Viola', 10),
    (42, 42, 'Cello', 25),
    (43, 43, 'Contrabass', 30),
    (44, 44, 'Tremolo strings', -30),
    (45, 45, 'Pizzicato strings (cello section by default)', 25),
    (46, 46, 'Orchestral harp', -25),
    (47, 47, 'Timpani', 10),
    (48, 48, 'String Ensemble 1', -30),
    (49, 49, 'String Ensemble 2', 10),
    (50, 51, 'Synth strings', -30),
    (52, 55, 'Choir, voice and orchestra hit', 0),
    (56, 56, 'Trumpet', 15),
    (57, 58, 'Trombone and tuba', 25),
    (59, 59, 'Muted trumpet', 15),
    (60, 60, 'French horn', -20),
    (61, 63, 'Brass section and synth brass', 25),
    (64, 67, 'Saxophones', 10),
    (68, 69, 'Oboe and English horn', -10),
    (70, 71, 'Bassoon and clarinet', 10),
    (72, 79, 'Piccolo, flutes, pipes and ocarina', -10),
    (80, 103, 'Synth leads, pads and effects', 0),
    (104, 107, 'Plucked world instruments', -12),
    (108, 108, 'Kalimba', -25),
    (109, 109, 'Bagpipe', -10),
    (110, 110, 'Fiddle', -30),
    (111, 111, 'Shanai', -10),
    (112, 115, 'Pitched bells and mallets', -25),
    (116, 127, 'Percussion and sound effects', 0),
)


def voice_seat(voice):
    if 'seat' in voice:
        seat = voice['seat']
        if (isinstance(seat, bool) or not isinstance(seat, (int, float))
                or not math.isfinite(seat) or not -100 <= seat <= 100):
            raise ValueError('seat must be a finite percent between -100 and 100')
        return seat
    if voice.get('noise'):
        return 0
    program = voice['program']
    if not isinstance(program, int) or not 0 <= program <= 127:
        raise ValueError('Invalid GM program for seat')
    return next(seat for lo, hi, _, seat in SEAT_FAMILIES if lo <= program <= hi)


def seated_pan(wsc_pan, seat):
    """Add the seat to the original rounded WSC CC10, with full movement.

    Python round and C++ nearbyint use ties-to-even. Clamp only to MIDI 0..127.
    """
    return max(0, min(127, round(wsc_pan + 63 * seat / 100)))
