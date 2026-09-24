# First lab: add, store, and load
        .data
result: .word 0
        .text
        .globl _start
_start:
        li a0, 5
        li a1, 3
        add a2, a0, a1
        la t0, result
        sw a2, 0(t0)
        lw a3, 0(t0)
        ebreak
