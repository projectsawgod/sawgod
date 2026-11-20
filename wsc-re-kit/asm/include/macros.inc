# Encoding macros for instruction forms GNU as won't emit on its own.
#
# The original assembler encodes 16-bit ALU ops with a 16-bit immediate even when the value would fit
# the shorter sign-extended imm8 form; gas always picks the short form. These macros spell out the
# original encoding.
#
#   accw OP, IMM          accumulator form:    OP ax, IMM   (opcode 05/0D/15/1D/25/2D/35/3D, imm16)
#   aluw OP, REG, IMM     register form:       OP REG, IMM  (81 /r, imm16)
#
# OP is one of add or adc sbb and sub xor cmp; REG is a 16-bit register.

.macro _alu_index op
  .ifc \op,add
    .set _alu, 0
  .endif
  .ifc \op,or
    .set _alu, 1
  .endif
  .ifc \op,adc
    .set _alu, 2
  .endif
  .ifc \op,sbb
    .set _alu, 3
  .endif
  .ifc \op,and
    .set _alu, 4
  .endif
  .ifc \op,sub
    .set _alu, 5
  .endif
  .ifc \op,xor
    .set _alu, 6
  .endif
  .ifc \op,cmp
    .set _alu, 7
  .endif
.endm

.macro _reg_index reg
  .irp r, ax, cx, dx, bx, sp, bp, si, di
    .ifc \reg,\r
      .set _reg, _regn
    .endif
    .set _regn, _regn + 1
  .endr
.endm

.macro accw op, imm
  _alu_index \op
  .byte (_alu << 3) | 5
  .word \imm
.endm

.macro aluw op, reg, imm
  _alu_index \op
  .set _regn, 0
  _reg_index \reg
  .byte 0x81, 0xC0 | (_alu << 3) | _reg
  .word \imm
.endm
