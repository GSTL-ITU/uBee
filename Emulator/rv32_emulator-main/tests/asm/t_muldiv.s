# Self-checking test: the M extension.
#
# Every case here is mandated by the spec and easy to get wrong. The division
# edge cases matter twice over, because computing INT32_MIN / -1 with the
# host's native operator would be undefined behaviour in the emulator itself.

        .text
        .globl _start
_start:

        # ---- 1: mul returns the low word ------------------------------------
        li      gp, 1
        li      t0, 6
        li      t1, 7
        mul     t2, t0, t1
        li      t3, 42
        bne     t2, t3, fail

        # ---- 2: the three high-half variants disagree -----------------------
        li      gp, 2
        li      t0, 0x80000000
        li      t1, 2
        mulh    t2, t0, t1              # signed x signed
        li      t3, -1
        bne     t2, t3, fail
        mulhu   t2, t0, t1              # unsigned x unsigned
        li      t3, 1
        bne     t2, t3, fail
        mulhsu  t2, t0, t1              # signed x unsigned
        li      t3, -1
        bne     t2, t3, fail

        # ---- 3: division by zero is defined, not a trap ---------------------
        li      gp, 3
        li      t0, 17
        li      t1, 0
        div     t2, t0, t1
        li      t3, -1                  # quotient is all ones
        bne     t2, t3, fail
        rem     t2, t0, t1
        li      t3, 17                  # remainder is the dividend
        bne     t2, t3, fail
        divu    t2, t0, t1
        li      t3, -1
        bne     t2, t3, fail

        # ---- 4: signed division overflow ------------------------------------
        li      gp, 4
        li      t0, 0x80000000
        li      t1, -1
        div     t2, t0, t1
        li      t3, 0x80000000          # INT32_MIN, no trap
        bne     t2, t3, fail
        rem     t2, t0, t1
        bnez    t2, fail                # remainder is zero

        # ---- 5: division truncates toward zero ------------------------------
        li      gp, 5
        li      t0, -7
        li      t1, 2
        div     t2, t0, t1
        li      t3, -3                  # not -4: truncation, not floor
        bne     t2, t3, fail
        rem     t2, t0, t1
        li      t3, -1                  # the remainder takes the dividend's sign
        bne     t2, t3, fail

        # ---- 6: unsigned division treats operands as unsigned ---------------
        li      gp, 6
        li      t0, -1                  # 0xffffffff
        li      t1, 2
        divu    t2, t0, t1
        li      t3, 0x7fffffff
        bne     t2, t3, fail
        div     t2, t0, t1              # signed: -1 / 2 is 0
        bnez    t2, fail

pass:
        li      a0, 0
        ebreak
fail:
        mv      a0, gp
        ebreak
