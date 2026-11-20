"""Cartridge footer and standard mapper address translation. No ROM identification database."""
from dataclasses import dataclass
from pathlib import Path

BANK = 0x10000
ROM_SIZES = {0: 128 << 10, 1: 256 << 10, 2: 512 << 10, 3: 1 << 20,
             4: 2 << 20, 5: 3 << 20, 6: 4 << 20, 7: 6 << 20, 8: 8 << 20, 9: 16 << 20}
SAVE_TYPES = {0: ("none", 0), 1: ("SRAM", 8 << 10), 2: ("SRAM", 32 << 10),
              3: ("SRAM", 128 << 10), 4: ("SRAM", 256 << 10), 5: ("SRAM", 512 << 10),
              0x10: ("EEPROM", 128), 0x20: ("EEPROM", 2048), 0x50: ("EEPROM", 1024)}


@dataclass
class Cartridge:
    data: bytes

    def __post_init__(self):
        if len(self.data) < BANK or len(self.data) % BANK:
            raise ValueError("ROM must contain complete 64 KiB banks and a footer")
        self.size = len(self.data)
        self.capacity = 1 << (self.size - 1).bit_length()
        self.padding = self.capacity - self.size
        self.declared_size = ROM_SIZES.get(self.data[-6])
        self.mapper = self.data[-3]
        self.save_type, self.save_size = SAVE_TYPES.get(self.data[-5], ("unknown", 0))
        self.color = bool(self.data[-9] & 1)
        self.checksum = int.from_bytes(self.data[-2:], "little")
        self.computed_checksum = sum(self.data[:-2]) & 0xFFFF

    def validate(self):
        if self.declared_size != self.size:
            raise ValueError("footer ROM size does not match file (stripped/padded dump or unknown size code)")
        if self.mapper not in (0, 1):
            raise ValueError(f"unsupported mapper {self.mapper:#x}; standard mapper 0/1 required")

    def file_offset(self, bus):
        """ROM is right aligned in its next-power-of-two bus capacity, as in Mesen2."""
        off = (bus % self.capacity) - self.padding
        return off if 0 <= off < self.size else None


@dataclass(frozen=True)
class Mapping:
    cart: Cartridge
    page: int = 0xFF
    bank0: int = 0xFF
    bank1: int = 0xFF

    def rom(self, linear):
        linear &= 0xFFFFF
        window = linear >> 16
        if window < 2:
            return None
        bus = ((self.bank0 if window == 2 else self.bank1) << 16 | (linear & 0xFFFF)) if window < 4 else (
            (self.page << 20) | linear)
        return self.cart.file_offset(bus)

    def cpu(self, off):
        """Prefer the highest linear alias; otherwise use a ROM0 bank overlay."""
        for bank in range(15, 3, -1):
            lin = bank << 16 | (off & 0xFFFF)
            if self.rom(lin) == off:
                return lin
        return 0x20000 | (off & 0xFFFF)


def mapping_args(parser):
    for name in ("linear-page", "bank0", "bank1"):
        parser.add_argument("--" + name, type=lambda s: int(s, 0), default=0xFF,
                            help="fixed mapper register value (default: 0xff)")


def load(path):
    cart = Cartridge(Path(path).read_bytes())
    cart.validate()
    return cart


def pairs(path):
    """Text rows: hexadecimal ROM offset, decimal length/flags; optional extra fields."""
    result = {}
    if path is not None:
        for number, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
            fields = line.split("#", 1)[0].split()
            if fields:
                off, value = int(fields[0], 16), int(fields[1])
                if off < 0 or value < 0:
                    raise ValueError(f"{path}:{number}: negative offset/value")
                result[off] = value
    return result


def symbols(path):
    """Rows: rom|func|ram HEX_OFFSET NAME [DECIMAL_SIZE]. func is a ROM function."""
    result = []
    names, addresses = set(), set()
    if path is not None:
        import re
        for number, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
            f = line.split("#", 1)[0].split()
            if not f:
                continue
            if len(f) not in (3, 4) or f[0] not in ("rom", "func", "ram") or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", f[2]):
                raise ValueError(f"{path}:{number}: expected rom|func|ram HEX_OFFSET NAME [SIZE]")
            kind, off, name, size = f[0], int(f[1], 16), f[2], int(f[3]) if len(f) == 4 else 0
            region = "ram" if kind == "ram" else "rom"
            if off < 0 or size < 0 or name in names or (region, off) in addresses:
                raise ValueError(f"{path}:{number}: duplicate or negative symbol")
            if kind == "ram" and off + max(size, 1) > 0x20000:
                raise ValueError(f"{path}:{number}: RAM symbol outside CPU RAM windows")
            names.add(name)
            addresses.add((region, off))
            result.append((kind, off, name, size))
    return result
