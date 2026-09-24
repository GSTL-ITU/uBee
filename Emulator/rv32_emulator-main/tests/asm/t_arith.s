# Self-checking test: integer arithmetic, logic and shifts.
#
# Convention (riscv-tests style): gp holds the number of the check in progress.
# On success a0 is 0; on failure a0 holds the number of the check that failed.
# Either way the program stops at ebreak.

        .text
        .globl _start
_start:

        # ---- 1: addi and add ------------------------------------------------
        li      gp, 1
        li      t0, 5
        li      t1, 3
        add     t2, t0, t1
        li      t3, 8
        bne     t2, t3, fail

        # ---- 2: sub ---------------------------------------------------------
        li      gp, 2
        sub     t2, t0, t1
        li      t3, 2
        bne     t2, t3, fail

        # ---- 3: x0 stays zero -----------------------------------------------
        li      gp, 3
        addi    x0, x0, 42
        bnez    x0, fail

        # ---- 4: addition wraps rather than trapping -------------------------
        li      gp, 4
        li      t0, 0x7fffffff
        addi    t0, t0, 1
        li      t1, 0x80000000
        bne     t0, t1, fail

        # ---- 5: slt is signed, sltu is not ----------------------------------
        li      gp, 5
        li      t0, -1
        li      t1, 1
        slt     t2, t0, t1              # -1 < 1 -> 1
        li      t3, 1
        bne     t2, t3, fail
        sltu    t2, t0, t1              # 0xffffffff < 1 -> 0
        bnez    t2, fail

        # ---- 6: sltiu sign-extends its immediate, then compares unsigned ----
        li      gp, 6
        li      t0, 5
        sltiu   t2, t0, -1              # 5 < 0xffffffff -> 1
        li      t3, 1
        bne     t2, t3, fail

        # ---- 7: logic -------------------------------------------------------
        li      gp, 7
        li      t0, 0xf0f0
        li      t1, 0x0ff0
        and     t2, t0, t1
        li      t3, 0x00f0
        bne     t2, t3, fail
        or      t2, t0, t1
        li      t3, 0xfff0
        bne     t2, t3, fail
        xor     t2, t0, t1
        li      t3, 0xff00
        bne     t2, t3, fail

        # ---- 8: srl is logical, sra is arithmetic ---------------------------
        li      gp, 8
        li      t0, -8                  # 0xfffffff8
        srli    t2, t0, 1
        li      t3, 0x7ffffffc
        bne     t2, t3, fail
        srai    t2, t0, 1
        li      t3, 0xfffffffc
        bne     t2, t3, fail

        # ---- 9: shift amounts use only the low 5 bits -----------------------
        li      gp, 9
        li      t0, 1
        li      t1, 33                  # low 5 bits are 1
        sll     t2, t0, t1
        li      t3, 2
        bne     t2, t3, fail

        # ---- 10: lui and auipc ----------------------------------------------
        li      gp, 10
        lui     t0, 0x12345
        li      t1, 0x12345000
        bne     t0, t1, fail
here:   auipc   t0, 0                   # auipc uses its own address
        la      t1, here
        bne     t0, t1, fail

pass:
        li      a0, 0
        ebreak
fail:
        mv      a0, gp
        ebreak
