"""Asset formats: each converts ROM bytes to an editable file and back, losslessly.

Every format has `extract(data, entry) -> (suffix, payload)` and `build(payload, entry) -> bytes`, where payload
is bytes (written as a file) or a JSON-serialisable object. `tools/assets.py check` proves
build(extract(x)) == x for every manifest entry.
"""
import io
import json

from PIL import Image

from .charset import TABLE

INV = {v: k for k, v in TABLE.items()}
GREY16 = [c for i in range(16) for c in (255 - i * 17,) * 3]  # index 0 = white, 15 = black


# --- tiles: 4bpp packed (two pixels per byte, high nibble first), 32 bytes per 8x8 tile -> indexed PNG

def tiles_extract(data, entry):
    cols = entry.get("cols", 16)
    n = len(data) // 32
    rows = (n + cols - 1) // cols
    img = Image.new("P", (cols * 8, rows * 8), 0)
    img.putpalette(GREY16 + [0] * (768 - len(GREY16)))
    px = img.load()
    for t in range(n):
        tx, ty = (t % cols) * 8, (t // cols) * 8
        for y in range(8):
            row = data[t * 32 + y * 4:t * 32 + y * 4 + 4]
            for x in range(8):
                px[tx + x, ty + y] = (row[x // 2] >> (4 * (1 - x % 2))) & 15
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return ".png", buf.getvalue()


def tiles_build(payload, entry):
    img = Image.open(io.BytesIO(payload))
    cols = entry.get("cols", 16)
    n = entry["size"] // 32
    px = img.load()
    out = bytearray()
    for t in range(n):
        tx, ty = (t % cols) * 8, (t // cols) * 8
        for y in range(8):
            for x in range(0, 8, 2):
                out.append((px[tx + x, ty + y] & 15) << 4 | (px[tx + x + 1, ty + y] & 15))
    return bytes(out)


# --- strings: 0xFF-terminated, system charset; bytes without a glyph are written as [XX]

def _dec(chunk):
    return "".join(TABLE.get(b, f"[{b:02X}]") for b in chunk)


def _enc(text):
    out, i = bytearray(), 0
    while i < len(text):
        if text[i] == "[" and i + 3 < len(text) and text[i + 3] == "]":
            out.append(int(text[i + 1:i + 3], 16))
            i += 4
        else:
            out.append(INV[text[i]])
            i += 1
    return bytes(out)


def strings_extract(data, entry):
    parts = data.split(b"\xff")
    return ".json", {"strings": [_dec(p) for p in parts[:-1]], "tail": _dec(parts[-1])}


def strings_build(payload, entry):
    return b"".join(_enc(s) + b"\xff" for s in payload["strings"]) + _enc(payload["tail"])


# --- records: fixed-size structures with named little-endian fields

def records_extract(data, entry):
    size, fields = entry["record_size"], entry["fields"]
    recs = []
    for k in range(len(data) // size):
        r = data[k * size:(k + 1) * size]
        rec, pos = {}, 0
        for f in fields:
            name, width = f["name"], f["size"]
            rec[name] = int.from_bytes(r[pos:pos + width], "little")
            pos += width
        if pos < size:
            rec["_rest"] = r[pos:].hex()
        recs.append(rec)
    return ".json", {"records": recs}


def records_build(payload, entry):
    size, fields = entry["record_size"], entry["fields"]
    out = bytearray()
    for rec in payload["records"]:
        r = bytearray()
        for f in fields:
            r += int(rec[f["name"]]).to_bytes(f["size"], "little")
        r += bytes.fromhex(rec.get("_rest", ""))
        assert len(r) == size
        out += r
    return bytes(out)


# --- palettes: 16 colours of 12-bit RGB (little-endian words 0x0RGB) per palette -> "#RGB" strings
# (a word with the unused top nibble set is kept as "0xNNNN" so the round trip stays exact)

def palettes_extract(data, entry):
    pals = []
    for p in range(len(data) // 32):
        cols = []
        for i in range(16):
            w = data[p * 32 + i * 2] | data[p * 32 + i * 2 + 1] << 8
            cols.append(f"#{w:03X}" if w < 0x1000 else f"0x{w:04X}")
        pals.append(cols)
    return ".json", {"palettes": pals}


def palettes_build(payload, entry):
    out = bytearray()
    for cols in payload["palettes"]:
        for c in cols:
            w = int(c[1:], 16) if c.startswith("#") else int(c, 16)
            out += bytes((w & 0xFF, w >> 8))
    return bytes(out)


# --- raw: anything not decoded yet

def raw_extract(data, entry):
    return ".bin", bytes(data)


def raw_build(payload, entry):
    return bytes(payload)


FORMATS = {
    "tiles_4bpp": (tiles_extract, tiles_build),
    "strings": (strings_extract, strings_build),
    "records": (records_extract, records_build),
    "palettes": (palettes_extract, palettes_build),
    "raw": (raw_extract, raw_build),
}


def dumps(payload):
    return json.dumps(payload, ensure_ascii=False, indent=1).encode("utf-8")
