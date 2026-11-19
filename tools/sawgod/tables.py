"""Game data tables as human-readable CSV: one row per record, named columns, decimal numbers, text in
the game's own characters.

A manifest entry with `format: table` has `rows` and `parts`: each part is a ROM region of `rows` records of
`stride` bytes with its own `fields` (name, size, and `type: text` for fixed-size 0xFF-terminated strings). The
parts are joined by row, so one CSV can hold, say, an item's name (bank 0x39), its record (bank 0x39) and its
price (bank 0x34). Bytes a part's fields don't cover are kept in a `<part>_rest` hex column. The first column
is `id` (the row index; informational, rows are matched by position).

A field with `type: list` is `size` one-byte values written as one space-separated cell, its trailing `empty`
values (default 255) left out and filled back in on rebuild; with `names` ({rom, stride} of a text table) a
read-only `<field>_names` column shows each value's name (ignored on rebuild).

A field with `bits` (a list of {name, lo, width}, as in pointer_records below) becomes one CSV column per bit
range instead of one column for the whole field; each bit range round-trips as its own 0..2**width-1 value.

Text cells: the name without its padding (full-width spaces before the 0xFF terminator); a cell that isn't in
that canonical shape keeps the exact characters, [XX] bytes and all, so every ROM table round-trips exactly.
"""
import csv
import io

from sawgod.formats import INV, _dec, _enc

PAD = "　"  # the full-width space the game pads names with


def regions(entry):
    """(rom offset, size) of each part."""
    return [(p["rom"], entry["rows"] * p["stride"]) for p in entry["parts"]]


def _columns(entry):
    cols = ["id"]
    for i, p in enumerate(entry["parts"]):
        for f in p["fields"]:
            cols += _field_columns(f)
            if f.get("type") == "list" and "names" in f:
                cols.append(f["name"] + "_names")
        if sum(f["size"] for f in p["fields"]) < p["stride"]:
            cols.append(f"{p.get('name', f'part{i}')}_rest")
    return cols


def _text_cell(b):
    full = _dec(b)
    if b and b[-1] == 0xFF and 0xFF not in b[:-1]:
        body = full[: -len("[FF]")]
        stripped = body.rstrip(PAD)
        if _text_bytes(stripped, len(b)) == b:
            return stripped
    return full


def _text_bytes(cell, size):
    try:
        raw = _enc(cell)
    except KeyError as e:
        raise ValueError(f"{e.args[0]!r} in {cell!r} isn't in the game's character set (Latin letters and digits "
                         f"are full-width, e.g. Ａ １; [XX] writes a byte)") from None
    if len(raw) == size and raw.endswith(b"\xff"):
        return raw  # written out exactly
    if len(raw) > size - 1:
        raise ValueError(f"text {cell!r} is longer than {size - 1} characters")
    return raw + bytes([INV[PAD]]) * (size - 1 - len(raw)) + b"\xff"


def extract(rom, entry):
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(_columns(entry))
    for k in range(entry["rows"]):
        row = [k]
        for i, p in enumerate(entry["parts"]):
            rec = rom[p["rom"] + k * p["stride"]: p["rom"] + (k + 1) * p["stride"]]
            pos = 0
            for f in p["fields"]:
                b = rec[pos: pos + f["size"]]
                if f.get("type") == "text":
                    row.append(_text_cell(b))
                elif f.get("type") == "list":
                    vals = list(b)
                    while vals and vals[-1] == f.get("empty", 255):
                        vals.pop()
                    row.append(" ".join(str(v) for v in vals))
                    if "names" in f:
                        row.append(" / ".join(_name_at(rom, f["names"], v) for v in vals))
                elif "bits" in f:
                    v = int.from_bytes(b, "little")
                    row += [(v >> bb["lo"]) & ((1 << bb["width"]) - 1) for bb in f["bits"]]
                else:
                    row.append(int.from_bytes(b, "little"))
                pos += f["size"]
            if pos < p["stride"]:
                row.append(rec[pos:].hex())
        w.writerow(row)
    return out.getvalue().encode("utf-8")


