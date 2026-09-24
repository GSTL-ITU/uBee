# Self-check: assembled with examples/ubee.toml (or tests/board/ubee.toml),
# `la` must encode the SoC DMEM base and MMIO stores must hit 0x40004000.

        .text
        .globl _start
_start:
        la      t0, marker
        li      t1, 0x20000000
        bne     t0, t1, fail1

        # Write a byte to the native UART window and require ready status.
        li      t2, 0x40004000
        li      t3, 'U'
        sb      t3, 0(t2)
        lw      t4, 4(t2)
        andi    t4, t4, 1
        beqz    t4, fail2

        li      a0, 0
        ebreak

fail1:
        li      a0, 1
        ebreak
fail2:
        li      a0, 2
        ebreak

        .data
marker: .word   0x11111111
