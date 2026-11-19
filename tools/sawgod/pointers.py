"""Find 16-bit values in code that are addresses (so the generated source can use labels).

A linear pass tracks what DS and ES hold at every instruction: this bank's own segment ("cs"), another
code bank's segment (0x4000-0xF000, banks 0x34-0x3F in the top linear page), "other", or unknown (None).
State is forgotten at labels and after ret/jmp. A memory operand's *effective segment* is its explicit
prefix, else SS for BP-based addressing, else DS; it is "ROM" when that segment is CS or DS/ES known to
hold a code bank's segment. Rules (each found by a failing shift test):

  R1  `mov word [X], IMM` where IMM is a function entry in this bank, or followed by `mov word [X+2], <own
      segment>` (a far pointer, e.g. an interrupt vector for a handler only reached by interrupt).
  R2  a ROM-segment memory operand's displacement >= 0x100 is an address in that segment's bank.
  R3  an immediate moved/added into a register that, within the next few instructions, becomes the
      base/index of a ROM-segment memory operand, or the target of `call/jmp reg`. Continues past calls
      (routines here preserve registers). Values < 0x100 only when they land on data (not code).
  R7  an immediate in a register right before `call f` (or a far call into this bank), where f (up to its first ret) uses that register as
      the base/index of a cs: access: a pointer passed to a table-reading routine (first call after the
      load only; not BX, which carries screen positions; below 0xC000 and the bank's used end).
  R4  DS known to be this bank's segment: `mov si, IMM` (>= 0x100) is an address here.
  R8  DS known to be another code bank's segment: `mov si, IMM` is an address in that bank (any value).
  R5  DS unknown (e.g. after a label): `mov si, IMM` when IMM lies in this bank's data (not code), below
      0xC000 (work RAM starts there) and below the bank's used end.
Word-table evidence (for R6 in disasm.py): an index doubled (`shl r, 1` / `add r, r`) near a 16-bit load.
Code-table evidence: the loaded word is then called/jumped to through a register (only such tables may point
at arbitrary instruction starts; data tables only at known entry points).
"""
from capstone import CS_AC_READ, CS_AC_WRITE, x86

WINDOW = 16
STOP = {"ret", "retf", "iret", "jmp", "ljmp", "int", "hlt"}  # calls preserve registers here (pusha/popa)
R16 = {x86.X86_REG_AX: "ax", x86.X86_REG_BX: "bx", x86.X86_REG_CX: "cx", x86.X86_REG_DX: "dx",
       x86.X86_REG_SI: "si", x86.X86_REG_DI: "di", x86.X86_REG_BP: "bp", x86.X86_REG_SP: "sp"}
ALIAS = {x86.X86_REG_AL: "ax", x86.X86_REG_AH: "ax", x86.X86_REG_BL: "bx", x86.X86_REG_BH: "bx",
         x86.X86_REG_CL: "cx", x86.X86_REG_CH: "cx", x86.X86_REG_DL: "dx", x86.X86_REG_DH: "dx"}
CODE_SEGS = {0x4000 + 0x1000 * k for k in range(12)}  # banks 0x34-0x3F in the top linear page
SEGS = {x86.X86_REG_CS, x86.X86_REG_DS, x86.X86_REG_ES, x86.X86_REG_SS}


def _reg(r):
    return R16.get(r) or ALIAS.get(r)


def _bank_of(seg):
    return 0x340000 + ((seg - 0x4000) >> 12 << 16)


def _doubles(i):
    """`shl r, 1` or `add r, r`: an index being scaled to a word stride."""
    o = i.operands
    if i.mnemonic == "shl" and len(o) == 2 and o[0].type == x86.X86_OP_REG and o[1].type == x86.X86_OP_IMM \
            and o[1].imm == 1:
        return True
    return i.mnemonic == "add" and len(o) == 2 and o[0].type == o[1].type == x86.X86_OP_REG and o[0].reg == o[1].reg


