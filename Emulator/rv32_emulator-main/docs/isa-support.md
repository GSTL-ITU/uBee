# What is implemented

**RV32IMC_Zicsr_Zifencei**, machine mode only, with machine-mode interrupts.
The `misa` register reads `0x40001104`.

The authoritative list is `src/isa/instr_table.def` — this document describes
it, but that file *is* it: the decoder, the disassembler, the encoder and the
assembler are all generated from those rows.

## Instructions

**RV32I (37)** — `lui auipc jal jalr`, `beq bne blt bge bltu bgeu`,
`lb lh lw lbu lhu`, `sb sh sw`,
`addi slti sltiu xori ori andi slli srli srai`,
`add sub sll slt sltu xor srl sra or and`.

**M (8)** — `mul mulh mulhsu mulhu div divu rem remu`.

**Zicsr (6)** — `csrrw csrrs csrrc csrrwi csrrsi csrrci`.

**C (27)** — 16-bit encodings, listed by quadrant below.

**System (6)** — `fence fence.i ecall ebreak mret wfi`. `fence` is
architecturally a no-op on a single in-order hart. `fence.i` (Zifencei) is one
for a stronger reason: the machine is Harvard, so a program cannot write the
instruction stream it fetches from and there is never a stale fetch to flush.
Both must still assemble, because code written against a real toolchain contains
them. `wfi` advances the cycle counter to the armed timer's deadline rather than
spinning; see the interrupt section below.

## The C extension

| Quadrant | Instructions |
|---|---|
| Q0 (`op=00`) | `c.addi4spn c.lw c.sw` |
| Q1 (`op=01`) | `c.addi c.nop c.jal c.li c.addi16sp c.lui c.srli c.srai c.andi c.sub c.xor c.or c.and c.j c.beqz c.bnez` |
| Q2 (`op=10`) | `c.slli c.lwsp c.jr c.mv c.ebreak c.jalr c.add c.swsp` |

Absent from Q0 and Q2 are the `funct3 = 001/011/101/111` slots, which are
`c.fld`/`c.flw`/`c.fsd`/`c.fsw` and the RV64-only `c.ld`/`c.sd` — excluded along
with F, D and RV64 themselves.

**Every compressed instruction is an alias.** `c.addi a0, 1` *is*
`addi a0, a0, 1`; the C extension adds no operations, only shorter spellings of
ones RV32I already had. The emulator takes that literally: `src/isa/expand.cpp`
rewrites each compressed instruction into the base instruction it stands for,
and `src/core/exec.cpp` has no compressed cases at all. All 27 inherit semantics
that were already tested rather than getting a second implementation that can
drift from the first. The encoding panel shows both forms side by side, which is
the part worth teaching.

**Registers.** The 3-bit fields (written `rd'`, `rs1'`, `rs2'`) reach only
`x8-x15` — `s0`, `s1` and `a0-a5`. Choosing those eight rather than any eight is
what makes the encoding pay for itself. `c.lw t0, 8(a1)` does not assemble, and
says so:

```
error[E0107]: 'c.lw' cannot reach t0
help: this form encodes its registers in 3 bits, so it can only use
      s0, s1, a0-a5 (x8-x15); use the 32-bit form for anything else
```

**Nothing is compressed automatically.** Writing `addi a0, a0, 1` always
produces four bytes; you get two only by writing `c.addi a0, 1`. This keeps the
rule stated under *Pseudo-instructions* below — sizes are fixed at parse time —
true for real instructions too, and it keeps the disassembly a literal echo of
the source. The cost is that you must know the short form exists; the editor's
completion list and operand hints are generated from the same table, so typing
`c.` shows all 27 with their real ranges.

**Reserved encodings.** Several holes in the encoding space cannot be written as
a match/mask — each is "this field must not be zero", and a mask can only say
"these bits are fixed" — so `src/isa/decode.cpp` enforces them by hand:
`c.addi4spn` with `nzuimm == 0`, `c.addi16sp` and `c.lui` with `imm == 0`,
`c.lui` with `rd == 0`, `c.jr`/`c.jalr` with `rs1 == x0`, and `c.lwsp` with
`rd == x0`. The first
carries real weight: `0x0000` is a `c.addi4spn`, and it has to stay illegal or
running off the end of a program into zeroed memory would quietly execute
instead of halting.

The spec's HINT encodings — `c.mv` and `c.addi` with `rd == x0`, a shift of zero
— are legal and execute as nops, so the decoder accepts them and the
disassembler round-trips them. The *assembler* still refuses to write one, since
typing it is almost always a mistake.

## Pseudo-instructions

`nop mv not neg seqz snez sltz sgtz`,
`beqz bnez blez bgez bltz bgtz bgt ble bgtu bleu`,
`j jal jr jalr ret call tail`,
`li la`,
`csrr csrw csrs csrc csrwi csrsi csrci`.

Sizes are fixed at parse time — there is no branch relaxation. `la` and `call`
are therefore always two words, even when the target would fit in one; the
assembler says so once per file, because the disassembly will honestly show a
`lui rd, 0` that otherwise looks like a bug. `li` is the exception: its operand
is a literal the parser has already read, so it is sized exactly.

## Directives

`.text .data`, `.word .half .byte`, `.ascii .asciz .string`, `.space .zero`,
`.align .org`, `.equ .set`, `.globl .global`.

A directive name is only a directive in the leading position, so `.zero` and
`.align` remain usable as local label names.

## CSRs

`mstatus misa mie mtvec mscratch mepc mcause mtval mip`,
`mvendorid marchid mimpid mhartid`,
`mcycle mcycleh minstret minstreth`,
`cycle cycleh time instret instreth timeh`.

