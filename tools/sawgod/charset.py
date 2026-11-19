"""WSC Makai Toushi SaGa one-byte character sets: menu/system text and event-script dialogue.

Seeded from Tower Reversed's wsc-saga-cset.txt (0x00-0x9F kana, 0xA1-0xBA A-Z); the other codes were
worked out from context in the menu strings at ROM 0x34B930. Unknown codes decode
as [XX]. Script/dialogue text may use a different encoding (not covered here).
"""

HIRA = ("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
        "がぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽぁぃぅぇぉっゃゅょ")
KATA = ("アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン"
        "ガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポァィゥェォッャュョ")

TABLE = {i: c for i, c in enumerate(HIRA)}
TABLE.update({0x50 + i: c for i, c in enumerate(KATA)})
TABLE.update({0xA1 + i: chr(ord("Ａ") + i) for i in range(26)})
TABLE.update({0xBB + i: chr(ord("０") + i) for i in range(10)})  # "タイム00:00"
TABLE.update({
    0xC5: "＠",
    0xC6: "「",
    0xC7: "」",
    0xC8: "、",
    0xC9: "。",
    0xCA: "‥",
    0xCB: "！",
    0xCC: "？",
    0xCF: "×",
    0xD0: "○",
    0xD2: "／",
    0xD3: "ー",
    0xD4: "：",
    0xD9: "　",
})
END = 0xFF


def decode(data: bytes) -> str:
    return "".join(TABLE.get(b, "|" if b == END else f"[{b:02X}]") for b in data)


# Event-script dialogue (banks 0x35/0x36/0x38 script data): the same glyphs shifted up by 0x20, so bytes
# below 0x20 are free for script opcodes. Control/punctuation meanings are from context
# and marked "?" in DIALOGUE_CONTROLS until the script interpreter is read.
DIALOGUE = {k + 0x20: v for k, v in TABLE.items() if k < 0xA0}
DIALOGUE.update({0xF9: "　", 0xEC: "？", 0xF3: "！", 0xDC: "一"})
DIALOGUE_CONTROLS = {
    0x0D: "newline?",
    0xE6: "open message box (speaker follows?)",
    0xE9: "end of message box / wait for button?",
    0xEB: "wait / continue?",
}


def decode_dialogue(data: bytes) -> str:
    return "".join(DIALOGUE.get(b, f"[{b:02X}]") for b in data)
