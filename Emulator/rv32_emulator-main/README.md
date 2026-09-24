# rv32

[![build and test](https://github.com/A-Eren/rv32_emulator/actions/workflows/ci.yml/badge.svg)](https://github.com/A-Eren/rv32_emulator/actions/workflows/ci.yml)

An **RV32IMC_Zicsr_Zifencei** emulator, assembler and debugger for learning and
teaching assembly. Not a fast emulator — a visible one. Write assembly in the
window, press `F5`, and step through it one pipeline cycle at a time.

Four questions shaped everything in it:

1. **What exactly changed in this step?** — register and memory diff highlighting
2. **How did this instruction become 16 or 32 bits?** — a bitfield breakdown,
   which for a compressed instruction also shows the one it expands to
3. **Why did it not assemble?** — line, column, caret, and a suggested fix
4. **Where is each instruction right now?** — the five pipeline stages with the
   instruction in each, a space-time diagram underneath, and the same stages
   named in the editor margin beside your code

## Install

Both downloads are on the
**[latest release](https://github.com/A-Eren/rv32_emulator/releases/latest)**.
Neither needs a compiler, a toolchain or admin rights, and CI builds both from
the same commit it tests.

### Linux

Download **`rv32-x86_64.AppImage`**, then run the installer once:

```bash
./packaging/install.sh ~/Downloads/rv32-x86_64.AppImage
```

Without a clone of this repository, download
[`packaging/install.sh`](packaging/install.sh) into the same folder as the
AppImage and run `./install.sh` — it finds an AppImage sitting beside it.

rv32 is then in the applications menu with its own icon, `.s` files open with a
double-click, and `rv32` and `rv32-gui` work from a terminal. Everything lands
under `~/.local`, nothing needs root, and `install.sh --uninstall` takes it all
back out.

The installer unpacks the AppImage rather than leaving it as one file, because
mounting an AppImage needs `libfuse2` and several current distributions no
longer install it — a double-click that silently does nothing is a poor way to
discover that. If your machine does have it, the AppImage runs on its own after
`chmod +x`.

### Windows

Download **`rv32-windows-x64.zip`**, unzip it anywhere, and double-click
`rv32-gui.exe`. Nothing to install: the Qt DLLs and the platform plugins travel
with it, and the directory in the zip is the one CI just ran with nothing but
Windows on `PATH` — so it starts on a machine that has never seen a compiler.

Keep the DLLs and the `platforms\` folder next to `rv32-gui.exe`; it will not
start without them. `rv32.exe`, the command-line half, is a single file you can
copy onto a lab machine on its own.

### From source

```bash
sudo apt install ninja-build qt6-base-dev   # Qt6 is optional
cmake --preset debug
cmake --build build/debug
ctest --preset debug
```

Full instructions — Windows, the AppImage, what CI builds, how a release is cut
— are in [`docs/building.md`](docs/building.md).

## Using it

### The window

```bash
rv32-gui examples/fib.s      # or just rv32-gui, and start typing
```

| Key | What it does |
|---|---|
| `F5` | Assemble the buffer and load it |
| `F8` | Step one pipeline cycle |
| `F6` | Step **back** one cycle, undoing what it committed |
| `F9` | Run until something stops it |
| `F4` | Reset, keeping breakpoints |
| `Ctrl+E` | Generate `imem.mem` and `dmem.mem` |

Click the margin to set a breakpoint. The editor suggests the instruction in a
small block under the caret that folds away once it is chosen, then greys the
operands it still wants in — a form to fill rather than a manual to consult.

Every key and every panel: [`docs/keybindings.md`](docs/keybindings.md).

### The command line

```bash
rv32 run examples/fib.s              # assemble and run
rv32 run examples/fib.s --trace      # one line per instruction
rv32 run examples/timer.s            # a timer interrupt, from scratch
rv32 asm examples/fib.s              # write imem.mem and dmem.mem
rv32 asm examples/fib.s --annotate   # ... with disassembly comments
rv32 dis imem.mem                    # disassemble a $readmemh image
rv32 test tests/asm/*.s              # self-checking assembly tests
rv32 dbg examples/fib.s              # the interactive debugger
```

**Generate** — `Ctrl+E` in the window, `rv32 asm` on the command line — writes
both memories as `$readmemh` images, padded to the depth the size list
advertises. That is the same number the Block Memory Generator asks for, so the
file drops into a BRAM without arithmetic in between.

The machine is configurable: both memory sizes, and the peripherals — a button,
a display, a block of preloaded ROM, or one you define yourself with registers
you can type into and an interrupt line you can raise.

```bash
rv32 run examples/fib.s --devices examples/board.toml
```

`--devices` gives the run the machine a board file describes. It is the same
file the window loads with **File → Load peripherals**, so a machine assembled
by hand in the GUI can be handed straight to a script. See
[`docs/memory-map.md`](docs/memory-map.md).

### The debugger

It steps by source line by default, so a `li` that expands to two instructions
counts as one step — and `si` shows the seam:

```
(rv32) break done
breakpoint 1 at line 32
(rv32) run
breakpoint 1 at 0x00000028 (line 32)
=> 00000028  lui a0, 0x0        [line 32, 1/2]
   la      a0, result
(rv32) back
=> 00000024  jal zero, 0x10        [line 27]
   j       loop
```

`back` is not a typo: every step records what it changed, so execution runs
backwards as easily as forwards.

### When it does not assemble

```
error[E0310]: 'call' needs a code address, but this is a data symbol
  --> bad.s:9:17
   |
 9 |         call    msg
   |                 ^^^ lives in data memory
 2 | msg:    .asciz "hello"
   | ^^^^ defined in .data here
   |
help: instruction memory and data memory are separate address spaces on this
      machine; use 'la' to load a data address into a register
```

```
error[E0201]: undefined symbol 'lop'
  --> bad.s:8:25
   |
 8 |         beq     a0, a1, lop
   |                         ^^^ not defined in this file
   |
help: did you mean 'loop'? (defined at line 11)
```

## Documentation

- [`docs/keybindings.md`](docs/keybindings.md) — every key, and what the panels show
- [`docs/memory-map.md`](docs/memory-map.md) — the two address spaces, MMIO, and the `.mem` format
- [`docs/isa-support.md`](docs/isa-support.md) — what is implemented, what is not, and why
- [`docs/building.md`](docs/building.md) — building on Linux and Windows, the AppImage, CI, releases
- [`docs/architecture.md`](docs/architecture.md) — how the layers fit, and why the ISA is described once
- [`docs/testing.md`](docs/testing.md) — running the suites, and where correctness comes from

## License

MIT. See [LICENSE](LICENSE).
