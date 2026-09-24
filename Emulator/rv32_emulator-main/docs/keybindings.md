# Keys

`rv32-gui <file.s>` opens the desktop window. Everything below is also on the
toolbar and in the **Run** and **View** menus, so nothing here has to be
memorised first.

## Running

| Key | Button | What it does |
|---|---|---|
| `F5` | Assemble | Assemble the buffer and load it |
| `Ctrl+E` | Generate | Write `imem.mem` and `dmem.mem`, padded to this machine's depth |
| `F8` | Step | One **pipeline cycle**. An instruction is written back when it reaches WB, so from a standing start the first one takes five presses |
| `F6` | Back | Undo one pipeline cycle, including whatever it committed |
| `F9` | Run | Run until something stops it. Pressing it again interrupts |
| `F4` | Reset | Restart the machine, keeping breakpoints |

**One step, and it is a cycle.** A source line, a call, a whole program are all
some number of cycles; there was nothing a second stepping control expressed
that holding this one down does not, and each extra button was another way to
ask the same question. `Back` is its mirror: one cycle, undoing whatever that
cycle committed.

Two consequences worth knowing. Landing inside a multi-instruction
pseudo-instruction happens on its own now — the status bar shows `[1/2]`, which
is how `li a0, 0x12345` stops being a mystery — and stepping *over* a call means
`Run` with a breakpoint after it. The command-line debugger still has `next` and
`finish` if you want a source line at a time.

`Run` is the one thing that still moves the machine a whole instruction at a
time; the pipeline adopts wherever it leaves it, which is with the pipe full,
since that is how the machine really left it.

`Back` greys out when there is no cycle left to undo.

## Writing assembly

Two halves, and only ever one of them at a time.

**Choosing the instruction.** Type two characters and a small block opens
under the caret with what matches. `Ctrl+Space` opens it explicitly. It floats
over the text, so opening it never moves the line being written, and it shows
eight candidates before saying `+N` rather than quietly dropping the rest.

```
        ad│
        ┌──────────────┐
        │ add    addi  │
        └──────────────┘
          ^^^ the one Tab will take
```

`Tab` walks along it, and each step lands **in the line** rather than only in
the block — the choice is read where the code is. `Enter` settles on the
highlighted one and leaves the caret at its operands. `Esc` folds the block
away, keeping whatever is typed.

| Key | What it does |
|---|---|
| `Ctrl+Space` | Open the block here |
| `Tab` / `Shift+Tab` | Walk forward / back, writing each into the line |
| `Enter` | Settle on the highlighted one |
| `Esc` | Fold the block away |

**Filling in the operands.** Once the instruction is settled the block folds
away and the operands it still wants appear
in grey after the caret, the way a form shows what a field is for. They are not
text: typing goes straight through them, and each disappears as it is filled.

```
        addi rd, rs1, imm          nothing written yet
        addi a0, rs1, imm          rd is done
        addi a0, a1, imm(-2048..2047)
        addi a0, a1, 4             nothing left to say
```

A field shows its **range** only when its name does not already answer the
question. `rd` and `label` say everything; `imm` does not tell you that 5000
will be rejected, so it shows `imm(-2048..2047)` while it is the field being
asked for.

Both halves come from `instr_table.def`, the same rows the assembler encodes
with, so a hint cannot promise something F5 will refuse. That matters most for
the compressed forms, whose ranges are much narrower than their 32-bit
equivalents: typing `c.` lists all 27, each with the range it will actually
take.

## Editing

Standard editing keys, plus:

| Key | What it does |
|---|---|
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | New, open, save |
| `Ctrl+Shift+P` | Turn the pipeline marks on the code on or off |
| `Ctrl+Shift+B` | Turn the green/red branch colours on or off |
| Click the margin | Toggle a breakpoint on that line |
| Click a diagnostic | Jump to the line it points at |

Lines whose instructions land past the end of instruction memory are painted
red with a bar in the margin. Not an error — what is written is valid, there is
simply nowhere to put it — so it marks rather than stops, and a bigger memory
or a shorter program makes it go away.

A failed `F5` keeps the buffer, marks the line in the margin, underlines the
exact span and puts the caret on it. It assembles a detached copy first, so a
broken edit never destroys the program you are already stepping.

## What the panels show

