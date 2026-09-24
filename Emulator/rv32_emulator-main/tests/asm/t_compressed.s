# Self-checking test: the C extension.
#
# Every compressed instruction is an alias for a base one, so the interesting
# failures are not "does c.add add" -- expand() would have to be wrong in a very
# specific way for that -- but the things that only go wrong once instructions
# stop being four bytes wide: the pc advancing by 2, a 32-bit instruction
# landing on an odd word offset, and a link register pointing two bytes back
# instead of four.

        .text
        .globl _start
_start:

        # ---- 1: the pc advances by two --------------------------------------
        # Four compressed instructions in a row. If the pc advanced by 4 the
        # second, fourth and sixth would be skipped and a0 would be wrong.
        li      gp, 1
        c.li    a0, 1
        c.li    a1, 2
        c.add   a0, a1                  # 3
        c.mv    a2, a0                  # 3
        c.add   a0, a2                  # 6
        li      t0, 6
        bne     a0, t0, fail

        # ---- 2: mixing widths, so 32-bit code lands off a word boundary -----
        # After an odd number of compressed instructions the cursor is 2 mod 4,
        # so this addi straddles two words. Fetching it requires two halfword
        # reads; a word-at-a-time fetch would read the wrong bits entirely.
        li      gp, 2
        c.li    t1, 1                   # cursor now 2 mod 4
        addi    t2, zero, 0x123         # straddles a word boundary
        li      t0, 0x123
        bne     t2, t0, fail
        c.addi  t2, -1
        addi    t0, t0, -1
        bne     t2, t0, fail

        # ---- 3: compressed arithmetic and the x8-x15 forms ------------------
        li      gp, 3
        li      a0, 0x0f0
        li      a1, 0x0ff
        c.and   a0, a1                  # 0x0f0
        li      t0, 0x0f0
        bne     a0, t0, fail

        li      a0, 0x0f0
        c.xor   a0, a1                  # 0x00f
        li      t0, 0x00f
        bne     a0, t0, fail

        li      a0, 0x0f0
        c.or    a0, a1                  # 0x0ff
        li      t0, 0x0ff
        bne     a0, t0, fail

        li      a0, 10
        li      a1, 3
        c.sub   a0, a1                  # 7
        li      t0, 7
        bne     a0, t0, fail

        # ---- 4: compressed shifts and the andi immediate --------------------
        li      gp, 4
        li      a0, 1
        c.slli  a0, 8                   # 0x100
        li      t0, 0x100
        bne     a0, t0, fail
        c.srli  a0, 4                   # 0x010
        li      t0, 0x010
        bne     a0, t0, fail

        li      a0, -16
        c.srai  a0, 2                   # -4, arithmetic
        li      t0, -4
        bne     a0, t0, fail

        li      a0, 0x3f
        c.andi  a0, 0x0f                # 0x0f
        li      t0, 0x0f
        bne     a0, t0, fail

        # c.andi sign-extends its 6-bit immediate, so -1 must be a no-op rather
        # than a mask with 0x3f.
        li      a0, 0x7ff
        c.andi  a0, -1
        li      t0, 0x7ff
        bne     a0, t0, fail

        # ---- 5: c.li and c.lui build values ---------------------------------
        li      gp, 5
        c.li    a0, -1
        li      t0, -1
        bne     a0, t0, fail

        c.lui   a0, 0x10                # 0x10000
        li      t0, 0x10000
        bne     a0, t0, fail

        # ---- 6: sp-relative loads and stores --------------------------------
        # c.swsp/c.lwsp and c.sw/c.lw put the same word offset in three
        # different bit layouts, so this is really a test of the scatters.
        li      gp, 6
        li      sp, 0x200
        li      a0, 0x12345678
        c.swsp  a0, 4(sp)
        c.lwsp  a1, 4(sp)
        bne     a0, a1, fail

        li      a2, 0x200               # a base in x8-x15
        li      a3, 0x0abcdef0
        c.sw    a3, 8(a2)
        c.lw    a4, 8(a2)
        bne     a3, a4, fail

        # The two views must agree: what c.swsp wrote at sp+4 is what c.lw sees
        # at a2+4.
        c.lw    a5, 4(a2)
        bne     a0, a5, fail

        # ---- 7: c.addi4spn and c.addi16sp -----------------------------------
        li      gp, 7
        li      sp, 0x300
        c.addi4spn a0, 8                # a0 = sp + 8
        li      t0, 0x308
        bne     a0, t0, fail

        c.addi16sp 32                   # sp += 32
        li      t0, 0x320
        bne     sp, t0, fail
        c.addi16sp -32                  # and back
        li      t0, 0x300
        bne     sp, t0, fail

        # ---- 8: compressed branches -----------------------------------------
        li      gp, 8
        li      a0, 0
        c.beqz  a0, .zero_ok
        j       fail
.zero_ok:
        li      a0, 1
        c.beqz  a0, fail                # must not be taken

        c.bnez  a0, .nonzero_ok
        j       fail
.nonzero_ok:

        # A backward compressed branch, which is where a wrong sign bit in the
        # 9-bit CB immediate shows up as an infinite loop.
        li      a0, 3
.loop:
        c.addi  a0, -1
        c.bnez  a0, .loop
        bne     a0, zero, fail

        # ---- 9: c.j jumps forward and backward ------------------------------
        li      gp, 9
        c.j     .forward
        j       fail
.backward:
        li      s0, 1
        c.j     .after_backward
.forward:
        c.j     .backward
.after_backward:
        li      t0, 1
        bne     s0, t0, fail

        # ---- 10: c.jal links to ra, and it links pc+2 -----------------------
        # The link register is the whole point: c.jal is two bytes, so ra must
        # be two past the call and not four. Getting that wrong returns into
        # the middle of the next instruction.
        li      gp, 10
        li      a1, 5
        c.jal   triple
.after_call:
        li      t0, 15
        bne     a1, t0, fail

        la      t0, .after_call
        bne     ra, t0, fail            # ra points *at* the next instruction

        # ---- 11: c.jr and c.jalr --------------------------------------------
        li      gp, 11
        la      t0, .landed
        c.jr    t0
        j       fail
.landed:

        li      gp, 11
        li      a1, 2
        la      t0, triple
        c.jalr  t0
.after_jalr:
        li      t0, 6
        bne     a1, t0, fail
        la      t0, .after_jalr
        bne     ra, t0, fail

        # ---- 12: c.nop retires and changes nothing --------------------------
        li      gp, 12
        li      a0, 0x55
        c.nop
        c.nop
        li      t0, 0x55
        bne     a0, t0, fail

pass:
        li      a0, 0
        ebreak
fail:
        mv      a0, gp
        ebreak

# ---------------------------------------------------------------------------
# Called with c.jal and with c.jalr, so it must return through ra either way.
triple:
        li      t3, 3
        mul     a1, a1, t3
        c.jr    ra
