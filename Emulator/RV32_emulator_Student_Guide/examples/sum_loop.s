# Sum 1 through 5
        .text
        .globl _start
_start:
        li t0, 1
        li t1, 6
        li a0, 0
loop:
        add a0, a0, t0
        addi t0, t0, 1
        blt t0, t1, loop
done:
        ebreak
