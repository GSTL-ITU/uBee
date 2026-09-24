`timescale 1ns/1ps
`include "uBee_defs.svh"

module uBee_immediate_gen (
  // opcode [6:0] is not part of any immediate encoding
  /* verilator lint_off UNUSEDSIGNAL */
  input  logic [31:0] instruction_i,
  /* verilator lint_on  UNUSEDSIGNAL */
  input  logic [2:0]  format_i,
  output logic [31:0] immediate_o
);
  always_comb begin
    immediate_o = 32'b0;

    unique case (format_i)
      `UBEE_IMM_I: begin
        immediate_o = {{20{instruction_i[31]}}, instruction_i[31:20]};
      end

      `UBEE_IMM_S: begin
        immediate_o = {
          {20{instruction_i[31]}},
          instruction_i[31:25],
          instruction_i[11:7]
        };
      end

      `UBEE_IMM_B: begin
        immediate_o = {
          {19{instruction_i[31]}},
          instruction_i[31],
          instruction_i[7],
          instruction_i[30:25],
          instruction_i[11:8],
          1'b0
        };
      end

      `UBEE_IMM_U: begin
        immediate_o = {instruction_i[31:12], 12'b0};
      end

      `UBEE_IMM_J: begin
        immediate_o = {
          {11{instruction_i[31]}},
          instruction_i[31],
          instruction_i[19:12],
          instruction_i[20],
          instruction_i[30:21],
          1'b0
        };
      end

      `UBEE_IMM_Z: begin
        immediate_o = {27'b0, instruction_i[19:15]};
      end

      default: begin
        immediate_o = 32'b0;
      end
    endcase
  end
endmodule
