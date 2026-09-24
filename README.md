<p align="center">
  <img src="assets/microBee_Logo.svg" alt="microBee" width="220">
</p>

# microBee — EHB326E Lesson Contents (Student Package)

Course materials for MCU / uBee lab work (book chapter 2 block design + hello UART test).

## Contents

| Path | Purpose |
|------|---------|
| `IP/` | Vivado IP repository (`uBee_soc`). See [`IP/README.md`](IP/README.md). |
| `Compiled_Assembly_Files/` | `imem.hex` / `dmem.hex` for SoC BRAM init |
| `RTL_files/` | Hello UART testbench (`tb_design_1_hello.sv`) and helpers |
| `Homework_1/` | Chapter 2 homework notes |
| `Emulator/RV32_emulator_Student_Guide/` | Student usage guide (PDF/HTML) + examples |
| `Emulator/rv32_emulator-main/` | RV32IMC emulator, assembler, and debugger |
| `EHB326_Book_MCU_en.pdf` / `EHB326_Book_MCU_tr.pdf` | Course book (EN / TR) |
| `assets/` | Project logos |

## Quick start (Vivado IP)

1. Create / open your Vivado project (Arty A7).
2. **Tools → Settings → IP → Repository** → add the `IP` folder from this package.
3. Refresh IP Catalog → add **uBee SoC RV32IMC**.
4. Build the block design as in the book.
5. On `uBee_soc`, set **IMEM/DMEM Init File** to **absolute** paths of:
   - `Compiled_Assembly_Files/imem.hex`
   - `Compiled_Assembly_Files/dmem.hex`
6. Simulate with `RTL_files/tb_design_1_hello.sv` (top: `tb_design_1_hello`).

Expect UART text: `Hello from uBee on Arty A7` (9600 baud @ 100 MHz).

Do not move files out of `IP/uBee_soc/` without keeping relative layout; the IP is self-contained with relative source paths.

## Emulator

Start with [`Emulator/RV32_emulator_Student_Guide/`](Emulator/RV32_emulator_Student_Guide/) (`RV32 Student Usage Guide.pdf` or `.html`). Source and install notes are in [`Emulator/rv32_emulator-main/README.md`](Emulator/rv32_emulator-main/README.md).
