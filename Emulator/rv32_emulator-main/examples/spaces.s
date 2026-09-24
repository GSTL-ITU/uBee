# The two address spaces, demonstrated.
#
# This machine is Harvard: instruction memory and data memory both start at
# address 0 and are completely different storage. That is the one thing about
# this emulator that surprises people, so here it is on purpose.
#
# Open the Sym panel (key 6) while stepping: `start` and `message` have almost
# the same numeric value and live in different spaces.

        .data
message:
        .asciz "same address, different memory\n"
counter:
        .word 0

        .text
        .globl _start
_start:
        # `la` loads an absolute address, expanding to lui + addi. It cannot
        # use auipc, because auipc computes an address relative to the program
        # counter -- that is, a position in *instruction* memory, where a data
        # symbol does not live.
        la      a0, message
        call    print_string

        # Address 0 in data memory is not the first instruction of this
        # program. Reading it gives zero, not an encoding.
        li      a0, 0
        lw      a1, 0(a0)       # a1 = 0

        # The assembler refuses to mix the spaces. Uncomment the next line to
        # see the error, which points at both the use and the definition:
        #
        #     call message
        #
        # error[E0310]: 'call' needs a code address, but this is a data symbol

        # Counting in data memory, so the Mem panel has something to show.
        la      a0, counter
        li      t0, 5
.loop:
        lw      t1, 0(a0)
        addi    t1, t1, 1
        sw      t1, 0(a0)
        addi    t0, t0, -1
        bnez    t0, .loop

        ebreak

# ---------------------------------------------------------------------------
print_string:
        li      t2, 0xffff0000
        mv      t1, a0
.next:
        lbu     t3, 0(t1)
        beqz    t3, .done
        sb      t3, 0(t2)
        addi    t1, t1, 1
        j       .next
.done:
        ret
