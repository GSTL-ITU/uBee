# Fibonacci: compute fib(n) iteratively and print it over the UART.
#
# Shows the shape of a whole program on this machine: a .data section for
# constants and buffers, a .text section for code, and MMIO for output.

        .equ  UART_BASE, 0xffff0000
        .equ  N,         10

        .data
result: .word 0                 # where the answer is stored
label:  .asciz "fib = "

        .text
        .globl _start
_start:
        li      s0, N           # how many terms to compute
        li      t0, 0           # a = fib(0)
        li      t1, 1           # b = fib(1)
        li      t2, 0           # loop counter

loop:
        bge     t2, s0, done    # while counter < n
        add     t3, t0, t1      # next = a + b
        mv      t0, t1          # a = b
        mv      t1, t3          # b = next
        addi    t2, t2, 1
        j       loop

done:
        # Store the result in data memory. `la` loads an absolute address,
        # because instruction and data memory are separate spaces here.
        la      a0, result
        sw      t0, 0(a0)

        # Print the label, then the number.
        la      a0, label
        call    print_string
        mv      a0, t0
        call    print_decimal

        li      a0, '\n'
        call    print_char
        ebreak

# ---------------------------------------------------------------------------
# print_char: write the byte in a0 to the UART.
# ---------------------------------------------------------------------------
print_char:
        li      t0, UART_BASE
        sb      a0, 0(t0)
        ret

# ---------------------------------------------------------------------------
# print_string: write the NUL-terminated string at a0.
# ---------------------------------------------------------------------------
print_string:
        mv      t1, a0
        li      t2, UART_BASE
.print_loop:
        lbu     t3, 0(t1)
        beqz    t3, .print_done
        sb      t3, 0(t2)
        addi    t1, t1, 1
        j       .print_loop
.print_done:
        ret

# ---------------------------------------------------------------------------
# print_decimal: write the unsigned value in a0 as decimal digits.
#
# Digits come out least significant first, so they are pushed onto the stack
# and popped back off to reverse them.
# ---------------------------------------------------------------------------
print_decimal:
        mv      t0, a0          # value
        li      t1, 10
        li      t4, 0           # digit count
        la      a1, digit_buf   # write cursor

        beqz    t0, .zero_case
.divide:
        remu    t2, t0, t1      # t2 = value % 10
        divu    t0, t0, t1      # value /= 10
        addi    t2, t2, '0'     # to ASCII
        sb      t2, 0(a1)
        addi    a1, a1, 1
        addi    t4, t4, 1
        bnez    t0, .divide
        j       .emit

.zero_case:
        li      t2, '0'
        sb      t2, 0(a1)
        addi    a1, a1, 1
        li      t4, 1

.emit:
        li      t5, UART_BASE
.emit_loop:
        beqz    t4, .emit_done
        addi    a1, a1, -1      # walk back down, reversing the digits
        lbu     t2, 0(a1)
        sb      t2, 0(t5)
        addi    t4, t4, -1
        j       .emit_loop
.emit_done:
        ret

        .data
digit_buf:
        .space  16
