# Memory map

This machine is **Harvard**: instruction memory and data memory are separate
address spaces. Instruction fetch reads IMEM and never goes through the data
bus; loads and stores reach DMEM and MMIO and can never see IMEM.

By default both spaces start at address **0** (teaching machine). A board file
can place them at SoC bases — see [uBee SoC alignment](#ubee-soc-alignment)
and `examples/ubee.toml`.

## Instruction memory (IMEM)

| Range | Size | Access |
|---|---|---|
| `0x00000000` – … (default) | 16 KB by default = 4096 instructions | fetch only |
| or board `imem_base` – … | same | fetch only |

**The size is configurable** — the two lists above the hex table on the Memory
tab, which is what they are a view of, or `[machine]` in a board file. Between 256 bytes and 1 MB, a multiple
of four, since both spaces are exported a word per line. Growing a memory keeps
what was in it; shrinking one drops what no longer fits, so press F5 afterwards
to assemble into the machine you now have.

Exported as `imem.mem`. Not readable with `lw`, not writable at all — there is
no self-modifying code on this machine.

### When a program does not fit

The lines whose instructions land past the end are **marked, not cut**: a red
bar in the margin and the line painted red across its width.

```
  66 │        addi    a0, a0, 15
▮ 67 │███████ addi    a0, a0, 0 ██████████
▮ 68 │███████ addi    a0, a0, 1 ██████████
```

Loading truncates at the end of the memory, so without the mark a program too
big for the machine quietly becomes a *different* program — one that runs off
the end of what was loaded and does whatever is there. The mark says which
instructions have nowhere to go, and leaves the program alone.

It is not an error. What is written is valid; there is simply nowhere to put
it, and either a bigger memory or a shorter program makes it go away. Both ends
of that comparison can move, so it is recomputed after assembling and after
either memory is resized.

The marked lines come from the source map rather than from counting, because
one line can be two instructions and the second may be the one that does not
fit. Data past the end of DMEM says so in the message line instead: `.data`
lives in its own space and has no line in the map to paint.

## Data memory (DMEM)

| Range | Size | Access |
|---|---|---|
| `0x00000000` – … (default) | 16 KB by default | load / store |
| or board `dmem_base` – … | same | load / store |

Configurable in the same place. Exported as `dmem.mem`. Address *N* here is a
*different location* from address *N* in IMEM (Harvard).

## Peripherals

The **default** teaching machine uses a one-kilobyte window at
`0xFFFF0000`–`0xFFFF03FF`, divided into 64 slots of 16 bytes. Devices may also
be placed at **any absolute address** from a board file (for example uBee SoC
UART at `0x40004000` or AXI UART at `0x60000000`).

An address that maps to nothing attached **faults**. Reading as zero would let
a program touch a peripheral that is not there and never find out.

The default machine, if nothing says otherwise:

| Slot | Address | Type | |
|---|---|---|---|
| 0 | `0xFFFF0000` | `uart` | `+0` W transmit a byte; `+4` R status, bit 0 = ready |
| 1 | `0xFFFF0010` | `leds` | 32-bit output register |
| 2 | `0xFFFF0020` | `switches` | 32-bit input register |
| 3 | `0xFFFF0030` | `mtime` | the clock, low and high words; one tick per instruction |
| 4 | `0xFFFF0040` | `mtimecmp` | timer compare; an interrupt is pending while `mtime >= mtimecmp` |
| 5 | `0xFFFF0050` | `irq` | `+0` software request, `+4` external request |

And the types that can be added:

| Type | | Sized in |
|---|---|---|
| `button` | momentary inputs; raise the external interrupt line while held | bits |
| `value` | a number the user types in, standing in for a sensor or an ADC | — |
| `display` | 7-segment digits, one byte each | — |
| `ram` | a memory block, optionally preloaded with `load` | bytes |
| `rom` | the same, but stores from the program are ignored | bytes |
| `custom` | your own: a `name`, registers the program can read and write, each settable by hand, and a line it can raise an interrupt on | registers |
| `function` | a peripheral that **computes**: operands in, a result out, after a latency | bits per operand |

`leds` and `switches` are sized in bits too. Every size is chosen from a list of
powers of two, for the same reason the memory sizes are: this is hardware, and a
width or a depth that is not one wastes what it is built from.

### Naming a device

Any device may be given a name. The list has a column for it, next to but
separate from the type: a name and a type are different things, and a type is
not a name that happens to be shared. Double-click the name to change it after
the fact — what two of a kind should be called is usually only clear once both are
there. Clearing the name puts the type back rather than leaving the device with
nothing to be called.

```toml
[[device]]
type = "uart"
address = 0xffff0010
name = "plotter"
```

### How wide a bank is

A board has the LEDs and switches it has — sixteen on a Basys 3, eight on
something smaller — and `size` says how many are wired up:

```toml
[[device]]
type = "leds"
address = 0xffff0010
size = 16
```

Bits above the width are **masked off**, not stored. A program that writes bit
20 of an eight-LED bank is writing to nothing, and reading back what it wrote
would be the emulator agreeing with a program the board will not. The panel
shows that many lights and no more.

A row of buttons is one device, not one per button: five direction buttons are
five bits at one address, which is how they are wired and how a program reads
them. The interrupt switch is always the last control, whatever the count.

A memory block takes as many slots as its size needs, so a 128-byte block
occupies eight.

Every block's first sixteen words are offered as fields to type into, in the
Devices panel, alongside the switches and buttons. A block is as much an input
as a bank of switches is: an exercise that reads a table wants the table set by
hand, and going through the memory view to do it means knowing the address the
block happened to land at. What is typed survives a reset — it was not the
program that put it there. The rest of a large block is still reachable in the
memory view; a hundred fields would not be an improvement.

`custom` is the one entry that is not a fixed part somebody else designed. It
is what an exercise reaches for when the peripheral it needs is not in the
list — and being able to raise an interrupt means a student can build the
source of one, not only handle one that was provided.

### A peripheral that computes

`custom` is a register file: it hands back what it was written. An IP core is
not that. Its output is a *function* of its input, and it takes time to produce
one, and that timing is most of what driving a peripheral is about. `function`
is that shape.

```toml
[[device]]
type = "function"
name = "multiplier"
address = 0xffff0060
operation = "mul"       # the behaviour, from the list below
size = 8                # bits per operand
latency = 7             # cycles it reports busy before the result is ready
```

| Offset | | |
|---|---|---|
| `+0x00` | `A` | RW — operand |
| `+0x04` | `B` | RW — operand |
| `+0x08` | `P` low | R — the latched result, low half |
| `+0x0c` | `P` high | R — the latched result, high half |
| `+0x10` | status | R — `0` idle, `1` busy, `2` done |

Five registers a word apart do not fit in one sixteen-byte slot, so a function
device takes two.

The protocol is the one the course's IP homework specifies:

```
write A, write B    the write is what starts it
poll status         until it reads done -- reads only
read P low, P high  the result, as two halves
```

Three parts of it are worth stating, because a program that gets them wrong
gets them wrong on the board in the same way:

- **Any write restarts the operation**, whichever register it names — a store to
  the read-only status register restarts it too, and loads nothing. So the poll
  loop reads and does nothing else. A program that rewrites an operand while
  waiting polls a counter that keeps resetting, and hangs.
- **The result is latched** when the operation completes. While one is in
  flight, the result registers still hold the one before it. A program that
  reads before the status says done reads the previous answer, not this one.
- **Idle lasts exactly the cycle the write lands on.** Nothing can read the
  status in the cycle it wrote, so a poll loop always sees busy at least once
  before it sees done.

The operands are masked to `size` bits and the result is twice that, read back
as two halves of one register — which is why sixteen bits per operand is the
widest on offer. `latency` is in this machine's cycles; see *Time is the instruction
count* below for what one of those is, and why the number will not match the
hardware's cycle for cycle.

| `operation` | |
|---|---|
| `mul` | an unsigned product, twice the operand width |

One behaviour, because one is what the coursework needs — the multiplier IP, and
the projects built on it. Adding another is a row in `devices.cpp`; a peripheral
that runs a *script* is the general answer and a far larger piece of work, and
`custom` plus a Python model covers that ground until someone needs it.

### Overlapping addresses

Two devices may be given addresses that overlap. The later arrival wins the
addresses they share; the earlier one stays attached, and the panel says
`covered by another device` or `(partly covered)` for it.

This is allowed rather than refused because a machine being built by hand
spends most of its life half-built, and stopping the work to complain is worse
than showing what the overlap did. It is a warning, at the point of making it
and afterwards in the list — never a silent one, since a device that quietly
stopped answering is the bug that costs an afternoon.

### Describing a machine

`examples/board.toml` is a worked example. The format is a deliberate subset of
TOML — arrays of tables with scalar keys — parsed in about a hundred lines
rather than taken as a dependency:

```toml
[machine]
imem = 8192             # bytes; omit either to keep what the machine has
dmem = 4096
# Optional SoC bases (omit to keep both at 0):
# imem_base = 0x80000000
# dmem_base = 0x20000000
# reset     = 0x80000000

[[device]]
type = "switches"
address = 0xffff0020    # or slot = 2, which is the same place
value = 0b1010          # a starting position

[[device]]
type = "custom"
name = "sensor"
address = 0xffff0030
size = 2                # registers, for a custom peripheral

[[device]]
type = "rom"
address = 0xffff0090
size = 256              # bytes, for a block
load = "sine_table.mem" # resolved relative to this file
```

Understood keys: `type`, `address` or `slot`, `name`, `size`, `value`, `load`,
`readonly`, `digits`, `operation`, `latency`, and under `[machine]`: `imem`,
`dmem`, `imem_base`, `dmem_base`, `reset`. Anything else is an error naming the
line and listing what was expected.

Addresses are written rather than slot numbers because an address is what the
program uses and what every panel shows; a slot number is arithmetic. An
address may sit in the legacy `0xFFFF0000` window or at a SoC base. An
address inside a slot names that slot, so `0xffff002c` and `0xffff0020` mean the
same device.

Applying a configuration **replaces** the devices rather than adding to them, so
a file describes a machine reproducibly. `File → Save peripherals` writes the
current set back out.

### From the command line

`--devices <file.toml>` gives a run the machine a file describes — its
peripherals and its memory sizes both:

```sh
rv32 run prog.s      --devices board.toml
rv32 test prog.s     --devices board.toml
rv32 dbg prog.s      --devices board.toml
rv32 exec 00a00513   --devices board.toml
rv32 asm prog.s --pad --devices board.toml
```

It is the same file the GUI loads with `File → Load peripherals`, and the same
one `--board` used to name — `--board` still works and means exactly this.

Under `asm` only the memory sizes are read, because that is all an image can
carry: what `--pad` pads to has to be the depth of the array the file will be
read into. Under every other command the peripherals are attached as well.

The configuration is applied **before** the program is loaded, since it can
resize memory and a resize truncates what no longer fits.

### Editing and undoing

Every device implements a side-effect-free `peek`/`poke` pair, and three things
follow from that:

- The memory view shows a peripheral's registers rather than a hole, and they
  can be edited by typing into it.
- A ROM can be edited that way even though a program cannot write it. Being
  unable to would be an obstacle, not a safeguard — the assembler is where a
  write to read-only memory should be caught.
- Reverse stepping undoes a write to any device with no per-device history at
  all: the debugger's ordinary one-word memory delta plus `poke` covers it. Two
  devices need more, and one extra field each is enough for both — the UART's
  output length, since a growing string is not a register, and a `function`
  device's start cycle and last result.

Peeking cannot consume anything, which matters because a memory view redraws
constantly — merely looking at the machine must not change it.

Switch inputs survive a reset, since physical switches on a board do not move
when the core is reset. A preloaded memory block survives one too: a lookup
table has to still be there after the machine restarts.

### Time is the instruction count

There is no wall clock. `mtime` is the **retired-instruction count** — it
advances by one per instruction — which is the choice that makes everything else
work: the same program takes the same interrupt on the same instruction every
run, so the self-checking test programs stay meaningful and reverse stepping is
exact rather than approximate.

**Hardware `mtime` counts clock cycles instead**, and an instruction is more
than one cycle there. So a delay tuned by counting `mtime` here is not the same
delay on the board, and the two will not agree instruction for instruction on
when a deadline passes. Everything that follows from a *comparison* — waiting
until `mtime` reaches a value, measuring one interval against another — carries
over; a number of cycles does not.

`mtimecmp` is 32 bits here, where a real machine has a 64-bit one that must be
written high half first to avoid firing spuriously in between. That hazard is
worth knowing about; it is not worth inflicting on someone learning assembly,
and no program on a 4096-instruction machine reaches four billion cycles.

`wfi` follows from the same choice. Rather than spinning, it advances the cycle
counter to the armed timer's deadline — the machine has nothing else to do, and
burning instructions would make the cycle count meaningless. With nothing armed
and nothing pending, it halts and says so instead of waiting forever.

### Interrupts land earlier here than on hardware

This machine checks for a pending interrupt strictly between instructions, so a
handler begins the moment the deadline passes. A pipelined core takes one only
when it can — instructions already in flight retire first — so the handler
starts a little later there. Neither is wrong: RISC-V does not specify interrupt
latency.

The gap is one or two instructions per interrupt, and it is not constant: it
depends on what happened to be in the pipeline, so the same program shows a
different skid at different points in its own run. The window's Pipeline panel
draws instructions in flight, but it does not change any of this: the emulator
still takes an interrupt strictly between instructions, so what the panel shows
in IF and ID is not something the handler is waiting for. It also **compounds**, when a
handler rearms the deadline from inside itself — each late start pushes the next
one later. Measured against the FPGA core this emulator is a reference for, over
eight ticks of one program, `mtime` at handler entry drifted from 0 to 10.

What this means for a program:

- A program written **producer/consumer** — the handler publishes, the main loop
  consumes only what has been published — is untouched by it, and prints
  identical output on both machines while their timers drift apart.
- A program whose output depends on **when** an interrupt lands will answer
  differently on the board, and the gap widens over a run.

The second is worth knowing before it is discovered on hardware. It is also
worth knowing that the first is a design choice available for free.

## Alignment

**Data.** Word and halfword accesses must be naturally aligned. A misaligned
access raises `LoadAddressMisaligned` or `StoreAddressMisaligned` rather than
being quietly emulated. This is deliberate: on the FPGA core this emulator is a
reference for, a misaligned access is a real bug, and a named trap teaches
alignment where silent success would hide it.

**Instructions follow a separate and looser rule.** With the C extension present
IALIGN is 16, so an instruction need only be halfword-aligned: a compressed
instruction can sit at any even address, and a 32-bit instruction may straddle a
word boundary. The fetch reads a halfword, takes the width from its low two
bits, and reads a second halfword only if it needs one. Only an *odd* pc raises
`InstructionAddressMisaligned`, and nothing a program can execute reaches one —
branch and jump immediates are always even, and `jalr` clears the low bit — so
in practice that trap is reachable only by setting the pc from the debugger.

## Why `auipc` cannot address data

This is the one consequence of the Harvard model that reaches all the way into
the assembler, so it is worth stating plainly.

`auipc` computes an address relative to the program counter — that is, relative
to a position in **IMEM**. A data symbol lives in **DMEM**. Adding an offset to
the pc can never produce a valid DMEM address, because the two spaces are
unrelated.

Therefore:

- `la rd, data_symbol` expands to **absolute** `lui` + `addi`, never
  `auipc` + `addi`.
- `call`, `j` and `tail` target text symbols and expand **pc-relative**.
- Every symbol carries a `Space` tag (`Text` or `Data`), and mixing them is a
  compile error rather than a runtime mystery:

  ```
  error[E0303]: 'msg' is a data symbol, but 'call' requires a text symbol
    --> hello.s:12:10
     |
  12 |     call msg
     |          ^^^ defined in .data at line 3
  ```

- `.word` inside `.text` emits into instruction memory and cannot be read with
  `lw`, so the assembler warns about it.

Handled this way the restriction teaches the architecture. Ignored, it produces
programs that assemble cleanly and behave insanely.

## `.mem` export format

One 32-bit word per line, lowercase `%08x`, no `@` address directives, `\n` line
endings — directly consumable by `$readmemh`:

```verilog
reg [31:0] imem [0:4095];
initial $readmemh("imem.mem", imem);
```

The byte address of line *n* is `n * 4` — unchanged by the C extension, because
the file is a picture of memory rather than a list of instructions. A line can
hold two compressed instructions, or the tail of one 32-bit instruction and the
head of the next; `--annotate` walks the halfword stream and names each
instruction against the line it starts on. `dmem.mem` packs the byte image
little-endian, so `.word 0x12345678` appears as the single line `12345678`.

By default the CLI truncates the file at the high-water mark; `--pad` emits the
full array.

## uBee SoC alignment

The default teaching map (`IMEM`/`DMEM` at `0`, MMIO at `0xFFFF0000`) is **not**
the uBee SoC map:

| Region | Default emulator | uBee SoC (`examples/ubee.toml`) |
|---|---|---|
| IMEM / reset | `0x00000000` | `0x80000000` |
| DMEM | `0x00000000` | `0x20000000` |
| Native UART | `0xFFFF0000` | `0x40004000` |
| AXI UART (BD hello) | — | `0x60000000` |

`.mem` / `.hex` export is a **word image** for BRAM init (line *n* → word *n*).
That part is fine on the FPGA. What must match the SoC are the **absolute
addresses baked into the program** by `la`, `li`, and MMIO stores.

For FPGA / Vivado IP work:

```bash
rv32 asm prog.s --pad --devices examples/ubee.toml
# or in the GUI: File → Load peripherals… → examples/ubee.toml, then Assemble / Generate
```

Without that board file, a program that uses `la` to data or `0xFFFF…` MMIO will
not run correctly on uBee even if the `.mem` depths match the BRAMs. Device
*register layouts* in the emulator are still the teaching models — address
alignment is what makes the image portable; full CLINT/PLIC/AXI parity is not
claimed.

### Getting it into Vivado

**Generate** on the toolbar, next to Assemble (`Ctrl+E`), writes both images, **always padded to the
full depth**. That is the point of padding: the file is read into an array of a
fixed depth, and a file shorter than the array leaves the tail holding whatever
the tool put there. That reads as the program going wrong rather than as the
file being short, which is an afternoon nobody gets back.

The depth is the number to configure the memory with, and it is on both ends of
the same line. The size list in the Devices panel says

```
imem  16 KB — 4096 × 32-bit
```

and 4096 is what goes into the Block Memory Generator's **Write Depth**, with
32 as the width. The exported file is exactly that many lines.

```verilog
reg [31:0] imem [0:4095];          // 4096 deep, matching the panel
initial $readmemh("rv32_imem.mem", imem);
```

The list offers powers of two because that is how block memory comes: a depth
that is not one wastes a whole block. A machine may still be given any size a
file names — the list is what is worth offering, not what is allowed — and the
panel will show it rather than round it silently.

#### One block on its own

Select a `ram` or `rom` in the Devices panel and its contents appear below it,
as memory — the same hex table the Memory tab uses for dmem — with **Load
.mem…** and **Export .mem…** above them. A block is looked at where it is
configured, rather than through a list on a tab that is about something else.

A block's contents are a memory image in their own right — a lookup table, a font, a sine wave — and what is
wanted on the far side is usually that block in a BRAM of its own, not the
program's data memory with the block somewhere inside it.

That file is padded to the block, not to dmem: a 128-byte block exports 32
lines, whatever is written in it. The depth is printed above the buttons for
the same reason it is on the size lists — it is what the Block Memory
Generator asks for.

Loading is the same door in the other direction, and is how a table gets in
after the block already exists rather than only at the moment it is created. A
file with more words than the block holds is truncated, and says so.

From the command line, `--pad` alone pads to the default 16 KB. Pass the same
board file the interface uses to pad to the machine that board describes:

```
rv32 asm fib.s --pad --board board.toml
```

If the target Verilog memory is word-addressed without byte enables, `sb` and
`sh` cannot be implemented in hardware. Pass `--strict-word-mem` to have the
assembler warn on every sub-word access, which catches "works in the emulator,
breaks on the FPGA" before synthesis.