Each has a WARL write mask, so reserved bits read back at their reset value
rather than accepting anything. The user-mode counter shadows at `0xc00`-`0xc82`
are read-only: `csrrw` targeting one raises IllegalInstruction rather than
silently doing nothing. Their machine-mode counterparts `mcycle` and `minstret`
*are* writable, as the spec requires, and a write moves the hart's own counter
rather than being overwritten by the next refresh — so `csrw mcycle, zero` really
does zero it before a measurement.

`time` mirrors `cycle`, which is the honest thing to report for a machine with
no real clock.

### The counters are not pipeline cycles

`mcycle` counts *instructions* — this hart executes one at a time and there is
no pipeline underneath it. The desktop window's Pipeline panel draws five stages
and keeps a cycle count of its own, and the two numbers are deliberately
different: a cycle the panel spends filling the pipe, or throwing away a
mispredicted guess, is not a cycle this machine experienced. Nothing a program
can read is affected by the panel being open. See
[keybindings.md](keybindings.md) for what it draws and what it is honest
about.

## Deliberately absent

Not oversights — each was ruled out because it costs a milestone and buys
nothing for learning assembly on a small FPGA core:

- **F/D (floating point)** — implementable in the emulator, heavy to synthesise.
- **A (atomics)** — one hart, so nothing to be atomic against.
- **User and supervisor mode, PMP, `satp`, interrupt delegation** — most of the
  privileged spec's complexity for none of its teaching value here.
- **Branch relaxation** — see above.
- **ELF input or output** — no RISC-V toolchain is assumed; this project
  assembles its own code.

## Interrupts

Machine mode, three lines, direct `mtvec` only:

| Line | `mcause` | Source |
|---|---|---|
| Machine software | `0x80000003` | the request register at `0xFFFF0050` |
| Machine timer | `0x80000007` | `mtime >= mtimecmp` |
| Machine external | `0x8000000B` | the request register at `0xFFFF0054` |

An interrupt is taken when `mstatus.MIE` is set *and* the line is unmasked in
`mie` *and* it is asserted in `mip`. `mip` is read-only and recomputed from the
devices before every step, so writing it does nothing. Priority is external,
then software, then timer.

The difference from an exception, and the thing handlers get wrong: `mepc`
points at the instruction that has **not** run yet. `mret` resumes it. An ecall
handler must advance `mepc` past the ecall or it re-executes forever — by 4 for
`ecall`, or by 2 if the trap came from a compressed instruction, which is why a
general handler reads the instruction rather than assuming; an interrupt handler
must **not** advance it at all, or it silently drops an instruction. `tests/asm/t_trap.s`
checks both, and deliberately introducing the second mistake makes it fail.

Acknowledging is the program's job. A pending line that the handler does not
clear re-enters the handler immediately and forever — writing `mtimecmp`
forward, or clearing the request register, is what stops it.

No vectored mode, no delegation, no nesting, and no interrupts below machine
mode.

## Semantics worth stating

With no reference toolchain available, these are the rules the tests pin down
explicitly, each citing the spec:

- Shift amounts use the low 5 bits only. `x << 32` is `x << 0`.
- `srl` is logical, `sra` arithmetic.
- `sltiu` sign-extends its immediate and *then* compares unsigned, which is
  what makes `sltiu rd, rs, 1` mean "rs == 0".
- `jalr` clears the low bit of the computed target, and computes it before
  writing the link register, so `jalr ra, ra, 0` jumps to the old `ra`.
- `auipc` is relative to its own address, not `pc + 4`.
- With the C extension present IALIGN is 16, so a branch or jump target two
  past a word boundary is a perfectly ordinary address — it is where the second
  of two compressed instructions in a word lives. Only an *odd* target raises
  InstructionAddressMisaligned, which nothing a program can execute reaches:
  branch and jump immediates are always even, and `jalr` clears the low bit. The
  check is still there because a debugger can set an odd pc.
- A 32-bit instruction may straddle a word boundary. Once code is a halfword
  stream nothing keeps four-byte instructions on four-byte addresses, so the
  fetch reads a halfword, decides the width from its low two bits, and reads the
  second halfword only if it needs it.
- `mepc` holds bit 1 but not bit 0, for the same reason.
- Misaligned loads and stores trap rather than being emulated — on the FPGA
  core this is a reference for, a misaligned access is a real bug.
- Division never traps: `x / 0` is all ones, `x % 0` is `x`,
  `INT32_MIN / -1` is `INT32_MIN` and `INT32_MIN % -1` is 0.
- Division truncates toward zero, and the remainder takes the dividend's sign.
- `csrrs`/`csrrc` with `x0` as source do not write; `csrrw` with `x0` as
  destination does not read. The condition is the register *number*, not its
  value.
- `csrrwi` and friends take a 5-bit zero-extended immediate.
- Writes to `x0` are discarded.
- An `ebreak` halts to the debugger by default; `EbreakBehavior::TrapToMtvec`
  makes it trap instead, which the Zicsr tests use.
- A trap taken while `mtvec` is still zero halts and says so, rather than
  jumping to address 0 and silently restarting the program. So does an
  interrupt: enabling a line without installing a handler is a mistake worth
  naming.
- Trap and interrupt entry save `mstatus.MIE` into `MPIE` and clear `MIE`, so a
  handler is not immediately interrupted by the line it is servicing.
- `wfi` observes `mie` alone, not `mstatus.MIE`: the spec has it resume on a
  pending enabled interrupt whether or not one could actually be taken.
