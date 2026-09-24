`timescale 1ns/1ps
`include "uBee_defs.svh"

module uBee_branch_compare (
  input  logic [31:0] operand_a_i,
  input  logic [31:0] operand_b_i,
  input  logic [2:0]  operation_i,
  output logic        taken_o
);
  always_comb begin
    taken_o = 1'b0;

    unique case (operation_i)
      `UBEE_BRANCH_EQ:  taken_o = operand_a_i == operand_b_i;
      `UBEE_BRANCH_NE:  taken_o = operand_a_i != operand_b_i;
      `UBEE_BRANCH_LT:  taken_o = $signed(operand_a_i) < $signed(operand_b_i);
      `UBEE_BRANCH_GE:  taken_o = $signed(operand_a_i) >= $signed(operand_b_i);
      `UBEE_BRANCH_LTU: taken_o = operand_a_i < operand_b_i;
      `UBEE_BRANCH_GEU: taken_o = operand_a_i >= operand_b_i;
      default:    taken_o = 1'b0;
    endcase
  end
endmodule
