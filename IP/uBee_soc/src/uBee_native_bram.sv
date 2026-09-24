// Synthesizable native memory with 1-cycle registered response.
// Matches verification/common/uBee_native_memory_model handshake timing
// (without HOST port). Intended for FPGA BRAM inference.
module uBee_native_bram #(
  parameter integer WORDS = 16384,
  parameter logic [31:0] BASE_ADDRESS = 32'h0000_0000,
  parameter integer READ_ONLY = 0,
  parameter INIT_FILE = ""
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
  output logic        rsp_err_o
`ifdef SIMULATION
  ,
  input logic        tb_mem_we_i    = 1'b0,
  input logic [31:0] tb_mem_widx_i  = 32'b0,
  input logic [31:0] tb_mem_wdata_i = 32'b0
`endif
);
  localparam integer BRAM_ADDR_BITS = (WORDS <= 1) ? 1 : $clog2(WORDS);

  (* ram_style = "block" *) logic [31:0] memory_q [0:WORDS-1] /*verilator public_flat_rw*/;
  logic        rsp_valid_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;
  logic [31:0] address_offset;
  logic [BRAM_ADDR_BITS-1:0] word_index;
  logic        address_in_range;
  logic        request_fire;
  integer      init_index;

  initial begin
    if (INIT_FILE != "") begin
`ifdef SIMULATION
      for (init_index = 0; init_index < WORDS; init_index = init_index + 1) begin
        memory_q[init_index] = 32'b0;
      end
`endif
      $readmemh(INIT_FILE, memory_q);
    end else begin
      for (init_index = 0; init_index < WORDS; init_index = init_index + 1) begin
        memory_q[init_index] = 32'b0;
      end
    end
  end

  assign address_offset   = req_addr_i - BASE_ADDRESS;
  assign word_index       = address_offset[BRAM_ADDR_BITS+1:2];
  // Byte/half addresses are legal: core shifts/strobes using addr[1:0].
  assign address_in_range = (req_addr_i >= BASE_ADDRESS) &&
                            (address_offset[31:2] < WORDS);

  assign req_ready_o  = !rsp_valid_q || rsp_ready_i;
  assign request_fire = req_valid_i && req_ready_o;
  assign rsp_valid_o  = rsp_valid_q;
  assign rsp_rdata_o  = rsp_rdata_q;
  assign rsp_err_o    = rsp_err_q;

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      rsp_valid_q <= 1'b0;
      rsp_rdata_q <= 32'b0;
      rsp_err_q   <= 1'b0;
    end else begin
      if (rsp_valid_q && rsp_ready_i) begin
        rsp_valid_q <= 1'b0;
      end

      if (request_fire) begin
        rsp_valid_q <= 1'b1;
        rsp_err_q   <= !address_in_range || (READ_ONLY != 0 && req_write_i);

        if (address_in_range) begin
          rsp_rdata_q <= memory_q[word_index];
        end else begin
          rsp_rdata_q <= 32'b0;
        end

        if (req_write_i && address_in_range && (READ_ONLY == 0)) begin
          if (req_wstrb_i[0]) memory_q[word_index][7:0]   <= req_wdata_i[7:0];
          if (req_wstrb_i[1]) memory_q[word_index][15:8]  <= req_wdata_i[15:8];
          if (req_wstrb_i[2]) memory_q[word_index][23:16] <= req_wdata_i[23:16];
          if (req_wstrb_i[3]) memory_q[word_index][31:24] <= req_wdata_i[31:24];
        end
      end
`ifdef SIMULATION
      if (tb_mem_we_i && (tb_mem_widx_i < WORDS)) begin
        memory_q[tb_mem_widx_i[BRAM_ADDR_BITS-1:0]] <= tb_mem_wdata_i;
      end
`endif
    end
  end
endmodule
