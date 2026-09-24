# Self-checking test: a machine described by a file, driven from the command
# line.
#
#       rv32 test tests/board/multiplier.s --devices tests/board/multiplier.toml
#
# Two things are under test and neither can be reached without --devices: the
# peripherals in the file, and the memory sizes beside them. Run without it the
# program faults on the first store -- there is nothing at 0xffff0060 in the
# machine a hart has by default, and no data memory past 16 KB.
#
# The peripheral is the IP homework's multiplier host. Its protocol is the part
# that matters:
#
#       write A, write B    the write is what starts it
#       poll Status         until it reads done -- reads only
#       read P low, P high  the product, latched, as two 8-bit halves
#
# and the rule that bites is that *any* write restarts the timing, which case 3
# checks by doing exactly what the homework warns against.

        .equ    MUL_A,       0xffff0060
        .equ    MUL_B,       0xffff0064
        .equ    MUL_P_LOW,   0xffff0068
        .equ    MUL_P_HIGH,  0xffff006c
        .equ    MUL_STATUS,  0xffff0070
        .equ    STATUS_IDLE, 0
        .equ    STATUS_BUSY, 1
        .equ    STATUS_DONE, 2

        .text
        .globl _start
_start:

        # ---- 1: the result is computed, not remembered ----------------------
        # A register file would hand back 12. An IP core hands back 144.
        li      gp, 1
        li      s0, 12
        li      t0, MUL_A
        sw      s0, 0(t0)
        li      t0, MUL_B
        sw      s0, 0(t0)               # this write starts the multiplication

        li      s2, 0                   # how many times the poll saw busy
poll:
        li      t0, MUL_STATUS
        lw      t1, 0(t0)
        li      t2, STATUS_BUSY
        bne     t1, t2, poll_done
        addi    s2, s2, 1
poll_done:
        li      t2, STATUS_DONE
        bne     t1, t2, poll            # reads only: a write would restart it

        li      t0, MUL_P_LOW
        lw      t2, 0(t0)
        li      t0, MUL_P_HIGH
        lw      t3, 0(t0)
        slli    t3, t3, 8
        or      t2, t2, t3              # 12 * 12, reassembled from two halves
        li      t3, 144
        bne     t2, t3, fail

        # ---- 2: the latency is real ----------------------------------------
        # A peripheral that answered immediately would pass case 1 and teach
        # nothing: the poll loop would never go round. It has to have been busy.
        li      gp, 2
        beqz    s2, fail

        # ---- 3: any write restarts it --------------------------------------
        # The homework's guideline, and the reason its poll loop only reads. A
        # write lands while the operation is in flight; it must not be done on
        # the very next instruction.
        li      gp, 3
        li      s0, 3
        li      t0, MUL_A
        sw      s0, 0(t0)
        li      t0, MUL_B
        sw      s0, 0(t0)
        li      t0, MUL_STATUS
        lw      t1, 0(t0)
        li      t2, STATUS_DONE
        beq     t1, t2, fail            # seven cycles have not passed yet

        li      t0, MUL_A               # the rewrite the guideline warns about
        sw      s0, 0(t0)
        li      t0, MUL_STATUS
        lw      t1, 0(t0)
        beq     t1, t2, fail            # the clock started again, so not done

settle:
        li      t0, MUL_STATUS          # left alone, it still finishes
        lw      t1, 0(t0)
        bne     t1, t2, settle
        li      t0, MUL_P_LOW
        lw      t2, 0(t0)
        li      t3, 9
        bne     t2, t3, fail

        # ---- 4: the memory sizes came from the file too ---------------------
        # 0x5000 is past the 16 KB a machine has by default. The file says 32
        # KB, so this is ordinary data memory -- and if the sizes had not
        # reached the run it would be a store access fault.
        li      gp, 4
        li      t0, 0x5000
        li      t1, 0x5a5a5a5a
        sw      t1, 0(t0)
        lw      t2, 0(t0)
        bne     t2, t1, fail

pass:
        li      a0, 0
        ebreak
fail:
        mv      a0, gp
        ebreak