| Panel | |
|---|---|
| Registers | Both spellings (`x10  a0`), zeros dimmed, and the register the last step wrote highlighted **and scrolled into view** |
| Encoding | The instruction as labelled bitfields, and what it did. A compressed instruction is drawn at 16 bits, with the base instruction it expands to underneath. Follows the instruction in write-back — the one that just ran, and so the one whose effect there is anything to describe — or the caret, so a line can also explain itself before it has ever run |
| Pipeline | The five stages — `IF ID EX MEM WB` — with the instruction in each, and underneath them the space-time diagram: the last ten cycles, one row per instruction, one column per cycle, each stage in its own colour. An instruction thrown away stops partway across its row, which is what a flush looks like; the header says `mispredicted` on the cycle a guess turns out wrong. The cycle count here is the pipeline's own, not the `cycle` in the status bar |
| Memory | Data memory, as deep as it actually is, with the two memory sizes above it — this table is what they are a view of. **Editable**: type a hex value into a cell. `Load .mem…` and `Export .mem…` move the whole image in and out |
| Devices | What is attached, name and type in columns of their own, with each device's controls below. Select a block and its contents appear as memory, with its own `.mem` in and out. **Double-click a name to rename it** |
| Console | Whatever the program sent to the UART |
| Symbols | Every symbol with the **space** it lives in — the thing that explains why `call msg` was rejected |

### The pipeline, drawn on the code itself

The five stages are marked on the source lines, so the pipeline can be read
without looking away from what was written. **Named, not shaded**: the stage
sits in the margin next to the line number, in the colour the panel gives it.

```
 22 ME      bge     t2, s0, done
 23 EX      add     t3, t0, t1
 24 ID      mv      t0, t1
 25 IF      mv      t1, t3
```

Exactly two things get a colour across the whole line, so there is never a set
of shades to rank by eye:

- **The line that has just finished** — the one in write-back — in yellow.
  There is only ever one.
- **A branch that has resolved**, green if it was taken and red if it fell
  through. Only at write-back, because that is where the instruction runs and
  the condition is finally known. While it is in flight the branch is marked
  like anything else — the front-end has *guessed* by then, but a guess is not
  an answer.

**Everything on screen agrees about write-back.** The arrow in the margin, the
yellow line, and the Encoding panel all point at the instruction that just wrote
back — not at the pc, which by then is four stages further on and sitting on
code that has not run. The status bar still reports the pc, because that is what
a pc is.

The marking follows the stages, not the source. A line holding a two-word
pseudo-instruction like `li a0, 0x12345` shows its *most advanced* instruction:
with the `lui` in EX and the `addi` still in ID, the line is at EX, because that
is where the pipeline has got to on it. And a line fetched from straight ahead of a
branch loses its mark the moment EX computes the target and the guess turns the
front-end around — or, if the guess was wrong, when write-back throws the pipe
away — so nothing is left marked on code that never ran.

Both halves are optional and independent, under **View**:

| | |
|---|---|
| Pipeline colours in the editor (`Ctrl+Shift+P`) | The stage names and the yellow finishing line |
| Green and red on a resolved branch (`Ctrl+Shift+B`) | Just the branch colours |

The panel is unaffected by either.

### Branches: guessed in EX, settled in WB

**EX computes the target.** That is where the adder is, and the target of a
branch or a `j`/`jal` is just the pc plus the immediate — no register needed. So
the cycle a branch reaches EX, the front-end is told where to go.

**The guess is always taken.** There is no history to consult, and it is right
every time for an unconditional jump. Sending the front-end to the target throws
away the one instruction already fetched from straight ahead, which is the cost
of a correctly guessed branch: one cycle.

**Write-back settles it.** The condition is only known once the instruction
runs, and that happens in WB. If the branch fell through after all, the guess
was wrong and everything in the pipe came from the wrong place, so it is all
discarded and fetching restarts from the fall-through. The panel says
`mispredicted` on that cycle.

`jalr` is not guessed at all: its target is in a register, and a register is not
read until the instruction runs. It redirects at write-back like a
misprediction, and so do traps and interrupts.

### The pipeline is a picture, not a timing model

The stages mean what they mean everywhere else: **IF** fetches, **ID** decodes
and reads the registers, **EX** is the ALU and the branch target adder, **MEM**
is the data access, and **WB** writes the register file.

Underneath, though, the machine has no pipeline: `Hart::step()` fetches,
decodes, executes and commits one instruction in a single call. The view runs
that call the cycle the instruction reaches write-back, because that is where an
instruction becomes part of the machine — so the register and memory panels
change on the cycle the pipeline says they should.

The cost of doing it that way is where a mispredicted branch is caught. Real
hardware compares the registers in EX and knows immediately; here the comparison
is part of running the instruction, so it is not known until WB and the flush is
deeper than it would be. The picture is right about what each stage does, about
the order, and about the cost of a *correct* guess; it overstates the cost of a
wrong one.

`mcycle`, `cycle` and `time` still count instructions, and the timer fires
exactly when it did before. The panel keeps its own cycle count, shown in its
top right, and it is deliberately not the same number as the one in the status
bar: a cycle spent filling the pipe is not a cycle the machine experienced.

There are still no hazards, no stalls and no forwarding.

## The command-line debugger

`rv32 dbg <file.s>` is the same engine without a window, for scripting or over
ssh:

```
(rv32) break 12
(rv32) run
(rv32) info reg
(rv32) x/16xw msg
(rv32) si
(rv32) back
```

`help` lists the commands; an empty line repeats the last one, so Enter keeps
stepping.
