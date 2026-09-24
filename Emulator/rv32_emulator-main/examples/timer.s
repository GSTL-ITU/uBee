# A timer interrupt, from scratch.
#
# The main loop counts as fast as it can. Every 200 cycles a timer interrupt
# pre-empts it, prints a dot, and rearms. After eight ticks the handler stops
# rearming and the program finishes.
#
# Step through this with F8 and watch the pc jump into `trap_handler` in the
# middle of the loop -- an interrupt is the one thing in this machine that
# moves the pc somewhere the source does not say it will go.
#
# Time here is the cycle counter, so this is completely reproducible: the same
# interrupt lands on the same cycle every run, and F6 steps back over it.

        .equ  UART_BASE,   0xffff0000
        .equ  MTIME,       0xffff0030   # the cycle counter
        .equ  MTIMECMP,    0xffff0040   # a timer interrupt fires while mtime >= this
        .equ  MIE_MTIE,    0x080        # mie bit 7:  machine timer
        .equ  MSTATUS_MIE, 0x008        # mstatus bit 3: interrupts enabled at all
        .equ  INTERVAL,    200
        .equ  TICKS,       8

        .data
count:  .word 0                         # how many ticks have fired
save:   .space 16                       # where the handler parks the registers
                                        # it clobbers: t0, t1, t2, ra

        .text
        .globl _start
_start:
        # Point mtvec at the handler. Direct mode: every trap and interrupt
        # lands here, so the handler has to work out which it was.
        la      t0, trap_handler
        csrw    mtvec, t0

        # Arm the timer for INTERVAL cycles from now.
        call    rearm

        # Unmask the timer line, then enable interrupts globally. Two separate
        # switches: mie chooses which lines can fire, mstatus.MIE whether any
        # can.
        li      t0, MIE_MTIE
        csrs    mie, t0
        li      t0, MSTATUS_MIE
        csrs    mstatus, t0

        # Now just count. Nothing here mentions the handler; the interrupt
        # arrives on its own.
        li      s0, 0
loop:
        addi    s0, s0, 1
        la      t0, count
        lw      t1, 0(t0)
        li      t2, TICKS
        blt     t1, t2, loop            # until the handler has fired TICKS times

        li      a0, '\n'
        call    print_char
        ebreak

# ---------------------------------------------------------------------------
# rearm: set mtimecmp to mtime + INTERVAL.
# ---------------------------------------------------------------------------
rearm:
        li      t0, MTIME
        lw      t1, 0(t0)               # t1 = now
        li      t2, INTERVAL
        add     t1, t1, t2
        li      t0, MTIMECMP
        sw      t1, 0(t0)               # one store: no half-written value
        ret

# ---------------------------------------------------------------------------
# trap_handler
#
# mcause bit 31 distinguishes an interrupt from an exception. mepc points at
# the instruction that has *not* run yet, so mret resumes it -- unlike an ecall
# handler, this one must not advance mepc or it would drop an instruction.
#
# Every register the handler touches is saved and restored. This is not
# optional politeness: an interrupt arrives *between* two instructions of the
# main loop, and the loop is holding live values in t0, t1 and t2 at that
# moment. Without the save, the handler leaves `now + INTERVAL` in t1, the
# loop's next `blt` compares that instead of the count, and the program exits
# after one tick. The bug only appears when the interrupt happens to land in
# the wrong place, which is exactly what makes it worth showing.
# ---------------------------------------------------------------------------
trap_handler:
        # Save first. A fixed area rather than a stack: nothing nests here, and
        # sp may not even be set up. mscratch buys the one free register needed
        # to get started.
        csrw    mscratch, t0
        la      t0, save
        sw      t1, 4(t0)
        sw      t2, 8(t0)
        sw      ra, 12(t0)
        csrr    t1, mscratch            # the caller's t0
        sw      t1, 0(t0)

        csrr    t0, mcause
        bgez    t0, .not_an_interrupt   # bit 31 clear means an exception

        # Count the tick.
        la      t0, count
        lw      t1, 0(t0)
        addi    t1, t1, 1
        sw      t1, 0(t0)

        # Show it.
        li      t0, UART_BASE
        li      t2, '.'
        sb      t2, 0(t0)

        # Acknowledge and decide whether to keep going. Writing mtimecmp is
        # what clears the pending bit: leaving it alone would re-enter the
        # handler immediately and forever.
        li      t2, TICKS
        blt     t1, t2, .rearm_and_return

        li      t0, MTIMECMP
        li      t1, -1                  # disarmed
        sw      t1, 0(t0)
        j       .restore

.rearm_and_return:
        call    rearm

.restore:
        la      t0, save
        lw      t1, 4(t0)
        lw      t2, 8(t0)
        lw      ra, 12(t0)
        lw      t0, 0(t0)
        mret

.not_an_interrupt:
        # An exception reached the same vector. Print '!' and stop, rather than
        # returning into whatever faulted and looping forever.
        li      t0, UART_BASE
        li      t2, '!'
        sb      t2, 0(t0)
        ebreak

# ---------------------------------------------------------------------------
print_char:
        li      t0, UART_BASE
        sb      a0, 0(t0)
        ret
