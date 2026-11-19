"""Load and check the symbol database in symbols/ (functions.toml, ram.toml)."""
import re
import tomllib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SYMBOLS = ROOT / "symbols"
TOP_PAGE = 0x340000
NAME = re.compile(r"^[a-z_][a-z0-9_]*$")


@dataclass(frozen=True)
class Function:
    rom: int
    name: str
    brief: str

    @property
    def cpu(self):
        """(segment, offset) the code runs at, for code in the top linear page."""
        if self.rom < TOP_PAGE:
            return None
        return 0x4000 + 0x1000 * ((self.rom - TOP_PAGE) >> 16), self.rom & 0xFFFF


@dataclass(frozen=True)
class Var:
    addr: int
    size: int
    name: str
    brief: str


def load():
    funcs = [Function(**f) for f in tomllib.loads((SYMBOLS / "functions.toml").read_text(encoding="utf-8"))["function"]]
    ram = [Var(**v) for v in tomllib.loads((SYMBOLS / "ram.toml").read_text(encoding="utf-8"))["var"]]
    return funcs, ram


def check(funcs, ram, code_starts=None):
    """Return a list of problems (empty if the database is consistent)."""
    errs = []
    names = {}
    for s in [*funcs, *ram]:
        if not NAME.match(s.name):
            errs.append(f"{s.name}: not snake_case")
        if s.name.startswith("sub_"):
            errs.append(f"{s.name}: sub_ names are generated, don't store them")
        if s.name in names:
            errs.append(f"{s.name}: defined twice")
        names[s.name] = s
    seen = {}
    for f in funcs:
        if f.rom in seen:
            errs.append(f"{f.name}: same ROM offset as {seen[f.rom]}")
        seen[f.rom] = f.name
        if code_starts is not None and f.rom not in code_starts:
            errs.append(f"{f.name}: 0x{f.rom:06X} is not a known instruction start (build/cdl/code.txt)")
    for v in ram:
        if not 0 <= v.addr <= 0xFFFF or v.size < 1:
            errs.append(f"{v.name}: bad addr/size")
    return errs
