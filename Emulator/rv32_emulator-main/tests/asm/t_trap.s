# Self-checking test: exceptions, mret, and interrupts.
#
# Written in assembly on purpose. It exercises the assembler, the encoder, the
# decoder, the executor, the CSR file and the interrupt path in one run, which
# no C++ unit test does.

        .equ  MTIME,       0xffff0030
        .equ  MTIMECMP,    0xffff0040
        .equ  MSIP,        0xffff0050
        .equ  MIE_MSIE,    0x008         # mie bit 3:  software
        .equ  MIE_MTIE,    0x080         # mie bit 7:  timer
        .equ  MSTATUS_MIE, 0x008

        .data
saved:  .space 32
flag:   .word 0

        .text
        .globl _start
_start:
        la      t0, handler
        csrw    mtvec, t0

        # ---- 1: an ecall reaches the handler and mret comes back ------------
        li      gp, 1
        li      s0, 0                   # s0 counts handler entries
        ecall
        li      t0, 1
        bne     s0, t0, fail            # the handler ran exactly once

        # ---- 2: mcause names the cause, with bit 31 clear -------------------
        li      gp, 2
        li      t0, 11                  # environment call from M-mode
        bne     s1, t0, fail            # s1 = mcause as the handler saw it

        # ---- 3: an exception handler must advance mepc itself ---------------
        # If it did not, the ecall above would have re-executed forever and we
        # would never have got here. Reaching this line is the check.
        li      gp, 3

        # ---- 4: a software interrupt fires when unmasked --------------------
        li      gp, 4
        li      s0, 0
        li      t0, MIE_MSIE
        csrs    mie, t0
        li      t0, MSTATUS_MIE
        csrs    mstatus, t0

        li      s2, 0
        li      t0, MSIP
        li      t1, 1
        sw      t1, 0(t0)               # raise the request

        # The interrupt lands somewhere in this run of increments. Counting
        # rather than checking one instruction makes the test independent of
        # exactly where it lands -- and it is precisely what would fail if mret
        # skipped the instruction it displaced.
        addi    s2, s2, 1
        addi    s2, s2, 1
        addi    s2, s2, 1
        addi    s2, s2, 1
        addi    s2, s2, 1

        li      t0, 1
        bne     s0, t0, fail

        # ---- 5: mcause marks it as an interrupt -----------------------------
        li      gp, 5
        li      t0, 3                   # machine software interrupt
        li      t1, 0x80000000
        or      t0, t0, t1              # bit 31 set
        bne     s1, t0, fail

        # ---- 6: no instruction is lost to the interrupt ---------------------
        # mepc points at the instruction that has *not* run, so mret resumes it.
        # If it skipped instead, one of the five increments above would be
        # missing.
        li      gp, 6
        li      t0, 5
        bne     s2, t0, fail

        # ---- 7: a timer interrupt fires when mtime reaches mtimecmp ---------
        li      gp, 7
        li      s0, 0
        li      t0, MIE_MTIE
        csrs    mie, t0

        li      t0, MTIME
        lw      t1, 0(t0)
        addi    t1, t1, 20
        li      t0, MTIMECMP
        sw      t1, 0(t0)               # arm for 20 cycles from now

.wait:  addi    s3, s3, 1               # spin until it lands
        li      t0, 100
        blt     s3, t0, .wait2
        j       fail                    # it never fired
.wait2: beqz    s0, .wait

        li      t0, 1
        bne     s0, t0, fail

        # ---- 8: masking stops it ---------------------------------------------
        li      gp, 8
        csrw    mie, zero               # mask every line
        li      t0, MSIP
        li      t1, 1
        sw      t1, 0(t0)               # raise software again
        li      s0, 0
        nop
        nop
        nop
        bnez    s0, fail                # nothing fired

pass:
        li      a0, 0
        ebreak
fail:
        mv      a0, gp
        ebreak

# ---------------------------------------------------------------------------
# handler: counts entries in s0, records mcause in s1, and knows the difference
# between an exception (advance mepc) and an interrupt (do not).
# ---------------------------------------------------------------------------
handler:
        csrw    mscratch, t0
        la      t0, saved
        sw      t1, 4(t0)
        sw      t2, 8(t0)
        csrr    t1, mscratch
        sw      t1, 0(t0)

        addi    s0, s0, 1
        csrr    s1, mcause

        bgez    s1, .exception          # bit 31 clear: an exception

        # An interrupt. Acknowledge whichever line it was, so it stops
        # asserting; leaving it set would re-enter here immediately.
        li      t0, MSIP
        sw      zero, 0(t0)
        li      t0, MTIMECMP
        li      t1, -1
        sw      t1, 0(t0)
        j       .restore

.exception:
        # mepc points at the faulting instruction, so it must be stepped past
        # or it will fault again forever.
        csrr    t1, mepc
        addi    t1, t1, 4
        csrw    mepc, t1

.restore:
        la      t0, saved
        lw      t1, 4(t0)
        lw      t2, 8(t0)
        lw      t0, 0(t0)
        mret
