# EHB326E Lesson Contents (Student Package)

Course materials for MCU / uBee lab work (book chapter 2 block design + hello test).

## Contents

| Path | Purpose |
|------|---------|
| `IP/` | Vivado IP repository (`uBee_soc`). See `IP/README.md`. |
| `Compiled_Assembly_Files/` | `imem.hex` / `dmem.hex` for SoC BRAM init |
| `RTL_files/` | Hello UART testbench (`tb_design_1_hello.sv`) and helpers |
| `Homework_1/` | Homework notes |
| `Emulator/` | RV32 emulator + student guide |
| `EHB326_Book_MCU_en.pdf` / `EHB326_Book_MCU_tr.pdf` | Course book |

## Quick start (Vivado IP)

1. Create / open your Vivado project (Arty A7).
2. **Tools → Settings → IP → Repository** → add the `IP` folder from this package.
3. Refresh IP Catalog → add **uBee SoC RV32IMC**.
4. Build the block design as in the book.
5. On `uBee_soc`, set **IMEM/DMEM Init File** to **absolute** paths of:
   - `Compiled_Assembly_Files/imem.hex`
   - `Compiled_Assembly_Files/dmem.hex`
6. Simulate with `RTL_files/tb_design_1_hello.sv` (top: `tb_design_1_hello`).

Do not move files out of `IP/uBee_soc/` without keeping relative layout; the IP is self-contained with relative source paths.
