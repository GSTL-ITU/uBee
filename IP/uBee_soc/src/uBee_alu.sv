`timescale 1ns/1ps
`include "uBee_defs.svh"

module uBee_alu (
  input  logic [31:0] operand_a_i,
  input  logic [31:0] operand_b_i,
  input  logic [4:0]  operation_i,
  output logic [31:0] result_o
);
  logic signed [31:0] signed_a, signed_b;
  logic        [31:0] unsigned_a, unsigned_b;

  assign signed_a   = $signed(operand_a_i);
  assign signed_b   = $signed(operand_b_i);
  assign unsigned_a = operand_a_i;
  assign unsigned_b = operand_b_i;

  always_comb begin
    result_o = 32'b0;

    unique case (operation_i)
      `UBEE_ALU_ADD:    result_o = operand_a_i + operand_b_i;
      `UBEE_ALU_SUB:    result_o = operand_a_i - operand_b_i;
      `UBEE_ALU_SLL:    result_o = operand_a_i << operand_b_i[4:0];
      `UBEE_ALU_SLT:    result_o = {31'b0, signed_a < signed_b};
      `UBEE_ALU_SLTU:   result_o = {31'b0, unsigned_a < unsigned_b};
      `UBEE_ALU_XOR:    result_o = operand_a_i ^ operand_b_i;
      `UBEE_ALU_SRL:    result_o = operand_a_i >> operand_b_i[4:0];
      `UBEE_ALU_SRA:    result_o = signed_a >>> operand_b_i[4:0];
      `UBEE_ALU_OR:     result_o = operand_a_i | operand_b_i;
      `UBEE_ALU_AND:    result_o = operand_a_i & operand_b_i;
      `UBEE_ALU_PASS_A: result_o = operand_a_i;
      `UBEE_ALU_PASS_B: result_o = operand_b_i;
      default:          result_o = 32'b0;
    endcase
  end
endmodule
