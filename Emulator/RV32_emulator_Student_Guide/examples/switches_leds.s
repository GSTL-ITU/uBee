# Read switches once and copy them to LEDs
        .text
        .globl _start
_start:
        li t0, 0xffff0020
        lw a0, 0(t0)
        li t1, 0xffff0010
        sw a0, 0(t1)
        ebreak
