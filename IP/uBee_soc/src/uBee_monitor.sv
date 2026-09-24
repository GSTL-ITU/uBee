`timescale 1ns/1ps
`include "uBee_monitor_regs.svh"
`include "uBee_perf_events.svh"

module uBee_monitor #(
  parameter logic [31:0] BASE_ADDRESS = `UBEE_MON_BASE_DEFAULT,
  parameter bit          AXI_ENABLE    = 1'b1,
  parameter bit          BP_ENABLE     = 1'b1
) (
  input  logic               clk_i,
  input  logic               rst_ni,

  input  logic               req_valid_i,
  output logic               req_ready_o,
  input  logic               req_write_i,
  input  logic [31:0]        req_addr_i,
  input  logic [31:0]        req_wdata_i,
  input  logic [3:0]         req_wstrb_i,
  output logic               rsp_valid_o,
  input  logic               rsp_ready_i,
  output logic [31:0]        rsp_rdata_o,
  output logic               rsp_err_o,

  input  uBee_perf_events_t  perf_events_i
);
  /* verilator lint_off UNUSEDPARAM */
  localparam logic [31:0] UNUSED_BASE = BASE_ADDRESS;
  /* verilator lint_on UNUSEDPARAM */

  logic [`UBEE_MON_NUM_COUNTERS-1:0][31:0] live_q;
  logic [`UBEE_MON_NUM_COUNTERS-1:0][31:0] shadow_q;

  logic        enable_q;
  logic        freeze_q;
  logic        rsp_valid_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;

  logic [11:0] offset;
  logic        word_aligned;
  logic        request_fire;
  logic        count_enable;
  logic        clear_pulse;
  logic        snapshot_pulse;
  logic        reg_hit;
  logic        ctrl_write;
  logic [4:0]  cnt_idx;
  logic [31:0] read_data;
  logic [31:0] capability_val;

  function automatic logic [31:0] sat_inc(
    input logic [31:0] value,
    input logic        inc
  );
    begin
      if (!inc) begin
        sat_inc = value;
      end else if (value == 32'hFFFF_FFFF) begin
        sat_inc = value;
      end else begin
        sat_inc = value + 32'd1;
      end
    end
  endfunction

  function automatic logic [4:0] offset_to_cnt_idx(input logic [11:0] off);
    logic [11:0] delta;
    begin
      delta = off - `UBEE_MON_CYCLES_OFFSET;
      if (off >= `UBEE_MON_LOAD_COUNT_OFFSET) begin
        offset_to_cnt_idx = 5'((delta >> 2) - 12'h1);
      end else begin
        offset_to_cnt_idx = 5'(delta >> 2);
      end
    end
  endfunction

  function automatic logic is_counter_offset(input logic [11:0] off);
    begin
      if ((off & 12'h003) != 12'h000) begin
        is_counter_offset = 1'b0;
      end else if (off == `UBEE_MON_CAPABILITY_OFFSET) begin
        is_counter_offset = 1'b0;
      end else if (off >= `UBEE_MON_CYCLES_OFFSET &&
                   off <= `UBEE_MON_DIV_USE_OFFSET) begin
        is_counter_offset = 1'b1;
      end else if (off >= `UBEE_MON_LOAD_COUNT_OFFSET &&
                   off <= `UBEE_MON_SPEC_REDIRECT_OFFSET) begin
        is_counter_offset = 1'b1;
      end else begin
        is_counter_offset = 1'b0;
      end
    end
  endfunction

  function automatic logic [31:0] build_capability(input bit axi_en, input bit bp_en);
    logic [15:0] features;
    begin
      features = `UBEE_MON_CAP_BASE |
               `UBEE_MON_CAP_FLUSH_HAZARD |
               `UBEE_MON_CAP_BRANCH |
               `UBEE_MON_CAP_IMEM |
               `UBEE_MON_CAP_DMEM |
               `UBEE_MON_CAP_BUS_ERROR |
               `UBEE_MON_CAP_IRQ |
               `UBEE_MON_CAP_EXEC_UNIT |
               `UBEE_MON_CAP_LOAD_STORE |
               `UBEE_MON_CAP_BRANCH_STALL |
               `UBEE_MON_CAP_SNAPSHOT |
               `UBEE_MON_CAP_MEM_WAIT_TOTAL;
      if (axi_en) begin
        features |= `UBEE_MON_CAP_AXI;
      end
      if (bp_en) begin
        features |= `UBEE_MON_CAP_PREDICTOR;
        features |= `UBEE_MON_CAP_SPEC_REDIRECT;
      end
      build_capability = {features,
                          `UBEE_MON_MAP_REVISION,
                          `UBEE_MON_CAP_WIDTH_BITS};
    end
  endfunction

  assign capability_val = build_capability(AXI_ENABLE, BP_ENABLE);
  assign offset       = req_addr_i[11:0];
  assign word_aligned = (req_addr_i[1:0] == 2'b00);
  assign req_ready_o  = !rsp_valid_q || rsp_ready_i;
  assign request_fire = req_valid_i && req_ready_o;
  assign count_enable = enable_q && !freeze_q;
  assign clear_pulse  = request_fire && req_write_i &&
    (offset == `UBEE_MON_CTRL_OFFSET) &&
    req_wdata_i[`UBEE_MON_CTRL_CLEAR];
  assign snapshot_pulse = request_fire && req_write_i &&
    (offset == `UBEE_MON_CTRL_OFFSET) &&
    req_wdata_i[`UBEE_MON_CTRL_SNAPSHOT];

  assign rsp_valid_o = rsp_valid_q;
  assign rsp_rdata_o = rsp_rdata_q;
  assign rsp_err_o   = rsp_err_q;

  always_comb begin
    reg_hit     = 1'b0;
    ctrl_write  = 1'b0;
    cnt_idx     = 5'd0;
    read_data   = 32'b0;

    if (offset == `UBEE_MON_CTRL_OFFSET) begin
      reg_hit   = 1'b1;
      ctrl_write = 1'b1;
      read_data = {28'b0, enable_q, freeze_q, 1'b0, 1'b0};
    end else if (offset == `UBEE_MON_CAPABILITY_OFFSET) begin
      reg_hit   = 1'b1;
      read_data = capability_val;
    end else if (offset >= (`UBEE_MON_SHADOW_BIAS + `UBEE_MON_CYCLES_OFFSET) &&
                 offset <= (`UBEE_MON_SHADOW_BIAS +
                            `UBEE_MON_SPEC_REDIRECT_OFFSET) &&
                 is_counter_offset(offset - `UBEE_MON_SHADOW_BIAS)) begin
      reg_hit     = 1'b1;
      cnt_idx = offset_to_cnt_idx(offset - `UBEE_MON_SHADOW_BIAS);
      read_data = shadow_q[cnt_idx];
    end else if (is_counter_offset(offset)) begin
      reg_hit   = 1'b1;
      cnt_idx   = offset_to_cnt_idx(offset);
      read_data = live_q[cnt_idx];
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      enable_q      <= 1'b0;
      freeze_q      <= 1'b0;
      live_q        <= '0;
      shadow_q      <= '0;
      rsp_valid_q   <= 1'b0;
      rsp_err_q     <= 1'b0;
    end else begin
      if (clear_pulse) begin
        live_q   <= '0;
        shadow_q <= '0;
      end else if (snapshot_pulse) begin
        shadow_q <= live_q;
      end else if (count_enable && !snapshot_pulse) begin
        live_q[`UBEE_MON_CNT_CYCLES] <=
          sat_inc(live_q[`UBEE_MON_CNT_CYCLES], 1'b1);
        live_q[`UBEE_MON_CNT_RETIRED] <=
          sat_inc(live_q[`UBEE_MON_CNT_RETIRED], perf_events_i.retired);
        live_q[`UBEE_MON_CNT_STALL] <=
          sat_inc(live_q[`UBEE_MON_CNT_STALL], perf_events_i.stall);
        live_q[`UBEE_MON_CNT_FLUSH] <=
          sat_inc(live_q[`UBEE_MON_CNT_FLUSH], perf_events_i.flush);
        live_q[`UBEE_MON_CNT_BUBBLE] <=
          sat_inc(live_q[`UBEE_MON_CNT_BUBBLE], perf_events_i.bubble);
        live_q[`UBEE_MON_CNT_HAZARD] <=
          sat_inc(live_q[`UBEE_MON_CNT_HAZARD], perf_events_i.hazard);
        live_q[`UBEE_MON_CNT_LOAD_USE] <=
          sat_inc(live_q[`UBEE_MON_CNT_LOAD_USE], perf_events_i.load_use);
        live_q[`UBEE_MON_CNT_BRANCH] <=
          sat_inc(live_q[`UBEE_MON_CNT_BRANCH], perf_events_i.branch);
        live_q[`UBEE_MON_CNT_BRANCH_TAKEN] <=
          sat_inc(live_q[`UBEE_MON_CNT_BRANCH_TAKEN], perf_events_i.branch_taken);
        live_q[`UBEE_MON_CNT_MISPREDICT] <=
          sat_inc(live_q[`UBEE_MON_CNT_MISPREDICT], perf_events_i.mispredict);
        live_q[`UBEE_MON_CNT_IMEM_WAIT] <=
          sat_inc(live_q[`UBEE_MON_CNT_IMEM_WAIT], perf_events_i.imem_wait);
        live_q[`UBEE_MON_CNT_DMEM_WAIT] <=
          sat_inc(live_q[`UBEE_MON_CNT_DMEM_WAIT], perf_events_i.dmem_wait);
        live_q[`UBEE_MON_CNT_AXI_WAIT] <=
          sat_inc(live_q[`UBEE_MON_CNT_AXI_WAIT], perf_events_i.axi_wait);
        live_q[`UBEE_MON_CNT_BUS_ERROR] <=
          sat_inc(live_q[`UBEE_MON_CNT_BUS_ERROR], perf_events_i.bus_error);
        live_q[`UBEE_MON_CNT_IRQ_TAKEN] <=
          sat_inc(live_q[`UBEE_MON_CNT_IRQ_TAKEN], perf_events_i.irq_taken);
        live_q[`UBEE_MON_CNT_ALU_USE] <=
          sat_inc(live_q[`UBEE_MON_CNT_ALU_USE], perf_events_i.alu_use);
        live_q[`UBEE_MON_CNT_MUL_USE] <=
          sat_inc(live_q[`UBEE_MON_CNT_MUL_USE], perf_events_i.mul_use);
        live_q[`UBEE_MON_CNT_DIV_USE] <=
          sat_inc(live_q[`UBEE_MON_CNT_DIV_USE], perf_events_i.div_use);
        live_q[`UBEE_MON_CNT_LOAD_COUNT] <=
          sat_inc(live_q[`UBEE_MON_CNT_LOAD_COUNT], perf_events_i.load_count);
        live_q[`UBEE_MON_CNT_STORE_COUNT] <=
          sat_inc(live_q[`UBEE_MON_CNT_STORE_COUNT], perf_events_i.store_count);
        live_q[`UBEE_MON_CNT_BRANCH_STALL] <=
          sat_inc(live_q[`UBEE_MON_CNT_BRANCH_STALL], perf_events_i.branch_stall);
        live_q[`UBEE_MON_CNT_MEM_WAIT_TOTAL] <=
          sat_inc(live_q[`UBEE_MON_CNT_MEM_WAIT_TOTAL],
                  perf_events_i.mem_wait_total);
        live_q[`UBEE_MON_CNT_PREDICT_CORRECT] <=
          sat_inc(live_q[`UBEE_MON_CNT_PREDICT_CORRECT],
                  perf_events_i.prediction_correct);
        live_q[`UBEE_MON_CNT_SPEC_REDIRECT] <=
          sat_inc(live_q[`UBEE_MON_CNT_SPEC_REDIRECT],
                  perf_events_i.spec_redirect);
      end

      if (request_fire) begin
        if (!word_aligned || !reg_hit) begin
          rsp_valid_q <= 1'b1;
          rsp_rdata_q <= 32'b0;
          rsp_err_q   <= 1'b1;
        end else if (req_write_i) begin
          rsp_valid_q <= 1'b1;
          rsp_rdata_q <= 32'b0;
          if (ctrl_write) begin
            rsp_err_q <= 1'b0;
            if (req_wstrb_i[0]) begin
              enable_q <= req_wdata_i[`UBEE_MON_CTRL_ENABLE];
              freeze_q <= req_wdata_i[`UBEE_MON_CTRL_FREEZE];
            end
          end else begin
            rsp_err_q <= 1'b1;
          end
        end else begin
          rsp_valid_q <= 1'b1;
          rsp_rdata_q <= read_data;
          rsp_err_q   <= 1'b0;
        end
      end else if (rsp_valid_q && rsp_ready_i) begin
        rsp_valid_q <= 1'b0;
        rsp_err_q   <= 1'b0;
      end
    end
  end
endmodule
