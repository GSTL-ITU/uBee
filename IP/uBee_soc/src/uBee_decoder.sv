`timescale 1ns/1ps
`include "uBee_defs.svh"

module uBee_decoder #(
  parameter bit MUL_ENABLE = 1'b1,
  parameter bit DIV_ENABLE = 1'b1
) (
  input  logic [31:0] instruction_i,

  output logic        illegal_o,
  output logic        uses_rs1_o,
  output logic        uses_rs2_o,
  output logic        rd_write_o,

  output logic [2:0]  immediate_format_o,
  output logic [4:0]  alu_operation_o,
  output logic [1:0]  operand_a_select_o,
  output logic        operand_b_select_o,
  output logic [2:0]  branch_operation_o,
  output logic        jump_o,
  output logic        jump_register_o,

  output logic        memory_read_o,
  output logic        memory_write_o,
  output logic [1:0]  memory_size_o,
  output logic        memory_unsigned_o,
  output logic [2:0]  writeback_select_o,

  output logic        fence_o,
  output logic [3:0]  system_operation_o,
  output logic [11:0] csr_address_o
);
  logic [6:0] opcode;
  logic [2:0] funct3;
  logic [6:0] funct7;

  always_comb begin
    opcode = instruction_i[6:0];
    funct3 = instruction_i[14:12];
    funct7 = instruction_i[31:25];

    illegal_o            = 1'b1;
    uses_rs1_o           = 1'b0;
    uses_rs2_o           = 1'b0;
    rd_write_o           = 1'b0;
    immediate_format_o   = `UBEE_IMM_NONE;
    alu_operation_o      = `UBEE_ALU_ADD;
    operand_a_select_o   = `UBEE_OPERAND_A_RS1;
    operand_b_select_o   = `UBEE_OPERAND_B_RS2;
    branch_operation_o   = `UBEE_BRANCH_NONE;
    jump_o               = 1'b0;
    jump_register_o      = 1'b0;
    memory_read_o        = 1'b0;
    memory_write_o       = 1'b0;
    memory_size_o        = `UBEE_MEMORY_WORD;
    memory_unsigned_o    = 1'b0;
    writeback_select_o   = `UBEE_WB_NONE;
    fence_o              = 1'b0;
    system_operation_o   = `UBEE_SYSTEM_NONE;
    csr_address_o        = 12'b0;

    unique case (opcode)
      `UBEE_OPCODE_LUI: begin
        illegal_o          = 1'b0;
        rd_write_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_U;
        alu_operation_o    = `UBEE_ALU_PASS_B;
        operand_a_select_o = `UBEE_OPERAND_A_ZERO;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;
        writeback_select_o = `UBEE_WB_ALU;
      end

      `UBEE_OPCODE_AUIPC: begin
        illegal_o          = 1'b0;
        rd_write_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_U;
        alu_operation_o    = `UBEE_ALU_ADD;
        operand_a_select_o = `UBEE_OPERAND_A_PC;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;
        writeback_select_o = `UBEE_WB_ALU;
      end

      `UBEE_OPCODE_JAL: begin
        illegal_o          = 1'b0;
        rd_write_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_J;
        alu_operation_o    = `UBEE_ALU_ADD;
        operand_a_select_o = `UBEE_OPERAND_A_PC;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;
        jump_o             = 1'b1;
        writeback_select_o = `UBEE_WB_PC_NEXT;
      end

      `UBEE_OPCODE_JALR: begin
        uses_rs1_o         = 1'b1;
        rd_write_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_I;
        alu_operation_o    = `UBEE_ALU_ADD;
        operand_a_select_o = `UBEE_OPERAND_A_RS1;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;
        jump_o             = 1'b1;
        jump_register_o    = 1'b1;
        writeback_select_o = `UBEE_WB_PC_NEXT;

        if (funct3 == 3'b000) begin
          illegal_o = 1'b0;
        end
      end

      `UBEE_OPCODE_BRANCH: begin
        uses_rs1_o         = 1'b1;
        uses_rs2_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_B;
        alu_operation_o    = `UBEE_ALU_ADD;
        operand_a_select_o = `UBEE_OPERAND_A_PC;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;

        unique case (funct3)
          3'b000: begin illegal_o = 1'b0; branch_operation_o = `UBEE_BRANCH_EQ;  end
          3'b001: begin illegal_o = 1'b0; branch_operation_o = `UBEE_BRANCH_NE;  end
          3'b100: begin illegal_o = 1'b0; branch_operation_o = `UBEE_BRANCH_LT;  end
          3'b101: begin illegal_o = 1'b0; branch_operation_o = `UBEE_BRANCH_GE;  end
          3'b110: begin illegal_o = 1'b0; branch_operation_o = `UBEE_BRANCH_LTU; end
          3'b111: begin illegal_o = 1'b0; branch_operation_o = `UBEE_BRANCH_GEU; end
          default: begin end
        endcase
      end

      `UBEE_OPCODE_LOAD: begin
        uses_rs1_o         = 1'b1;
        rd_write_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_I;
        alu_operation_o    = `UBEE_ALU_ADD;
        operand_a_select_o = `UBEE_OPERAND_A_RS1;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;
        memory_read_o      = 1'b1;
        writeback_select_o = `UBEE_WB_MEMORY;

        unique case (funct3)
          3'b000: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_BYTE; memory_unsigned_o = 1'b0; end
          3'b001: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_HALF; memory_unsigned_o = 1'b0; end
          3'b010: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_WORD; memory_unsigned_o = 1'b0; end
          3'b100: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_BYTE; memory_unsigned_o = 1'b1; end
          3'b101: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_HALF; memory_unsigned_o = 1'b1; end
          default: begin end
        endcase
      end

      `UBEE_OPCODE_STORE: begin
        uses_rs1_o         = 1'b1;
        uses_rs2_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_S;
        alu_operation_o    = `UBEE_ALU_ADD;
        operand_a_select_o = `UBEE_OPERAND_A_RS1;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;
        memory_write_o     = 1'b1;

        unique case (funct3)
          3'b000: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_BYTE; end
          3'b001: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_HALF; end
          3'b010: begin illegal_o = 1'b0; memory_size_o = `UBEE_MEMORY_WORD; end
          default: begin end
        endcase
      end

      `UBEE_OPCODE_OP_IMM: begin
        uses_rs1_o         = 1'b1;
        rd_write_o         = 1'b1;
        immediate_format_o = `UBEE_IMM_I;
        operand_a_select_o = `UBEE_OPERAND_A_RS1;
        operand_b_select_o = `UBEE_OPERAND_B_IMM;
        writeback_select_o = `UBEE_WB_ALU;

        unique case (funct3)
          3'b000: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_ADD;  end
          3'b010: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SLT;  end
          3'b011: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SLTU; end
          3'b100: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_XOR;  end
          3'b110: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_OR;   end
          3'b111: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_AND;  end

          3'b001: begin
            if (funct7 == 7'b0000000) begin
              illegal_o = 1'b0;
              alu_operation_o = `UBEE_ALU_SLL;
            end
          end

          3'b101: begin
            if (funct7 == 7'b0000000) begin
              illegal_o = 1'b0;
              alu_operation_o = `UBEE_ALU_SRL;
            end else if (funct7 == 7'b0100000) begin
              illegal_o = 1'b0;
              alu_operation_o = `UBEE_ALU_SRA;
            end
          end

          default: begin end
        endcase
      end

      `UBEE_OPCODE_OP: begin
        uses_rs1_o         = 1'b1;
        uses_rs2_o         = 1'b1;
        rd_write_o         = 1'b1;
        operand_a_select_o = `UBEE_OPERAND_A_RS1;
        operand_b_select_o = `UBEE_OPERAND_B_RS2;
        writeback_select_o = `UBEE_WB_ALU;

        unique case ({funct7, funct3})
          // RV32I base
          10'b0000000_000: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_ADD;    end
          10'b0100000_000: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SUB;    end
          10'b0000000_001: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SLL;    end
          10'b0000000_010: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SLT;    end
          10'b0000000_011: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SLTU;   end
          10'b0000000_100: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_XOR;    end
          10'b0000000_101: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SRL;    end
          10'b0100000_101: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_SRA;    end
          10'b0000000_110: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_OR;     end
          10'b0000000_111: begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_AND;    end
          // M extension (funct7 = 0000001)
          10'b0000001_000: begin
            if (MUL_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_MUL; end
          end
          10'b0000001_001: begin
            if (MUL_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_MULH; end
          end
          10'b0000001_010: begin
            if (MUL_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_MULHSU; end
          end
          10'b0000001_011: begin
            if (MUL_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_MULHU; end
          end
          10'b0000001_100: begin
            if (DIV_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_DIV; end
          end
          10'b0000001_101: begin
            if (DIV_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_DIVU; end
          end
          10'b0000001_110: begin
            if (DIV_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_REM; end
          end
          10'b0000001_111: begin
            if (DIV_ENABLE) begin illegal_o = 1'b0; alu_operation_o = `UBEE_ALU_REMU; end
          end
          default: begin end
        endcase
      end

      `UBEE_OPCODE_MISC_MEM: begin
        if ((funct3 == 3'b000) || (funct3 == 3'b001)) begin
          illegal_o = 1'b0;
          fence_o   = 1'b1;
        end
      end

      `UBEE_OPCODE_SYSTEM: begin
        unique case (funct3)
          3'b000: begin
            unique case (instruction_i)
              32'h0000_0073: begin illegal_o = 1'b0; system_operation_o = `UBEE_SYSTEM_ECALL;  end
              32'h0010_0073: begin illegal_o = 1'b0; system_operation_o = `UBEE_SYSTEM_EBREAK; end
              32'h3020_0073: begin illegal_o = 1'b0; system_operation_o = `UBEE_SYSTEM_MRET;   end
              default: begin end
            endcase
          end

          3'b001, 3'b010, 3'b011: begin
            illegal_o          = 1'b0;
            uses_rs1_o         = 1'b1;
            rd_write_o         = 1'b1;
            writeback_select_o = `UBEE_WB_CSR;
            csr_address_o      = instruction_i[31:20];

            unique case (funct3)
              3'b001: system_operation_o = `UBEE_SYSTEM_CSR_RW;
              3'b010: system_operation_o = `UBEE_SYSTEM_CSR_RS;
              default: system_operation_o = `UBEE_SYSTEM_CSR_RC;
            endcase
          end

          3'b101, 3'b110, 3'b111: begin
            illegal_o          = 1'b0;
            rd_write_o         = 1'b1;
            immediate_format_o = `UBEE_IMM_Z;
            writeback_select_o = `UBEE_WB_CSR;
            csr_address_o      = instruction_i[31:20];

            unique case (funct3)
              3'b101: system_operation_o = `UBEE_SYSTEM_CSR_RWI;
              3'b110: system_operation_o = `UBEE_SYSTEM_CSR_RSI;
              default: system_operation_o = `UBEE_SYSTEM_CSR_RCI;
            endcase
          end

          default: begin end
        endcase
      end

      default: begin end
    endcase

    // Illegal instruction hiçbir mimari yan etki üretemez. Bu son kapı,
    // gelecekte decoder genişletilirken güvenli varsayılanı korur.
    if (illegal_o) begin
      uses_rs1_o           = 1'b0;
      uses_rs2_o           = 1'b0;
      rd_write_o           = 1'b0;
      immediate_format_o   = `UBEE_IMM_NONE;
      alu_operation_o      = `UBEE_ALU_ADD;
      operand_a_select_o   = `UBEE_OPERAND_A_RS1;
      operand_b_select_o   = `UBEE_OPERAND_B_RS2;
      branch_operation_o   = `UBEE_BRANCH_NONE;
      jump_o               = 1'b0;
      jump_register_o      = 1'b0;
      memory_read_o        = 1'b0;
      memory_write_o       = 1'b0;
      memory_size_o        = `UBEE_MEMORY_WORD;
      memory_unsigned_o    = 1'b0;
      writeback_select_o   = `UBEE_WB_NONE;
      fence_o              = 1'b0;
      system_operation_o   = `UBEE_SYSTEM_NONE;
      csr_address_o        = 12'b0;
    end
  end
endmodule
