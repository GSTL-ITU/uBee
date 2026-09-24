// Read/write data BRAM (Harvard D-port local target).
module uBee_dmem #(
  parameter integer WORDS = 16384,
  parameter logic [31:0] BASE_ADDRESS = 32'h2000_0000,
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
  uBee_native_bram #(
    .WORDS        (WORDS),
    .BASE_ADDRESS (BASE_ADDRESS),
    .READ_ONLY    (0),
    .INIT_FILE    (INIT_FILE)
  ) u_bram (
    .clk_i       (clk_i),
    .rst_ni      (rst_ni),
    .req_valid_i (req_valid_i),
    .req_ready_o (req_ready_o),
    .req_write_i (req_write_i),
    .req_addr_i  (req_addr_i),
    .req_wdata_i (req_wdata_i),
    .req_wstrb_i (req_wstrb_i),
    .rsp_valid_o (rsp_valid_o),
    .rsp_ready_i (rsp_ready_i),
    .rsp_rdata_o (rsp_rdata_o),
    .rsp_err_o   (rsp_err_o)
`ifdef SIMULATION
    ,
    .tb_mem_we_i    (tb_mem_we_i),
    .tb_mem_widx_i  (tb_mem_widx_i),
    .tb_mem_wdata_i (tb_mem_wdata_i)
`endif
  );
endmodule