def _word_load(i):
    return any(op.type == x86.X86_OP_MEM and op.size == 2 for op in i.operands) and i.mnemonic == "mov"


def word_table_evidence(insns, k):
    """Around instruction k (which references a table): an index doubled and a 16-bit load nearby."""
    near = insns[max(0, k - 4):k + 6]
    return any(_doubles(j) for j in near) and any(_word_load(j) for j in insns[k:k + 10])


def segment_states(insns, segment, labels):
    """(ds, es) before each instruction: "cs" | code-bank segment int | "other" | None."""
    states, ds, es = [], None, None
    for k, i in enumerate(insns):
        if i.address in labels:
            ds = es = None
        states.append((ds, es))
        ops = i.operands
        dst = ops[0].reg if ops and ops[0].type == x86.X86_OP_REG else None
        val = False  # False = this instruction doesn't load DS/ES
        if i.mnemonic == "pop" and dst in (x86.X86_REG_DS, x86.X86_REG_ES):
            prev = insns[k - 1] if k else None
            pushed_cs = bool(prev and prev.mnemonic == "push" and prev.operands
                             and prev.operands[0].type == x86.X86_OP_REG and prev.operands[0].reg == x86.X86_REG_CS)
            val = "cs" if pushed_cs else None
        elif i.mnemonic == "mov" and dst in (x86.X86_REG_DS, x86.X86_REG_ES) and len(ops) == 2:
            val = None
            if ops[1].type == x86.X86_OP_REG:
                src = ops[1].reg
                if src == x86.X86_REG_CS:
                    val = "cs"
                elif src == x86.X86_REG_SS:
                    val = "other"
                elif src == x86.X86_REG_DS:
                    val = ds
                elif src == x86.X86_REG_ES:
                    val = es
                else:
                    for p in reversed(insns[max(0, k - 4):k]):
                        po = p.operands
                        if p.mnemonic == "mov" and len(po) == 2 and po[0].type == x86.X86_OP_REG and po[0].reg == src:
                            if po[1].type == x86.X86_OP_IMM:
                                sv = po[1].imm & 0xFFFF
                                val = "cs" if sv == segment else (sv if sv in CODE_SEGS else "other")
                            elif po[1].type == x86.X86_OP_REG and po[1].reg == x86.X86_REG_CS:
                                val = "cs"
                            elif po[1].type == x86.X86_OP_REG and po[1].reg == x86.X86_REG_SS:
                                val = "other"  # mov ax, ss; mov ds, ax: RAM
                            elif po[1].type == x86.X86_OP_REG and po[1].reg == x86.X86_REG_DS:
                                val = ds
                            elif po[1].type == x86.X86_OP_REG and po[1].reg == x86.X86_REG_ES:
                                val = es
                            break
            else:
                val = "other"  # loaded from memory
        if val is not False:
            if dst == x86.X86_REG_DS:
                ds = val
            else:
                es = val
        if i.mnemonic in ("ret", "retf", "iret", "jmp", "ljmp"):
            ds = es = None
    return states


def code_table_evidence(insns, k):
    """After instruction k (a table reference): a word loaded into a register that is then called/jumped to."""
    for j in insns[k:k + 10]:
        o = j.operands
        if j.mnemonic in ("call", "jmp") and o and o[0].type == x86.X86_OP_REG:
            return True
        if j.mnemonic in ("ret", "retf", "iret"):
            return False
    return False


