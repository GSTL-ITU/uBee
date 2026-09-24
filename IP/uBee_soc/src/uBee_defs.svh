`ifndef UBEE_DEFS_SVH
`define UBEE_DEFS_SVH

`define UBEE_ALU_ADD     5'd0
`define UBEE_ALU_SUB     5'd1
`define UBEE_ALU_SLL     5'd2
`define UBEE_ALU_SLT     5'd3
`define UBEE_ALU_SLTU    5'd4
`define UBEE_ALU_XOR     5'd5
`define UBEE_ALU_SRL     5'd6
`define UBEE_ALU_SRA     5'd7
`define UBEE_ALU_OR      5'd8
`define UBEE_ALU_AND     5'd9
`define UBEE_ALU_PASS_A  5'd10
`define UBEE_ALU_PASS_B  5'd11
// M extension
`define UBEE_ALU_MUL     5'd12
`define UBEE_ALU_MULH    5'd13
`define UBEE_ALU_MULHSU  5'd14
`define UBEE_ALU_MULHU   5'd15
`define UBEE_ALU_DIV     5'd16
`define UBEE_ALU_DIVU    5'd17
`define UBEE_ALU_REM     5'd18
`define UBEE_ALU_REMU    5'd19

`define UBEE_BRANCH_NONE 3'd0
`define UBEE_BRANCH_EQ   3'd1
`define UBEE_BRANCH_NE   3'd2
`define UBEE_BRANCH_LT   3'd3
`define UBEE_BRANCH_GE   3'd4
`define UBEE_BRANCH_LTU  3'd5
`define UBEE_BRANCH_GEU  3'd6

`define UBEE_IMM_NONE    3'd0
`define UBEE_IMM_I       3'd1
`define UBEE_IMM_S       3'd2
`define UBEE_IMM_B       3'd3
`define UBEE_IMM_U       3'd4
`define UBEE_IMM_J       3'd5
`define UBEE_IMM_Z       3'd6

`define UBEE_OPERAND_A_RS1   2'd0
`define UBEE_OPERAND_A_PC    2'd1
`define UBEE_OPERAND_A_ZERO  2'd2

`define UBEE_OPERAND_B_RS2   1'd0
`define UBEE_OPERAND_B_IMM   1'd1

`define UBEE_FORWARD_NONE    2'd0
`define UBEE_FORWARD_EX_MEM  2'd1
`define UBEE_FORWARD_MEM_WB  2'd2

`define UBEE_WB_NONE         3'd0
`define UBEE_WB_ALU          3'd1
`define UBEE_WB_MEMORY       3'd2
`define UBEE_WB_PC_NEXT      3'd3
`define UBEE_WB_CSR          3'd4

`define UBEE_MEMORY_BYTE     2'd0
`define UBEE_MEMORY_HALF     2'd1
`define UBEE_MEMORY_WORD     2'd2

`define UBEE_SYSTEM_NONE     4'd0
`define UBEE_SYSTEM_ECALL    4'd1
`define UBEE_SYSTEM_EBREAK   4'd2
`define UBEE_SYSTEM_MRET     4'd3
`define UBEE_SYSTEM_CSR_RW   4'd4
`define UBEE_SYSTEM_CSR_RS   4'd5
`define UBEE_SYSTEM_CSR_RC   4'd6
`define UBEE_SYSTEM_CSR_RWI  4'd7
`define UBEE_SYSTEM_CSR_RSI  4'd8
`define UBEE_SYSTEM_CSR_RCI  4'd9

`define UBEE_CSR_MSTATUS      12'h300
`define UBEE_CSR_MIE          12'h304
`define UBEE_CSR_MTVEC        12'h305
`define UBEE_CSR_MSCRATCH     12'h340
`define UBEE_CSR_MEPC         12'h341
`define UBEE_CSR_MCAUSE       12'h342
`define UBEE_CSR_MTVAL        12'h343
`define UBEE_CSR_MIP          12'h344
`define UBEE_CSR_MCYCLE       12'hb00
`define UBEE_CSR_MINSTRET     12'hb02
`define UBEE_CSR_MCYCLEH      12'hb80
`define UBEE_CSR_MINSTRETH    12'hb82

`define UBEE_CAUSE_INSTRUCTION_ADDRESS_MISALIGNED 32'd0
`define UBEE_CAUSE_INSTRUCTION_ACCESS_FAULT       32'd1
`define UBEE_CAUSE_ILLEGAL_INSTRUCTION            32'd2
`define UBEE_CAUSE_BREAKPOINT                     32'd3
`define UBEE_CAUSE_LOAD_ADDRESS_MISALIGNED        32'd4
`define UBEE_CAUSE_LOAD_ACCESS_FAULT               32'd5
`define UBEE_CAUSE_STORE_ADDRESS_MISALIGNED       32'd6
`define UBEE_CAUSE_STORE_ACCESS_FAULT              32'd7
`define UBEE_CAUSE_ECALL_MACHINE                   32'd11

`define UBEE_INTERRUPT_SOFTWARE 32'h8000_0003
`define UBEE_INTERRUPT_TIMER    32'h8000_0007
`define UBEE_INTERRUPT_EXTERNAL 32'h8000_000b

`define UBEE_CLINT_BASE         32'h4000_0000
`define UBEE_PLIC_BASE          32'h4000_1000

`define UBEE_PLIC_MODE_RISING   2'b00
`define UBEE_PLIC_MODE_FALLING  2'b01
`define UBEE_PLIC_MODE_LEVEL_LO 2'b10
`define UBEE_PLIC_MODE_LEVEL_HI 2'b11

`define UBEE_OPCODE_LOAD       7'b0000011
`define UBEE_OPCODE_MISC_MEM   7'b0001111
`define UBEE_OPCODE_OP_IMM     7'b0010011
`define UBEE_OPCODE_AUIPC      7'b0010111
`define UBEE_OPCODE_STORE      7'b0100011
`define UBEE_OPCODE_OP         7'b0110011
`define UBEE_OPCODE_LUI        7'b0110111
`define UBEE_OPCODE_BRANCH     7'b1100011
`define UBEE_OPCODE_JALR       7'b1100111
`define UBEE_OPCODE_JAL        7'b1101111
`define UBEE_OPCODE_SYSTEM     7'b1110011

`endif
