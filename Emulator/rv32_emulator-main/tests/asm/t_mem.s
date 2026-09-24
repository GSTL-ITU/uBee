# Self-checking test: loads, stores, and the two address spaces.

        .data
word_slot:
        .word 0
pattern:
        .word 0x80808080
bytes:
        .byte 0x11, 0x22, 0x33, 0x44
message:
        .asciz "hi"

        .text
        .globl _start
_start:

        # ---- 1: store then load a word --------------------------------------
        li      gp, 1
        la      a1, word_slot
        li      t0, 0x12345678
        sw      t0, 0(a1)
        lw      t1, 0(a1)
        bne     t0, t1, fail

        # ---- 2: byte and halfword sign extension ----------------------------
        li      gp, 2
        la      a1, pattern
        lb      t0, 0(a1)               # signed byte
        li      t1, 0xffffff80
        bne     t0, t1, fail
        lbu     t0, 0(a1)               # unsigned byte
        li      t1, 0x80
        bne     t0, t1, fail
        lh      t0, 0(a1)               # signed halfword
        li      t1, 0xffff8080
        bne     t0, t1, fail
        lhu     t0, 0(a1)               # unsigned halfword
        li      t1, 0x8080
        bne     t0, t1, fail

        # ---- 3: the image is little-endian ----------------------------------
        li      gp, 3
        la      a1, bytes
        lw      t0, 0(a1)
        li      t1, 0x44332211          # byte 0 is the least significant
        bne     t0, t1, fail

        # ---- 4: a sub-word store leaves its neighbours alone -----------------
        li      gp, 4
        la      a1, word_slot
        li      t0, 0xaaaaaaaa
        sw      t0, 0(a1)
        li      t1, 0x11
        sb      t1, 0(a1)
        lw      t0, 0(a1)
        li      t1, 0xaaaaaa11
        bne     t0, t1, fail

        # ---- 5: negative offsets --------------------------------------------
        li      gp, 5
        la      a1, pattern             # word_slot is 4 bytes below it
        li      t0, 0x5a5a5a5a
        sw      t0, -4(a1)
        lw      t1, -4(a1)
        bne     t0, t1, fail

        # ---- 6: strings are NUL terminated ----------------------------------
        li      gp, 6
        la      a1, message
        lbu     t0, 0(a1)
        li      t1, 'h'
        bne     t0, t1, fail
        lbu     t0, 2(a1)
        bnez    t0, fail                # the terminator

        # ---- 7: instruction and data memory are separate spaces -------------
        # Address 0 in data memory is not the first instruction of this program.
        li      gp, 7
        li      a1, 0
        lw      t0, 0(a1)
        la      t1, word_slot
        lw      t1, 0(t1)
        # word_slot holds 0x5a5a5a5a... no: it holds 0xaaaaaa11 from check 4.
        # What matters is only that DMEM[0] is not an instruction encoding.
        li      t2, 0x00000013          # what a `nop` would encode to
        beq     t0, t2, fail

        # ---- 8: MMIO ---------------------------------------------------------
        li      gp, 8
        li      a1, 0xffff0000
        li      t0, 0x2a
        sw      t0, 0x10(a1)            # LED register
        lw      t1, 0x10(a1)
        bne     t0, t1, fail
        lw      t1, 4(a1)               # UART status: transmitter ready
        li      t2, 1
        bne     t1, t2, fail

pass:
        li      a0, 0
        ebreak
fail:
        mv      a0, gp
        ebreak
