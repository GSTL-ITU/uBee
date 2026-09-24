# Print A and a newline on the default machine
        .text
        .globl _start
_start:
        li t0, 0xffff0000
        li a0, 65
        sb a0, 0(t0)
        li a0, 10
        sb a0, 0(t0)
        ebreak
