`timescale 1ns/1ps
// Pattern History Table (PHT): 2-bit saturating counters, weakly not-taken (01) on reset.
// Predict taken when counter[1]==1. BP_ENABLE=0 elides the table and always predicts NT.
// Indexing is done by the core: PC-fold bimodal (default), or optional GSHARE
// (pc_fold XOR zero-extended GHR) when BP_GSHARE=1.
module uBee_branch_predictor #(
  parameter bit BP_ENABLE = 1'b1,
  parameter int unsigned BP_ENTRIES = 128,
  parameter int unsigned BP_BIT_SIZE = $clog2(BP_ENTRIES)
) (
  input  logic        clk_i,
  input  logic        rst_ni,

  input  logic [BP_BIT_SIZE-1:0]  lookup_index_i,
  output logic        predict_taken_o,

  input  logic        update_valid_i,
  input  logic [BP_BIT_SIZE-1:0]  update_index_i,
  input  logic        update_taken_i
);
  localparam logic [1:0]  BP_WNT = 2'b01;

  logic [1:0] counter_table [BP_ENTRIES-1:0];

  assign predict_taken_o = BP_ENABLE && counter_table[lookup_index_i][1];

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      for (int unsigned entry = 0; entry < BP_ENTRIES; entry++) begin
        counter_table[entry] <= BP_WNT;
      end
    end else if (BP_ENABLE && update_valid_i) begin
      unique case (counter_table[update_index_i])
        2'b00: counter_table[update_index_i] <= update_taken_i ? 2'b01 : 2'b00;
        2'b01: counter_table[update_index_i] <= update_taken_i ? 2'b10 : 2'b00;
        2'b10: counter_table[update_index_i] <= update_taken_i ? 2'b11 : 2'b01;
        2'b11: counter_table[update_index_i] <= update_taken_i ? 2'b11 : 2'b10;
        default: counter_table[update_index_i] <= BP_WNT;
      endcase
    end
  end
endmodule
