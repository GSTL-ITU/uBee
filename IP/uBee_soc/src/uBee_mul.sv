`timescale 1ns/1ps
// 2-cycle multiply unit for RV32M (single 33x33 signed multiplier).
`include "uBee_defs.svh"

module uBee_mul (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        start_i,
  input  logic        flush_i,
  input  logic        result_ack_i,
  input  logic [4:0]  operation_i,
  input  logic [31:0] operand_a_i,
  input  logic [31:0] operand_b_i,
  output logic        busy_o,
  output logic        done_o,
  output logic [31:0] result_o
);
  typedef enum logic [1:0] {
    MUL_IDLE  = 2'd0,
    MUL_PIPE  = 2'd1,
    MUL_DONE  = 2'd2
  } state_e;

  state_e        state_q;
  logic [4:0]    op_q;
  logic [31:0]   a_q, b_q;
  logic [63:0]   product_q;

  logic signed [32:0] a_ext;
  logic signed [32:0] b_ext;
  logic        [63:0] product_next;

  function automatic logic is_high_mul(input logic [4:0] op);
    return (op == `UBEE_ALU_MULH) ||
           (op == `UBEE_ALU_MULHSU) ||
           (op == `UBEE_ALU_MULHU);
  endfunction

  always_comb begin
    unique case (op_q)
      `UBEE_ALU_MUL: begin
        a_ext = {a_q[31], a_q};
        b_ext = {b_q[31], b_q};
      end
      `UBEE_ALU_MULH: begin
        a_ext = {a_q[31], a_q};
        b_ext = {b_q[31], b_q};
      end
      `UBEE_ALU_MULHSU: begin
        a_ext = {a_q[31], a_q};
        b_ext = {1'b0, b_q};
      end
      default: begin // MULHU
        a_ext = {1'b0, a_q};
        b_ext = {1'b0, b_q};
      end
    endcase

    product_next = a_ext * b_ext;
  end

  assign busy_o   = (state_q != MUL_IDLE);
  assign done_o   = (state_q == MUL_DONE);
  assign result_o = is_high_mul(op_q) ? product_q[63:32] : product_q[31:0];

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      state_q   <= MUL_IDLE;
      op_q      <= 5'b0;
      a_q       <= 32'b0;
      b_q       <= 32'b0;
      product_q <= 64'b0;
    end else if (flush_i) begin
      state_q <= MUL_IDLE;
    end else begin
      unique case (state_q)
        MUL_IDLE: begin
          if (start_i) begin
            op_q    <= operation_i;
            a_q     <= operand_a_i;
            b_q     <= operand_b_i;
            state_q <= MUL_PIPE;
          end
        end
        MUL_PIPE: begin
          product_q <= product_next;
          state_q   <= MUL_DONE;
        end
        MUL_DONE: begin
          if (result_ack_i) begin
            state_q <= MUL_IDLE;
          end
        end
        default: begin
        end
      endcase
    end
  end
endmodule