def find(insns, base, entries, labels=(), used_end=0x10000, word_tables=None, runtime=None, code_tables=None):
    """insns: capstone instructions (detail on) of one bank, addresses = bank offsets.
    entries: ROM offsets that are function entries / labelled code; labels: bank offsets that are branch
    targets (segment state is forgotten there). word_tables, if given, receives targets with evidence of
    being arrays of words (for R6). runtime: {insn bank offset: set of ROM bank bases it was seen reading}
    (from the movies' read logs), used when the segment register's value is statically unknown. Returns (imm_refs, disp_refs): {insn bank offset: target ROM offset},
    targets possibly in other banks."""
    imm_refs, disp_refs = {}, {}
    labels = set(labels)
    segment = 0x4000 + 0x1000 * ((base - 0x340000) >> 16) if base >= 0x340000 else None
    index = {i.address: k for k, i in enumerate(insns)}
    states = segment_states(insns, segment, labels)
    code_bytes = set()
    for i in insns:
        code_bytes.update(range(i.address, i.address + i.size))

    def rom_bank(k, op):
        """ROM offset of the bank a memory operand at instruction k reads through, or None."""
        seg = op.mem.segment
        ds, es = states[k]
        if seg == x86.X86_REG_CS:
            state = "cs"
        elif seg == x86.X86_REG_ES:
            state = es
        elif seg == x86.X86_REG_SS or (seg == x86.X86_REG_INVALID and op.mem.base in (x86.X86_REG_BP, x86.X86_REG_SP)):
            state = "other"
        else:
            state = ds
        if state == "cs":
            return base
        if isinstance(state, int):
            return _bank_of(state)
        if state is None and runtime:  # statically unknown: did the movies see it read one ROM bank?
            mems = [o for o in insns[k].operands if o.type == x86.X86_OP_MEM]
            # only for a single memory operand that is read (not string moves, not stores)
            if len(mems) == 1 and op.access & CS_AC_READ and not op.access & CS_AC_WRITE:
                seen = runtime.get(insns[k].address)
                if seen and len(seen) == 1:
                    return next(iter(seen))
        return None

    def cs_regs(entry, _cache={}):
        """Registers function `entry` (bank offset) uses as cs: base/index before its first ret."""
        key = (base, entry)
        if key not in _cache:
            regs = set()
            k = index.get(entry)
            for j in (insns[k:k + 40] if k is not None else []):
                for op in j.operands:
                    if op.type == x86.X86_OP_MEM and op.mem.segment == x86.X86_REG_CS:
                        regs.update(r for r in (_reg(op.mem.base), _reg(op.mem.index)) if r)
                if j.mnemonic in ("ret", "retf", "iret", "jmp"):
                    break
            _cache[key] = regs
        return _cache[key]

    for k, i in enumerate(insns):
        ops = i.operands
        ds = states[k][0]
        # R1
        if (i.mnemonic == "mov" and len(ops) == 2 and ops[0].type == x86.X86_OP_MEM and ops[0].size == 2
                and ops[1].type == x86.X86_OP_IMM):
            v = ops[1].imm & 0xFFFF
            nxt = insns[k + 1] if k + 1 < len(insns) else None
            no = nxt.operands if nxt else ()
            far_pair = (nxt is not None and nxt.mnemonic == "mov" and len(no) == 2 and no[0].type == x86.X86_OP_MEM
                        and no[0].size == 2 and no[1].type == x86.X86_OP_IMM and no[1].imm & 0xFFFF == segment
                        and no[0].mem.base == ops[0].mem.base and no[0].mem.disp == ops[0].mem.disp + 2)
            if (base | v) in entries or far_pair:
                imm_refs[i.address] = base | v
        # R2: displacements through a ROM segment
        for op in ops:
            if op.type == x86.X86_OP_MEM and op.mem.disp & 0xFFFF >= 0x100:
                rb = rom_bank(k, op)
                if rb is not None:
                    disp_refs[i.address] = rb | op.mem.disp & 0xFFFF
        # R4 / R8 / R5: mov si, IMM by DS state
        if (i.mnemonic == "mov" and len(ops) == 2 and ops[0].type == x86.X86_OP_REG
                and ops[0].reg == x86.X86_REG_SI and ops[1].type == x86.X86_OP_IMM):
            v = ops[1].imm & 0xFFFF
            if ds == "cs" and v >= 0x100:
                imm_refs[i.address] = base | v
            elif isinstance(ds, int):
                imm_refs[i.address] = _bank_of(ds) | v
            elif ds is None and 0x100 <= v < 0xC000 and v < used_end and v not in code_bytes:
                imm_refs[i.address] = base | v
        # R3 / R7: an immediate into a register, then used as a ROM-segment base/index or call target
        if i.mnemonic in ("mov", "add") and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                and ops[1].type == x86.X86_OP_IMM and ops[0].size == 2 and ops[0].reg not in SEGS:
            v = ops[1].imm & 0xFFFF
            tracked = {_reg(ops[0].reg)}
            calls_seen = 0
            for jk in range(k + 1, min(len(insns), k + 1 + WINDOW)):
                j = insns[jk]
                jo = j.operands
                if j.mnemonic == "mov" and len(jo) == 2 and jo[0].type == x86.X86_OP_REG \
                        and jo[0].reg in SEGS and jo[1].type == x86.X86_OP_REG and _reg(jo[1].reg) in tracked:
                    break  # the value is a segment, not an address
                hit_bank = None
                for op in jo:
                    if op.type == x86.X86_OP_MEM and (_reg(op.mem.base) in tracked or _reg(op.mem.index) in tracked):
                        rb = rom_bank(jk, op)
                        if rb is not None:
                            hit_bank = rb
                if j.mnemonic in ("call", "jmp") and jo and jo[0].type == x86.X86_OP_REG and _reg(jo[0].reg) in tracked:
                    hit_bank = base
                callee = None  # R7: near call, or far call into this bank's own segment
                if j.mnemonic == "call" and jo and jo[0].type == x86.X86_OP_IMM:
                    callee = jo[0].imm & 0xFFFF
                elif j.mnemonic == "lcall" and len(jo) == 2 and jo[0].type == jo[1].type == x86.X86_OP_IMM \
                        and jo[0].imm & 0xFFFF == segment:
                    callee = jo[1].imm & 0xFFFF
                if callee is not None and not calls_seen and \
                        cs_regs(callee) & (tracked - {"bx"}) and v < min(0xC000, used_end):
                    hit_bank = base
                if hit_bank is not None:
                    if v >= 0x100 or hit_bank != base or v not in code_bytes:
                        imm_refs[i.address] = hit_bank | v
                    break
                if j.mnemonic in STOP:
                    break
                if j.mnemonic in ("call", "lcall"):
                    calls_seen += 1
                # propagate `mov r2, r1`; forget registers that get overwritten otherwise
                if j.mnemonic == "mov" and len(jo) == 2 and jo[0].type == x86.X86_OP_REG:
                    dst = _reg(jo[0].reg)
                    if jo[1].type == x86.X86_OP_REG and _reg(jo[1].reg) in tracked:
                        tracked.add(dst)
                    else:
                        tracked.discard(dst)
                elif j.mnemonic in ("xor", "pop", "lea", "mul", "div", "in", "lodsb", "lodsw", "xchg") and jo \
                        and jo[0].type == x86.X86_OP_REG:
                    tracked.discard(_reg(jo[0].reg))
                if not tracked:
                    break
    # Only 16-bit fields can hold a label: a value encoded as imm8/disp8 stays numeric.
    by_addr = {i.address: i for i in insns}
    for a in [a for a in imm_refs if by_addr[a].imm_size != 2]:
        del imm_refs[a]
    for a in [a for a in disp_refs if by_addr[a].disp_size != 2]:
        del disp_refs[a]
    if word_tables is not None:
        for ref in (imm_refs, disp_refs):
            for src, tgt in ref.items():
                if word_table_evidence(insns, index[src]):
                    word_tables.add(tgt)
                if code_tables is not None and code_table_evidence(insns, index[src]):
                    word_tables.add(tgt)
                    code_tables.add(tgt)
    return imm_refs, disp_refs