def build(csv_bytes, entry):
    """[(rom offset, bytes)] for each part, from an edited CSV."""
    rows = list(csv.DictReader(io.StringIO(csv_bytes.decode("utf-8-sig"))))
    if len(rows) != entry["rows"]:
        raise ValueError(f"{entry['name']}: {len(rows)} rows, the table has {entry['rows']} (rows can't be added yet)")
    out = []
    for i, p in enumerate(entry["parts"]):
        data = bytearray()
        rest_col = f"{p.get('name', f'part{i}')}_rest"
        for r in rows:
            rec = bytearray()
            for f in p["fields"]:
                if "bits" in f:
                    v = 0
                    for bb in f["bits"]:
                        x = int(r[bb["name"]], 0)
                        if not 0 <= x < 1 << bb["width"]:
                            raise ValueError(f"{entry['name']} row {r['id']}: {bb['name']} must be 0-{(1 << bb['width']) - 1}")
                        v |= x << bb["lo"]
                    rec += v.to_bytes(f["size"], "little")
                    continue
                cell = r[f["name"]]
                if f.get("type") == "text":
                    rec += _text_bytes(cell, f["size"])
                elif f.get("type") == "list":
                    vals = [int(x, 0) for x in cell.split()]
                    if len(vals) > f["size"] or any(not 0 <= v < 256 for v in vals):
                        raise ValueError(f"{entry['name']} row {r['id']}: {f['name']} takes up to {f['size']} values 0-255")
                    rec += bytes(vals) + bytes([f.get("empty", 255)]) * (f["size"] - len(vals))
                else:
                    v = int(cell, 0)
                    if not 0 <= v < 1 << (8 * f["size"]):
                        raise ValueError(f"{entry['name']} row {r['id']}: {f['name']} = {v} doesn't fit {f['size']} byte(s)")
                    rec += v.to_bytes(f["size"], "little")
            if len(rec) < p["stride"]:
                rec += bytes.fromhex(r[rest_col])
            if len(rec) != p["stride"]:
                raise ValueError(f"{entry['name']} row {r['id']}: record is {len(rec)} bytes, not {p['stride']}")
            data += rec
        out.append((p["rom"], bytes(data)))
    return out


# --- pointer_records: variable-length records reached through a table of 16-bit pointers (same bank), e.g. the
# monster templates. Entry: rows, pointers (ROM offset of the pointer table), data (ROM offset where the records
# start, right after it), size (bytes of the records region), fields (the fixed header; one may be the
# `count_field` of a trailing `list` of one-byte values, written as one space-separated cell). Rebuilding packs
# the records back to back from `data` and rewrites the pointers, so lists may grow or shrink while the total
# fits; the rest of the region is padded with its original bytes. With `list_names`, a read-only
# `<list>_names` column shows each listed id's name from that table, and with `row_names` a `name` column
# shows the row's own name (both ignored on rebuild).

def pointer_regions(entry):
    return [(entry["pointers"], entry["rows"] * 2), (entry["data"], entry["size"])]


def _field_columns(f):
    """A field's CSV columns: its name, or with `bits` one column per bit range ({name, lo, width})."""
    return [b["name"] for b in f["bits"]] if "bits" in f else [f["name"]]


def pointer_columns(entry):
    names = [entry["list"] + "_names"] if "list_names" in entry else []
    row_name = ["name"] if "row_names" in entry else []
    return (["id"] + row_name + [c for f in entry["fields"] for c in _field_columns(f)] + [entry["list"]]
            + names)


def _name_at(rom, table, index):
    return _text_cell(rom[table["rom"] + index * table["stride"]:table["rom"] + (index + 1) * table["stride"]])


def pointer_extract(rom, entry):
    bank = entry["pointers"] & ~0xFFFF
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(pointer_columns(entry))
    for k in range(entry["rows"]):
        p = bank + (rom[entry["pointers"] + 2 * k] | rom[entry["pointers"] + 2 * k + 1] << 8)
        row, pos, count = [k], p, 0
        if "row_names" in entry:  # read-only: the row's name from another table
            row.append(_name_at(rom, entry["row_names"], k))
        for f in entry["fields"]:
            v = int.from_bytes(rom[pos:pos + f["size"]], "little")
            if f["name"] == entry["count_field"]:
                count = v
            if "bits" in f:
                row += [(v >> b["lo"]) & ((1 << b["width"]) - 1) for b in f["bits"]]
            else:
                row.append(v)
            pos += f["size"]
        values = rom[pos:pos + count]
        row.append(" ".join(str(b) for b in values))
        if "list_names" in entry:  # read-only: the names of the listed ids (e.g. item names for monster gear)
            row.append(" / ".join(_name_at(rom, entry["list_names"], b) for b in values))
        w.writerow(row)
    return out.getvalue().encode("utf-8")


