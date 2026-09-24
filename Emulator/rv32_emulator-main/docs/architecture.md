# How it is put together

The rule that shapes everything: **nothing below `rv_cmd` knows a user
interface exists.** The engine is a library, the window is a shell over it, and
the command-line debugger is a second shell over the same one.

```
rv_isa    instruction table, encode, decode, disassemble    (no dependencies)
 ├── rv_core   registers, memory, MMIO, execute, traps, interrupts
 └── rv_asm    lexer, parser, assembler
       └── rv_dbg    breakpoints, stepping, reverse step, the pipeline view
             └── rv_cmd    command registry + REPL, no Qt
                   ├── rv32-gui   the desktop window
                   └── rv32       the command-line tools
```

Both front-ends are shells over `DebugSession` and `rv_cmd`. Neither implements
stepping, breakpoints or highlighting of its own, which is why they cannot
disagree about what any of those mean.

`rv_cmd` sitting *below* both front-ends is deliberate: `break`, `step` and
`x/16xw` are implemented once, and the window reaches the same registry the
command line does.

## The ISA is described once

`src/isa/instr_table.def` is an X-macro table with one row per instruction. It
generates the `InstrId` enum, the descriptor table, and the mnemonic lookup —
so the decoder, the disassembler, the encoder and the assembler all read from
one description and cannot drift apart. The editor's completion hints are
generated from that same table, so a hint cannot promise something the
assembler will refuse.

Execution semantics deliberately stay out of it. `src/core/exec.cpp` is a plain
`switch` over `InstrId` with **no `default:` label**, compiled under
`-Werror=switch`: adding a row to the table breaks the build until the executor
handles it. That is the same "you cannot forget one" guarantee a generated
dispatch table gives, without putting semantics inside a macro — and semantics
are exactly what this project exists to make readable.

## Harvard memory — and what it costs

IMEM and DMEM are **separate address spaces**, both starting at 0 and both 16 KB
by default. Instruction fetch never touches the data bus. See
[`memory-map.md`](memory-map.md) for the consequence that shapes the assembler:
`la a0, some_data_label` cannot use `auipc`, because that would compute an
address in the wrong space.

## The pipeline is a picture, not a re-timing

The view draws five stages and steps by cycle, but instructions still execute
atomically — each one runs on the cycle it reaches write-back. `mcycle` still
counts instructions, and the panel keeps its own cycle count beside it.

Branch targets are computed in EX and followed on an always-taken guess; the
condition is only known when the instruction runs, so a wrong guess is caught at
write-back and costs more than it would in hardware. Hazards, stalls and
forwarding are not implemented.

What keeps the drawing honest is a test rather than a promise: a program driven
a cycle at a time must end in exactly the state it reaches driven an instruction
at a time — pc, `cycle`, `instret` and all 32 registers. See
[`testing.md`](testing.md).

## How it got here

| Milestone | Scope | State |
|---|---|---|
| M0 | Build skeleton, test harness | ✅ done |
| M1 | Registers, memory, first instructions | ✅ done |
| M2 | Full ISA, traps, CSRs, disassembler, `.mem` export | ✅ done |
| M3 | Assembler + source map | ✅ done |
| M4 | CLI debugger | ✅ done |
| M5 | Front-end shell | ✅ done |
| M6 | Embedded editor | ✅ done |
| M7 | Encoding panel, diff highlighting, reverse step | ✅ done |
| M8 | Polish, examples, docs | ✅ done |
| M9 | Pipeline view: five stages, space-time diagram | ✅ done |
