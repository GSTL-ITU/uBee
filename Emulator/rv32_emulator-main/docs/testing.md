# Testing

```bash
ctest --preset debug                    # everything
./build/debug/rv_tests --filter=exec_m  # one suite
./build/debug/rv_tests --list           # what exists
```

`ctest --preset asan` runs the same suite under ASan and UBSan. CI runs both,
under both g++ and clang++.

The self-checking assembly programs are also runnable on their own, which is
the shortest path from "does this machine work" to an answer:

```bash
./build/debug/rv32 test tests/asm/*.s
```

## Where correctness comes from

There is no reference toolchain on the machines this was written on — no Spike,
no `riscv64-unknown-elf-gcc` — so correctness is not established by comparing
against one. It rests on five things instead:

- **Round-trip properties** — `encode(decode(w)) == w` over 500 000 random
  words, plus every table row against itself. The 16-bit space is small enough
  to do properly: all 65 536 compressed halfwords are swept exhaustively, holes
  included. No golden files, and it catches any misplaced bit mechanically.
- **A semantic checklist** — each test cites the spec rule it enforces
  (`jalr` clearing the low bit, `div` by zero yielding −1, `srai` sign
  behaviour, shift amounts masked to 5 bits).
- **Self-checking `.s` programs** — riscv-tests style, written in the language
  the tool is *for*. One of them exercises the assembler, encoder, decoder,
  executor and traps in a single run.
- **Offscreen GUI tests** — the window is created for real on Qt's offscreen
  platform, driven through the actions the toolbar calls, then asked what it
  shows. No display needed, so it runs in CI, and it exercises the real path
  rather than a parallel one.
- **The pipeline view against the machine** — a program driven a cycle at a
  time must end in exactly the state it reaches driven an instruction at a
  time: pc, `cycle`, `instret` and all 32 registers, for 32-bit and compressed
  control flow alike. That is what stops a drawing from quietly becoming a
  second implementation.
