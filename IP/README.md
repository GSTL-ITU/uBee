# uBee SoC IP (`uBee_soc`)

Vivado custom IP: RV32IMC core + on-chip IMEM/DMEM + CLINT/PLIC + AXI4-Lite master.

VLNV: `uBee.org:user:uBee_soc:1.0`

## Add the IP repository

1. Open your Vivado project (or create one for Arty A7-35/100).
2. **Tools → Settings → IP → Repository**
3. Add this folder: the parent of `uBee_soc` (i.e. the `IP` directory next to this README).
4. Click **OK**, then **IP Catalog → Refresh**.
5. Search for **uBee SoC** / `uBee_soc` under **/UserIP**.

TCL equivalent:

```tcl
set_property ip_repo_paths [list [file normalize "<path-to>/IP"]] [current_project]
update_ip_catalog -rebuild
```

## Memory init files (important)

`IMEM_INIT_FILE` and `DMEM_INIT_FILE` are passed to `$readmemh`. Vivado resolves these paths relative to its **current working directory**, not relative to this IP folder.

**Recommended:** use absolute paths when customizing the IP, for example:

- `.../Compiled_Assembly_Files/imem.hex`
- `.../Compiled_Assembly_Files/dmem.hex`

(Those hex files ship next to this `IP` folder in the lesson package.)

Leaving the parameters empty leaves BRAM cleared (zeros) — useful until you attach a program image.

## Hello UART simulation

After building the block design from the book:

1. Set init files as above on the `uBee_soc` instance.
2. Add `RTL_files/tb_design_1_hello.sv` as simulation source.
3. Set simulation top to `tb_design_1_hello`.
4. Expect UART text: `Hello from uBee on Arty A7` (9600 baud @ 100 MHz).

## Supported device family

Packaged for **Artix-7** (Arty A7). Other families may require editing the IP’s supported families list or re-packaging.
