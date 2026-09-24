# Euclid's algorithm, iteratively.
#
# A small program to step through. Put a breakpoint on the loop and watch a0
# and a1 swap places -- the register panel highlights whichever one just
# changed, so the shape of the algorithm is visible without reading it.

        .equ  UART_BASE, 0xffff0000

        .text
        .globl _start
_start:
        li      a0, 1071
        li      a1, 462
        call    gcd
        # a0 now holds the answer: 21
        call    print_decimal
        li      a0, '\n'
        call    print_char
        ebreak

# ---------------------------------------------------------------------------
# gcd: a0 = gcd(a0, a1), by repeated remainder.
# ---------------------------------------------------------------------------
gcd:
        beqz    a1, .done       # gcd(a, 0) = a
        remu    t0, a0, a1      # t0 = a % b
        mv      a0, a1          # a = b
        mv      a1, t0          # b = t0
        j       gcd
.done:
        ret

# ---------------------------------------------------------------------------
print_char:
        li      t0, UART_BASE
        sb      a0, 0(t0)
        ret

# ---------------------------------------------------------------------------
# print_decimal: write the unsigned value in a0 as decimal digits.
#
# Digits come out least significant first, so they are stored and walked back.
# ---------------------------------------------------------------------------
print_decimal:
        mv      t0, a0
        li      t1, 10
        li      t4, 0
        la      a1, digits

        beqz    t0, .zero
.divide:
        remu    t2, t0, t1
        divu    t0, t0, t1
        addi    t2, t2, '0'
        sb      t2, 0(a1)
        addi    a1, a1, 1
        addi    t4, t4, 1
        bnez    t0, .divide
        j       .emit
.zero:
        li      t2, '0'
        sb      t2, 0(a1)
        addi    a1, a1, 1
        li      t4, 1
.emit:
        li      t5, UART_BASE
.emit_loop:
        beqz    t4, .emit_done
        addi    a1, a1, -1
        lbu     t2, 0(a1)
        sb      t2, 0(t5)
        addi    t4, t4, -1
        j       .emit_loop
.emit_done:
        ret

        .data
digits: .space 16
