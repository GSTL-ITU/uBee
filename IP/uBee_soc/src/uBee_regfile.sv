`timescale 1ns/1ps
module uBee_regfile #(
  parameter bit SHADOW_BANK_ENABLE = 1'b1
) (
  input  logic        clk_i,
  input  logic        bank_select_i,

  input  logic [4:0]  rs1_addr_i,
  output logic [31:0] rs1_data_o,
  input  logic [4:0]  rs2_addr_i,
  output logic [31:0] rs2_data_o,

  input  logic        rd_write_i,
  input  logic [4:0]  rd_addr_i,
  input  logic [31:0] rd_data_i
);
  // Each context has an independent x1-x31 set. x0 is hard-wired to zero in
  // both contexts and therefore needs no physical storage.
  logic [31:0] normal_registers_q [1:31];
  logic [31:0] shadow_registers_q [1:31];
  logic        active_shadow_bank;

  assign active_shadow_bank = SHADOW_BANK_ENABLE && bank_select_i;

  always_comb begin
    rs1_data_o = 32'b0;
    if (rs1_addr_i != 5'd0) begin
      rs1_data_o = active_shadow_bank
        ? shadow_registers_q[rs1_addr_i]
        : normal_registers_q[rs1_addr_i];
    end
  end

  always_comb begin
    rs2_data_o = 32'b0;
    if (rs2_addr_i != 5'd0) begin
      rs2_data_o = active_shadow_bank
        ? shadow_registers_q[rs2_addr_i]
        : normal_registers_q[rs2_addr_i];
    end
  end

  always_ff @(posedge clk_i) begin
    if (rd_write_i && (rd_addr_i != 5'd0)) begin
      if (active_shadow_bank) begin
        shadow_registers_q[rd_addr_i] <= rd_data_i;
      end else begin
        normal_registers_q[rd_addr_i] <= rd_data_i;
      end
    end
  end
endmodule
