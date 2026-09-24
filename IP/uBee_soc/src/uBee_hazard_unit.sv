`timescale 1ns/1ps
`include "uBee_defs.svh"

module uBee_hazard_unit (
  input  logic       decode_valid_i,
  input  logic       decode_uses_rs1_i,
  input  logic       decode_uses_rs2_i,
  input  logic [4:0] decode_rs1_addr_i,
  input  logic [4:0] decode_rs2_addr_i,

  input  logic       execute_valid_i,
  input  logic       execute_uses_rs1_i,
  input  logic       execute_uses_rs2_i,
  input  logic [4:0] execute_rs1_addr_i,
  input  logic [4:0] execute_rs2_addr_i,
  input  logic       execute_memory_read_i,
  input  logic       execute_rd_write_i,
  input  logic [4:0] execute_rd_addr_i,

  input  logic       memory_valid_i,
  input  logic       memory_rd_write_i,
  input  logic       memory_read_i,
  input  logic [4:0] memory_rd_addr_i,

  input  logic       writeback_valid_i,
  input  logic       writeback_rd_write_i,
  input  logic [4:0] writeback_rd_addr_i,

  output logic       load_use_stall_o,
  output logic [1:0] forward_rs1_o,
  output logic [1:0] forward_rs2_o
);
  logic decode_rs1_load_match;
  logic decode_rs2_load_match;
  logic execute_rs1_memory_match;
  logic execute_rs2_memory_match;
  logic execute_rs1_writeback_match;
  logic execute_rs2_writeback_match;

  always_comb begin
    decode_rs1_load_match =
      decode_uses_rs1_i && (decode_rs1_addr_i != 5'd0) &&
      (decode_rs1_addr_i == execute_rd_addr_i);
    decode_rs2_load_match =
      decode_uses_rs2_i && (decode_rs2_addr_i != 5'd0) &&
      (decode_rs2_addr_i == execute_rd_addr_i);

    load_use_stall_o =
      decode_valid_i && execute_valid_i && execute_memory_read_i &&
      execute_rd_write_i && (execute_rd_addr_i != 5'd0) &&
      (decode_rs1_load_match || decode_rs2_load_match);

    execute_rs1_memory_match =
      execute_valid_i && execute_uses_rs1_i &&
      memory_valid_i && memory_rd_write_i &&
      (memory_rd_addr_i != 5'd0) &&
      (execute_rs1_addr_i == memory_rd_addr_i);
    execute_rs2_memory_match =
      execute_valid_i && execute_uses_rs2_i &&
      memory_valid_i && memory_rd_write_i &&
      (memory_rd_addr_i != 5'd0) &&
      (execute_rs2_addr_i == memory_rd_addr_i);

    execute_rs1_writeback_match =
      execute_valid_i && execute_uses_rs1_i &&
      writeback_valid_i && writeback_rd_write_i &&
      (writeback_rd_addr_i != 5'd0) &&
      (execute_rs1_addr_i == writeback_rd_addr_i);
    execute_rs2_writeback_match =
      execute_valid_i && execute_uses_rs2_i &&
      writeback_valid_i && writeback_rd_write_i &&
      (writeback_rd_addr_i != 5'd0) &&
      (execute_rs2_addr_i == writeback_rd_addr_i);

    forward_rs1_o = `UBEE_FORWARD_NONE;
    if (execute_rs1_memory_match) begin
      if (!memory_read_i) begin
        forward_rs1_o = `UBEE_FORWARD_EX_MEM;
      end
    end else if (execute_rs1_writeback_match) begin
      forward_rs1_o = `UBEE_FORWARD_MEM_WB;
    end

    forward_rs2_o = `UBEE_FORWARD_NONE;
    if (execute_rs2_memory_match) begin
      if (!memory_read_i) begin
        forward_rs2_o = `UBEE_FORWARD_EX_MEM;
      end
    end else if (execute_rs2_writeback_match) begin
      forward_rs2_o = `UBEE_FORWARD_MEM_WB;
    end
  end
endmodule
