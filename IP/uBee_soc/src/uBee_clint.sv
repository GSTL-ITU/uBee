// Single-hart CLINT: MSIP, MTIME, MTIMECMP on native valid/ready bus.
// Local 4 KiB window (router applies BASE). Side-effects on request fire.
`timescale 1ns/1ps
module uBee_clint #(
  parameter logic [31:0] BASE_ADDRESS = 32'h4000_0000
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

  output logic        irq_software_o,
  output logic        irq_timer_o
);
  // BASE_ADDRESS is informational for documentation / SoC wiring.
  /* verilator lint_off UNUSEDPARAM */
  localparam logic [31:0] UNUSED_BASE = BASE_ADDRESS;
  /* verilator lint_on UNUSEDPARAM */

  localparam logic [11:0] OFF_MSIP        = 12'h000;
  localparam logic [11:0] OFF_MTIMECMP_LO = 12'h400;
  localparam logic [11:0] OFF_MTIMECMP_HI = 12'h404;
  localparam logic [11:0] OFF_MTIME_LO    = 12'hFF8;
  localparam logic [11:0] OFF_MTIME_HI    = 12'hFFC;

  logic        msip_q;
  logic [63:0] mtime_q;
  logic [63:0] mtimecmp_q;

  logic        rsp_valid_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;

  logic [11:0] offset;
  logic        word_aligned;
  logic        request_fire;
  logic [31:0] read_data;
  logic        reg_hit;
  logic [63:0] mtime_next;
  logic [63:0] mtimecmp_next;
  logic        msip_next;
  logic        mtime_written;

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
  assign word_aligned = (req_addr_i[1:0] == 2'b00);
  assign req_ready_o  = !rsp_valid_q || rsp_ready_i;
  assign request_fire = req_valid_i && req_ready_o;
  assign rsp_valid_o  = rsp_valid_q;
  assign rsp_rdata_o  = rsp_rdata_q;
  assign rsp_err_o    = rsp_err_q;

  assign irq_software_o = msip_q;
  assign irq_timer_o    = (mtime_q >= mtimecmp_q);

  always_comb begin
    read_data = 32'b0;
    reg_hit   = 1'b0;
    unique case (offset)
      OFF_MSIP: begin
        read_data = {31'b0, msip_q};
        reg_hit   = 1'b1;
      end
      OFF_MTIMECMP_LO: begin
        read_data = mtimecmp_q[31:0];
        reg_hit   = 1'b1;
      end
      OFF_MTIMECMP_HI: begin
        read_data = mtimecmp_q[63:32];
        reg_hit   = 1'b1;
      end
      OFF_MTIME_LO: begin
        read_data = mtime_q[31:0];
        reg_hit   = 1'b1;
      end
      OFF_MTIME_HI: begin
        read_data = mtime_q[63:32];
        reg_hit   = 1'b1;
      end
      default: begin
        read_data = 32'b0;
        reg_hit   = 1'b0;
      end
    endcase
  end

  always_comb begin
    msip_next      = msip_q;
    mtimecmp_next  = mtimecmp_q;
    mtime_next     = mtime_q + 64'd1;
    mtime_written  = 1'b0;

    if (request_fire && word_aligned && req_write_i && reg_hit) begin
      unique case (offset)
        OFF_MSIP: begin
          if (req_wstrb_i[0]) begin
            msip_next = req_wdata_i[0];
          end
        end
        OFF_MTIMECMP_LO: begin
          mtimecmp_next[31:0] = apply_wstrb(mtimecmp_q[31:0], req_wdata_i, req_wstrb_i);
        end
        OFF_MTIMECMP_HI: begin
          mtimecmp_next[63:32] = apply_wstrb(mtimecmp_q[63:32], req_wdata_i, req_wstrb_i);
        end
        OFF_MTIME_LO: begin
          mtime_next    = {mtime_q[63:32], apply_wstrb(mtime_q[31:0], req_wdata_i, req_wstrb_i)};
          mtime_written = 1'b1;
        end
        OFF_MTIME_HI: begin
          mtime_next    = {apply_wstrb(mtime_q[63:32], req_wdata_i, req_wstrb_i), mtime_q[31:0]};
          mtime_written = 1'b1;
        end
        default: begin end
      endcase
    end

    // Written value replaces the auto-increment for that cycle.
    if (!mtime_written) begin
      mtime_next = mtime_q + 64'd1;
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      msip_q      <= 1'b0;
      mtime_q     <= 64'b0;
      mtimecmp_q  <= 64'hFFFF_FFFF_FFFF_FFFF;
      rsp_valid_q <= 1'b0;
      rsp_rdata_q <= 32'b0;
      rsp_err_q   <= 1'b0;
    end else begin
      msip_q     <= msip_next;
      mtime_q    <= mtime_next;
      mtimecmp_q <= mtimecmp_next;

      if (rsp_valid_q && rsp_ready_i) begin
        rsp_valid_q <= 1'b0;
      end

      if (request_fire) begin
        rsp_valid_q <= 1'b1;
        rsp_err_q   <= !word_aligned;
        rsp_rdata_q <= word_aligned ? read_data : 32'b0;
      end
    end
  end
endmodule
