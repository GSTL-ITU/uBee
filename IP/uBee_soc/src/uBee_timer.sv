// General-purpose 64-bit timer with prescale, compare and auto-reload (TRM).
`timescale 1ns/1ps
module uBee_timer #(
  parameter logic [31:0] BASE_ADDRESS = 32'h4000_3000
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

  output logic        irq_o
);
  /* verilator lint_off UNUSEDPARAM */
  localparam logic [31:0] UNUSED_BASE = BASE_ADDRESS;
  /* verilator lint_on UNUSEDPARAM */

  localparam logic [11:0] OFF_CTRL       = 12'h000;
  localparam logic [11:0] OFF_PRESCALE   = 12'h004;
  localparam logic [11:0] OFF_COUNT_LO   = 12'h008;
  localparam logic [11:0] OFF_COUNT_HI   = 12'h00C;
  localparam logic [11:0] OFF_CMP0_LO    = 12'h010;
  localparam logic [11:0] OFF_CMP0_HI    = 12'h014;
  localparam logic [11:0] OFF_CMP1_LO    = 12'h018;
  localparam logic [11:0] OFF_CMP1_HI    = 12'h01C;
  localparam logic [11:0] OFF_PERIOD_LO  = 12'h020;
  localparam logic [11:0] OFF_PERIOD_HI  = 12'h024;
  localparam logic [11:0] OFF_STATUS     = 12'h028;
  localparam logic [11:0] OFF_CAPABILITY = 12'h02C;

  logic        ctrl_en_q;
  logic        ctrl_auto_q;
  logic        ctrl_irq0_q;
  logic        ctrl_irq1_q;
  logic [31:0] prescale_q;
  logic [63:0] count_q;
  logic [63:0] cmp0_q;
  logic [63:0] cmp1_q;
  logic [63:0] period_q;
  logic        status_cmp0_q;
  logic        status_cmp1_q;
  logic [31:0] prescale_cnt_q;

  logic        rsp_valid_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;

  logic [11:0] offset;
  logic        word_aligned;
  logic        request_fire;
  logic [31:0] read_data;
  logic        reg_hit;
  logic        tick_pulse;
  logic [63:0] count_next;
  logic        status_cmp0_next;
  logic        status_cmp1_next;

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
  assign tick_pulse   = ctrl_en_q && (prescale_cnt_q >= prescale_q);

  assign irq_o = (status_cmp0_q && ctrl_irq0_q) || (status_cmp1_q && ctrl_irq1_q);

  always_comb begin
    count_next = count_q;
    status_cmp0_next = status_cmp0_q;
    status_cmp1_next = status_cmp1_q;

    if (tick_pulse) begin
      count_next = count_q + 64'd1;
      if (count_next >= cmp0_q) begin
        status_cmp0_next = 1'b1;
        if (ctrl_auto_q) count_next = period_q;
      end
      if (count_next >= cmp1_q) begin
        status_cmp1_next = 1'b1;
      end
    end

    if (request_fire && word_aligned && req_write_i) begin
      unique case (offset)
        OFF_STATUS: begin
          if (req_wstrb_i[0] && req_wdata_i[0]) status_cmp0_next = 1'b0;
          if (req_wstrb_i[0] && req_wdata_i[1]) status_cmp1_next = 1'b0;
        end
        default: begin end
      endcase
    end
  end

  always_comb begin
    read_data = 32'b0;
    reg_hit   = 1'b0;
    unique case (offset)
      OFF_CTRL: begin
        read_data = {22'b0, ctrl_irq1_q, ctrl_irq0_q, 6'b0,
                     ctrl_auto_q, ctrl_en_q};
        reg_hit = 1'b1;
      end
      OFF_PRESCALE: begin read_data = prescale_q; reg_hit = 1'b1; end
      OFF_COUNT_LO: begin read_data = count_q[31:0]; reg_hit = 1'b1; end
      OFF_COUNT_HI: begin read_data = count_q[63:32]; reg_hit = 1'b1; end
      OFF_CMP0_LO:  begin read_data = cmp0_q[31:0]; reg_hit = 1'b1; end
      OFF_CMP0_HI:  begin read_data = cmp0_q[63:32]; reg_hit = 1'b1; end
      OFF_CMP1_LO:  begin read_data = cmp1_q[31:0]; reg_hit = 1'b1; end
      OFF_CMP1_HI:  begin read_data = cmp1_q[63:32]; reg_hit = 1'b1; end
      OFF_PERIOD_LO: begin read_data = period_q[31:0]; reg_hit = 1'b1; end
      OFF_PERIOD_HI: begin read_data = period_q[63:32]; reg_hit = 1'b1; end
      OFF_STATUS: begin
        read_data = {23'b0, ctrl_en_q, status_cmp1_q, status_cmp0_q};
        reg_hit = 1'b1;
      end
      OFF_CAPABILITY: begin
        read_data = 32'h0000_0440;
        reg_hit = 1'b1;
      end
      default: begin end
    endcase
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      ctrl_en_q        <= 1'b0;
      ctrl_auto_q      <= 1'b0;
      ctrl_irq0_q      <= 1'b0;
      ctrl_irq1_q      <= 1'b0;
      prescale_q       <= 32'b0;
      count_q          <= 64'b0;
      cmp0_q           <= 64'hFFFF_FFFF_FFFF_FFFF;
      cmp1_q           <= 64'hFFFF_FFFF_FFFF_FFFF;
      period_q         <= 64'b0;
      status_cmp0_q    <= 1'b0;
      status_cmp1_q    <= 1'b0;
      prescale_cnt_q   <= 32'b0;
      rsp_valid_q      <= 1'b0;
      rsp_rdata_q      <= 32'b0;
      rsp_err_q        <= 1'b0;
    end else begin
      status_cmp0_q <= status_cmp0_next;
      status_cmp1_q <= status_cmp1_next;

      if (tick_pulse) begin
        count_q <= count_next;
        prescale_cnt_q <= 32'b0;
      end else if (ctrl_en_q) begin
        prescale_cnt_q <= prescale_cnt_q + 32'd1;
      end

      if (request_fire && word_aligned && req_write_i && reg_hit) begin
        unique case (offset)
          OFF_CTRL: begin
            // xvlog: cannot index a function call result with [...] — mask instead.
            ctrl_en_q   <= |(apply_wstrb({31'b0, ctrl_en_q}, req_wdata_i, req_wstrb_i) & 32'h1);
            ctrl_auto_q <= |(apply_wstrb({31'b0, ctrl_auto_q}, req_wdata_i, req_wstrb_i) & 32'h2);
            ctrl_irq0_q <= |(apply_wstrb({31'b0, ctrl_irq0_q}, req_wdata_i, req_wstrb_i) & 32'h100);
            ctrl_irq1_q <= |(apply_wstrb({31'b0, ctrl_irq1_q}, req_wdata_i, req_wstrb_i) & 32'h200);
          end
          OFF_PRESCALE: prescale_q <= apply_wstrb(prescale_q, req_wdata_i, req_wstrb_i);
          OFF_COUNT_LO: count_q[31:0] <= apply_wstrb(count_q[31:0], req_wdata_i, req_wstrb_i);
          OFF_COUNT_HI: count_q[63:32] <= apply_wstrb(count_q[63:32], req_wdata_i, req_wstrb_i);
          OFF_CMP0_LO:  cmp0_q[31:0]  <= apply_wstrb(cmp0_q[31:0], req_wdata_i, req_wstrb_i);
          OFF_CMP0_HI:  cmp0_q[63:32] <= apply_wstrb(cmp0_q[63:32], req_wdata_i, req_wstrb_i);
          OFF_CMP1_LO:  cmp1_q[31:0]  <= apply_wstrb(cmp1_q[31:0], req_wdata_i, req_wstrb_i);
          OFF_CMP1_HI:  cmp1_q[63:32] <= apply_wstrb(cmp1_q[63:32], req_wdata_i, req_wstrb_i);
          OFF_PERIOD_LO: period_q[31:0]  <= apply_wstrb(period_q[31:0], req_wdata_i, req_wstrb_i);
          OFF_PERIOD_HI: period_q[63:32] <= apply_wstrb(period_q[63:32], req_wdata_i, req_wstrb_i);
          default: begin end
        endcase
      end

      if (rsp_valid_q && rsp_ready_i) begin
        rsp_valid_q <= 1'b0;
      end

      if (request_fire) begin
        rsp_valid_q <= 1'b1;
        rsp_err_q   <= !word_aligned || !reg_hit;
        rsp_rdata_q <= (word_aligned && reg_hit) ? read_data : 32'b0;
      end
    end
  end
endmodule
