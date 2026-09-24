# Self-checking test: branches, jumps, calls and the link register.

        .text
        .globl _start
_start:

        # ---- 1: beq taken and not taken -------------------------------------
        li      gp, 1
        li      t0, 5
        li      t1, 5
        beq     t0, t1, .equal
        j       fail
.equal:
        li      t1, 6
        beq     t0, t1, fail            # must not be taken

        # ---- 2: signed and unsigned comparisons differ ----------------------
        li      gp, 2
        li      t0, -1
        li      t1, 1
        blt     t0, t1, .signed_ok      # signed: -1 < 1
        j       fail
.signed_ok:
        bltu    t0, t1, fail            # unsigned: 0xffffffff < 1 is false

        # ---- 3: a backward branch forms a loop ------------------------------
        # A wrong sign bit in the B-type immediate turns this into an infinite
        # loop or a wild jump, so reaching the end at all is most of the test.
        li      gp, 3
        li      t0, 3                   # counter
        li      t1, 0                   # accumulator
.loop:
        add     t1, t1, t0
        addi    t0, t0, -1
        bnez    t0, .loop
        li      t2, 6                   # 3 + 2 + 1
        bne     t1, t2, fail

        # ---- 4: jal links to the following instruction ----------------------
        li      gp, 4
        jal     ra, .after
        j       fail                    # skipped
.after:
        la      t0, .after
        # ra should point at the instruction after the jal, which is 4 bytes
        # before .after.
        addi    t0, t0, -4
        bne     ra, t0, fail

        # ---- 5: jalr clears the low bit of its target -----------------------
        li      gp, 5
        la      t0, .landing
        addi    t0, t0, 1               # deliberately odd
        jalr    zero, t0, -1            # (target + 1 - 1) & ~1
        j       fail
.landing:

        # ---- 6: call and ret ------------------------------------------------
        li      gp, 6
        li      a1, 20
        call    triple
        li      t0, 60
        bne     a1, t0, fail

        # ---- 7: nested calls need ra saved ----------------------------------
        li      gp, 7
        li      a1, 2
        call    outer
        li      t0, 18                  # (2 * 3) * 3
        bne     a1, t0, fail

        # ---- 8: pseudo-branches against zero --------------------------------
        li      gp, 8
        li      t0, 0
        beqz    t0, .z1
        j       fail
.z1:    bnez    t0, fail
        li      t0, -5
        bltz    t0, .z2
        j       fail
.z2:    bgtz    t0, fail
        blez    t0, .z3
        j       fail
.z3:

        # ---- 9: bgt and ble swap their operands -----------------------------
        li      gp, 9
        li      t0, 7
        li      t1, 3
        bgt     t0, t1, .g1
        j       fail
.g1:    ble     t0, t1, fail

pass:
        li      a0, 0
        ebreak
fail:
        mv      a0, gp
        ebreak

# ---------------------------------------------------------------------------
triple:
        li      t3, 3
        mul     a1, a1, t3
        ret

outer:
        mv      s1, ra                  # save the return address across a call
        call    triple
        call    triple
        mv      ra, s1
        ret