def pointer_build(csv_bytes, entry, rom):
    rows = list(csv.DictReader(io.StringIO(csv_bytes.decode("utf-8-sig"))))
    if len(rows) != entry["rows"]:
        raise ValueError(f"{entry['name']}: {len(rows)} rows, the table has {entry['rows']}")
    data, ptrs = bytearray(), bytearray()
    for r in rows:
        items = [int(x, 0) for x in r[entry["list"]].split()]
        ptrs += ((entry["data"] + len(data)) & 0xFFFF).to_bytes(2, "little")
        for f in entry["fields"]:
            if "bits" in f:
                v = 0
                for b in f["bits"]:
                    x = int(r[b["name"]], 0)
                    if not 0 <= x < 1 << b["width"]:
                        raise ValueError(f"{entry['name']} row {r['id']}: {b['name']} must be 0-{(1 << b['width']) - 1}")
                    v |= x << b["lo"]
                data += v.to_bytes(f["size"], "little")
                continue
            v = len(items) if f["name"] == entry["count_field"] else int(r[f["name"]], 0)
            if f["name"] == entry["count_field"] and int(r[f["name"]], 0) != v:
                raise ValueError(f"{entry['name']} row {r['id']}: {f['name']} is {r[f['name']]} but "
                                 f"{entry['list']} has {v} entries")
            data += v.to_bytes(f["size"], "little")
        if any(not 0 <= b < 256 for b in items):
            raise ValueError(f"{entry['name']} row {r['id']}: {entry['list']} values must be 0-255")
        data += bytes(items)
    if len(data) > entry["size"]:
        raise ValueError(f"{entry['name']}: the records need {len(data)} bytes, there are {entry['size']}")
    data += rom[entry["data"] + len(data):entry["data"] + entry["size"]]
    return [(entry["pointers"], bytes(ptrs)), (entry["data"], bytes(data))]


# --- formations: the battle formations (setup_battle's actor setup, bank 0x36): variable-length records behind a
# word pointer table. Record: a flags byte (bit 4 = can't run, low nibble = number of groups), then per group a
# monster type and a count byte (low nibble = min, high nibble = max; random_range picks between them). One
# row per formation: cant_run, then per group type / name (read-only) / min / max; empty groups are left
# blank. Rebuilding packs the records back to back and rewrites the pointers.

def formation_columns(entry):
    cols = ["id", "cant_run"]
    for g in range(1, entry["max_groups"] + 1):
        cols += [f"group{g}_type", f"group{g}_name", f"group{g}_min", f"group{g}_max"]
    return cols


def formation_extract(rom, entry):
    bank = entry["pointers"] & ~0xFFFF
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(formation_columns(entry))
    for k in range(entry["rows"]):
        p = bank + (rom[entry["pointers"] + 2 * k] | rom[entry["pointers"] + 2 * k + 1] << 8)
        flags = rom[p]
        if flags & ~0x1F or (flags & 0xF) > entry["max_groups"]:
            raise ValueError(f"{entry['name']} {k}: flags {flags:#x} not understood")
        row = [k, 1 if flags & 0x10 else 0]
        for g in range(entry["max_groups"]):
            if g < flags & 0xF:
                t, c = rom[p + 1 + 2 * g], rom[p + 2 + 2 * g]
                row += [t, _name_at(rom, entry["names"], t), c & 0xF, c >> 4]
            else:
                row += ["", "", "", ""]
        w.writerow(row)
    return out.getvalue().encode("utf-8")


def formation_build(csv_bytes, entry, rom):
    rows = list(csv.DictReader(io.StringIO(csv_bytes.decode("utf-8-sig"))))
    if len(rows) != entry["rows"]:
        raise ValueError(f"{entry['name']}: {len(rows)} rows, the table has {entry['rows']}")
    data, ptrs = bytearray(), bytearray()
    for r in rows:
        groups = []
        for g in range(1, entry["max_groups"] + 1):
            t = r[f"group{g}_type"].strip()
            if not t:
                continue
            lo, hi = int(r[f"group{g}_min"], 0), int(r[f"group{g}_max"], 0)
            if not (0 <= int(t, 0) < 256 and 0 <= lo <= 15 and 0 <= hi <= 15):
                raise ValueError(f"{entry['name']} row {r['id']}: group {g}: type 0-255, min and max 0-15")
            groups.append((int(t, 0), lo | hi << 4))
        ptrs += ((entry["data"] + len(data)) & 0xFFFF).to_bytes(2, "little")
        data.append((0x10 if int(r["cant_run"], 0) else 0) | len(groups))
        for t, c in groups:
            data += bytes([t, c])
    if len(data) > entry["size"]:
        raise ValueError(f"{entry['name']}: the records need {len(data)} bytes, there are {entry['size']}")
    data += rom[entry["data"] + len(data):entry["data"] + entry["size"]]
    return [(entry["pointers"], bytes(ptrs)), (entry["data"], bytes(data))]
