// Memory-mapped GPIO with per-bank 0x20 stride (TRM GPIO chapter).
`timescale 1ns/1ps
module uBee_gpio #(
  parameter logic [31:0] BASE_ADDRESS      = 32'h4000_2000,
  parameter integer      NUM_BANKS         = 1,
  parameter logic [31:0] INPUT_VALID_MASK  = 32'hFFFF_FFFF,
  parameter logic [31:0] OUTPUT_VALID_MASK = 32'hFFFF_FFFF,
  parameter integer      GPIO_WIDTH        = 32
) (
  input  logic        clk_i,
  input  logic        rst_ni,

  input  logic        req_valid_i,
  output logic        req_ready_o,
  input  logic        req_write_i,
  input  logic [31:0] req_addr_i,
  input  logic [31:0] req_wdata_i,
  input  logic [3:0]  req_wstrb_i,

  output logic        rsp_valid_o,
  input  logic        rsp_ready_i,
  output logic [31:0] rsp_rdata_o,
  output logic        rsp_err_o,

  input  logic [GPIO_WIDTH-1:0] gpio_in_i,
  output logic [GPIO_WIDTH-1:0] gpio_out_o,
  output logic [GPIO_WIDTH-1:0] gpio_oe_o
);
  /* verilator lint_off UNUSEDPARAM */
  localparam logic [31:0] UNUSED_BASE = BASE_ADDRESS;
  /* verilator lint_on UNUSEDPARAM */

  localparam integer BANK_STRIDE = 32;

  logic [GPIO_WIDTH-1:0] gpio_in_sync_q;
  logic [GPIO_WIDTH-1:0] gpio_in_meta_q;
  logic [GPIO_WIDTH-1:0] out_q;
  logic [GPIO_WIDTH-1:0] oe_q;

  logic        rsp_valid_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;

  logic [11:0] offset;
  logic [4:0]  bank_idx;
  logic        word_aligned;
  logic        request_fire;
  logic        bank_hit;
  logic [4:0]  reg_offset;
  logic [31:0] read_data;
  logic        reg_hit;

  function automatic logic [31:0] apply_wstrb(
    input logic [31:0] old_data,
    input logic [31:0] new_data,
    input logic [3:0]  wstrb
  );
    begin
      apply_wstrb = old_data;
      if (wstrb[0]) apply_wstrb[7:0]   = new_data[7:0];
      if (wstrb[1]) apply_wstrb[15:8]  = new_data[15:8];
      if (wstrb[2]) apply_wstrb[23:16] = new_data[23:16];
      if (wstrb[3]) apply_wstrb[31:24] = new_data[31:24];
    end
  endfunction

  assign offset       = req_addr_i[11:0];
  assign bank_idx     = offset[11:5];
  assign reg_offset   = offset[4:0];
  assign word_aligned = (req_addr_i[1:0] == 2'b00);
  assign bank_hit     = (bank_idx < NUM_BANKS);
  assign req_ready_o  = !rsp_valid_q || rsp_ready_i;
  assign request_fire = req_valid_i && req_ready_o;
  assign rsp_valid_o  = rsp_valid_q;
  assign rsp_rdata_o  = rsp_rdata_q;
  assign rsp_err_o    = rsp_err_q;

  assign gpio_out_o = out_q & OUTPUT_VALID_MASK[GPIO_WIDTH-1:0];
  assign gpio_oe_o  = oe_q & OUTPUT_VALID_MASK[GPIO_WIDTH-1:0];

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      gpio_in_meta_q <= '0;
      gpio_in_sync_q <= '0;
    end else begin
      gpio_in_meta_q <= gpio_in_i;
      gpio_in_sync_q <= gpio_in_meta_q;
    end
  end

  always_comb begin
    read_data = 32'b0;
    reg_hit   = 1'b0;
    if (bank_hit) begin
      unique case (reg_offset)
        5'h00: begin
          read_data = gpio_in_sync_q & INPUT_VALID_MASK[GPIO_WIDTH-1:0];
          reg_hit   = 1'b1;
        end
        5'h04: begin
          read_data = out_q;
          reg_hit   = 1'b1;
        end
        5'h08, 5'h0C, 5'h10: begin
          read_data = 32'b0;
          reg_hit   = 1'b1;
        end
        5'h14: begin
          read_data = oe_q;
          reg_hit   = 1'b1;
        end
        5'h18: begin
          read_data = OUTPUT_VALID_MASK;
          reg_hit   = 1'b1;
        end
        default: begin
        end
      endcase
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      out_q       <= 32'b0;
      oe_q        <= 32'b0;
      rsp_valid_q <= 1'b0;
      rsp_rdata_q <= 32'b0;
      rsp_err_q   <= 1'b0;
    end else begin
      if (request_fire && word_aligned && req_write_i && bank_hit && reg_hit) begin
        unique case (reg_offset)
          5'h04: out_q <= apply_wstrb(out_q, req_wdata_i, req_wstrb_i) &
            OUTPUT_VALID_MASK;
          5'h08: out_q <= out_q | (req_wdata_i & OUTPUT_VALID_MASK);
          5'h0C: out_q <= out_q & ~(req_wdata_i & OUTPUT_VALID_MASK);
          5'h10: out_q <= out_q ^ (req_wdata_i & OUTPUT_VALID_MASK);
          5'h14: oe_q  <= apply_wstrb(oe_q, req_wdata_i, req_wstrb_i) &
            OUTPUT_VALID_MASK;
          default: begin end
        endcase
      end

      if (rsp_valid_q && rsp_ready_i) begin
        rsp_valid_q <= 1'b0;
      end

      if (request_fire) begin
        rsp_valid_q <= 1'b1;
        rsp_err_q   <= !word_aligned || !bank_hit || !reg_hit;
        rsp_rdata_q <= (word_aligned && bank_hit && reg_hit) ? read_data : 32'b0;
      end
    end
  end
endmodule
