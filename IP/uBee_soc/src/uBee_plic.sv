// Compact 4 KiB PLIC: N sources, programmable 4-mode gateways, claim/complete.
// Source ID 0 is reserved. ext_irq_i[i] maps to source ID (i+1).
`timescale 1ns/1ps
module uBee_plic #(
  parameter integer      NUM_SOURCES       = 16,
  parameter integer      PRIO_BITS         = 3,
  parameter bit          IRQ_SYNC_ENABLE   = 1'b1,
  parameter logic [31:0] IRQ_MODE_RESET    = 32'h0000_0000,
  parameter logic [15:0] IRQ_INITIAL_LEVEL = 16'h0000,
  parameter logic [31:0] BASE_ADDRESS      = 32'h4000_1000
) (
  input  logic                    clk_i,
  input  logic                    rst_ni,

  input  logic                    req_valid_i,
  output logic                    req_ready_o,
  input  logic                    req_write_i,
  input  logic [31:0]             req_addr_i,
  input  logic [31:0]             req_wdata_i,
  input  logic [3:0]              req_wstrb_i,

  output logic                    rsp_valid_o,
  input  logic                    rsp_ready_i,
  output logic [31:0]             rsp_rdata_o,
  output logic                    rsp_err_o,

  input  logic [NUM_SOURCES-1:0]  ext_irq_i,
  output logic                    irq_external_o
);
  /* verilator lint_off UNUSEDPARAM */
  localparam logic [31:0] UNUSED_BASE = BASE_ADDRESS;
  /* verilator lint_on UNUSEDPARAM */

  localparam integer SRC_BITS   = (NUM_SOURCES <= 1) ? 1 : $clog2(NUM_SOURCES + 1);
  localparam integer PRIO_MAX   = (1 << PRIO_BITS) - 1;
  localparam logic [11:0] OFF_CAPABILITY  = 12'h000;
  localparam logic [11:0] OFF_PENDING     = 12'h100;
  localparam logic [11:0] OFF_SOURCE_MODE = 12'h104;
  localparam logic [11:0] OFF_INPUT_STATE = 12'h108;
  localparam logic [11:0] OFF_RESET_LEVEL = 12'h10C;
  localparam logic [11:0] OFF_ENABLE      = 12'h180;
  localparam logic [11:0] OFF_THRESHOLD   = 12'h300;
  localparam logic [11:0] OFF_CLAIM       = 12'h304;
  localparam logic [11:0] OFF_COMPLETE    = 12'h308;

  logic [PRIO_BITS-1:0] priority_q [1:NUM_SOURCES];
  logic [NUM_SOURCES:1] pending_q;
  logic [NUM_SOURCES:1] in_service_q;
  logic [NUM_SOURCES:1] enable_q;
  logic [PRIO_BITS-1:0] threshold_q;
  logic [31:0]          source_mode_q;
  (* ASYNC_REG = "TRUE" *) logic [NUM_SOURCES-1:0] sync_meta_q;
  (* ASYNC_REG = "TRUE" *) logic [NUM_SOURCES-1:0] sync_q;
  logic [NUM_SOURCES-1:0] prev_level_q;

  logic        rsp_valid_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;

  logic [11:0] offset;
  logic        word_aligned;
  logic        request_fire;
  logic [NUM_SOURCES-1:0] irq_level;
  logic [NUM_SOURCES:1]   gateway_event;
  logic [NUM_SOURCES:1]   level_active;
  logic [SRC_BITS-1:0]    best_id;
  logic [PRIO_BITS-1:0]   best_prio;
  logic                   best_valid;

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

  function automatic logic [1:0] mode_of(input integer src_id);
    begin
      mode_of = source_mode_q[((src_id-1)*2) +: 2];
    end
  endfunction

  assign offset       = req_addr_i[11:0];
  assign word_aligned = (req_addr_i[1:0] == 2'b00);
  assign req_ready_o  = !rsp_valid_q || rsp_ready_i;
  assign request_fire = req_valid_i && req_ready_o;
  assign rsp_valid_o  = rsp_valid_q;
  assign rsp_rdata_o  = rsp_rdata_q;
  assign rsp_err_o    = rsp_err_q;

  // Optional 2-FF synchronizer bank.
  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      sync_meta_q <= IRQ_INITIAL_LEVEL[NUM_SOURCES-1:0];
      sync_q      <= IRQ_INITIAL_LEVEL[NUM_SOURCES-1:0];
    end else if (IRQ_SYNC_ENABLE) begin
      sync_meta_q <= ext_irq_i;
      sync_q      <= sync_meta_q;
    end else begin
      sync_meta_q <= ext_irq_i;
      sync_q      <= ext_irq_i;
    end
  end

  assign irq_level = IRQ_SYNC_ENABLE ? sync_q : ext_irq_i;

  always_comb begin
    gateway_event = '0;
    level_active  = '0;
    for (int gi = 1; gi <= NUM_SOURCES; gi = gi + 1) begin
      unique case (mode_of(gi))
        2'b00: begin // rising
          gateway_event[gi] = (~prev_level_q[gi-1]) & irq_level[gi-1];
          level_active[gi]  = 1'b0;
        end
        2'b01: begin // falling
          gateway_event[gi] = prev_level_q[gi-1] & (~irq_level[gi-1]);
          level_active[gi]  = 1'b0;
        end
        2'b10: begin // active-low level
          gateway_event[gi] = ~irq_level[gi-1];
          level_active[gi]  = ~irq_level[gi-1];
        end
        default: begin // active-high level
          gateway_event[gi] = irq_level[gi-1];
          level_active[gi]  = irq_level[gi-1];
        end
      endcase
    end
  end

  // Priority arbitration: highest priority wins; tie -> lowest ID.
  always_comb begin
    best_id    = '0;
    best_prio  = '0;
    best_valid = 1'b0;
    for (int ai = 1; ai <= NUM_SOURCES; ai = ai + 1) begin
      if (pending_q[ai] && enable_q[ai] && (priority_q[ai] > threshold_q) &&
          (priority_q[ai] != '0)) begin
        if (!best_valid || (priority_q[ai] > best_prio) ||
            ((priority_q[ai] == best_prio) && (ai[SRC_BITS-1:0] < best_id))) begin
          best_valid = 1'b1;
          best_prio  = priority_q[ai];
          best_id    = ai[SRC_BITS-1:0];
        end
      end
    end
  end

  assign irq_external_o = best_valid;

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      for (int ri = 1; ri <= NUM_SOURCES; ri = ri + 1) begin
        priority_q[ri] <= '0;
      end
      pending_q      <= '0;
      in_service_q   <= '0;
      enable_q       <= '0;
      threshold_q    <= '0;
      source_mode_q  <= IRQ_MODE_RESET;
      prev_level_q   <= IRQ_INITIAL_LEVEL[NUM_SOURCES-1:0];
      rsp_valid_q    <= 1'b0;
      rsp_rdata_q    <= 32'b0;
      rsp_err_q      <= 1'b0;
    end else begin
      // Gateway pending updates (edge only when not pending/in-service).
      for (int gi = 1; gi <= NUM_SOURCES; gi = gi + 1) begin
        if (!pending_q[gi] && !in_service_q[gi] && gateway_event[gi]) begin
          pending_q[gi] <= 1'b1;
        end
      end

      // Advance edge history after sampling.
      prev_level_q <= irq_level;

      if (rsp_valid_q && rsp_ready_i) begin
        rsp_valid_q <= 1'b0;
      end

      if (request_fire) begin
        rsp_valid_q <= 1'b1;
        rsp_err_q   <= !word_aligned;
        rsp_rdata_q <= 32'b0;

        if (word_aligned) begin
          // -------- reads (CLAIM has side-effect) --------
          if (!req_write_i) begin
            if (offset == OFF_CAPABILITY) begin
              rsp_rdata_q <= {20'b0, PRIO_BITS[3:0], NUM_SOURCES[7:0]};
            end else if ((offset >= 12'h004) &&
                         (offset < 12'h004 + 12'(4*NUM_SOURCES)) &&
                         (offset[1:0] == 2'b00)) begin
              // PRIORITY[i] at 0x004 + 4*(i-1) for i=1..N
              integer prio_id;
              prio_id = ((offset - 12'h004) >> 2) + 1;
              if ((prio_id >= 1) && (prio_id <= NUM_SOURCES)) begin
                rsp_rdata_q <= {{(32-PRIO_BITS){1'b0}}, priority_q[prio_id]};
              end
            end else if (offset == OFF_PENDING) begin
              rsp_rdata_q <= {15'b0, pending_q, 1'b0};
            end else if (offset == OFF_SOURCE_MODE) begin
              rsp_rdata_q <= source_mode_q;
            end else if (offset == OFF_INPUT_STATE) begin
              rsp_rdata_q <= {{(32-NUM_SOURCES){1'b0}}, irq_level};
            end else if (offset == OFF_RESET_LEVEL) begin
              rsp_rdata_q <= {{(32-NUM_SOURCES){1'b0}},
                              IRQ_INITIAL_LEVEL[NUM_SOURCES-1:0]};
            end else if (offset == OFF_ENABLE) begin
              rsp_rdata_q <= {15'b0, enable_q, 1'b0};
            end else if (offset == OFF_THRESHOLD) begin
              rsp_rdata_q <= {{(32-PRIO_BITS){1'b0}}, threshold_q};
            end else if (offset == OFF_CLAIM) begin
              if (best_valid) begin
                rsp_rdata_q            <= {{(32-SRC_BITS){1'b0}}, best_id};
                pending_q[best_id]     <= 1'b0;
                in_service_q[best_id]  <= 1'b1;
              end else begin
                rsp_rdata_q <= 32'b0;
              end
            end else if (offset == OFF_COMPLETE) begin
              rsp_rdata_q <= 32'b0; // WO
            end
          end else begin
            // -------- writes --------
            if ((offset >= 12'h004) &&
                (offset < 12'h004 + 12'(4*NUM_SOURCES)) &&
                (offset[1:0] == 2'b00)) begin
              integer prio_id;
              logic [31:0] pdata;
              prio_id = ((offset - 12'h004) >> 2) + 1;
              if ((prio_id >= 1) && (prio_id <= NUM_SOURCES)) begin
                pdata = apply_wstrb({{(32-PRIO_BITS){1'b0}}, priority_q[prio_id]},
                                    req_wdata_i, req_wstrb_i);
                priority_q[prio_id] <= pdata[PRIO_BITS-1:0];
              end
            end else if (offset == OFF_SOURCE_MODE) begin
              logic [31:0] new_mode;
              logic [31:0] mode_mask;
              new_mode  = apply_wstrb(source_mode_q, req_wdata_i, req_wstrb_i);
              mode_mask = (NUM_SOURCES >= 16) ? 32'hFFFF_FFFF :
                          ((32'h1 << (NUM_SOURCES*2)) - 32'h1);
              for (int mi = 1; mi <= NUM_SOURCES; mi = mi + 1) begin
                if (new_mode[((mi-1)*2) +: 2] != source_mode_q[((mi-1)*2) +: 2]) begin
                  if (!enable_q[mi] && !in_service_q[mi]) begin
                    pending_q[mi]      <= 1'b0;
                    prev_level_q[mi-1] <= irq_level[mi-1];
                  end
                end
              end
              source_mode_q <= new_mode & mode_mask;
            end else if (offset == OFF_ENABLE) begin
              logic [31:0] edata;
              edata = apply_wstrb({15'b0, enable_q, 1'b0}, req_wdata_i, req_wstrb_i);
              enable_q <= edata[NUM_SOURCES:1];
            end else if (offset == OFF_THRESHOLD) begin
              logic [31:0] tdata;
              tdata = apply_wstrb({{(32-PRIO_BITS){1'b0}}, threshold_q},
                                  req_wdata_i, req_wstrb_i);
              threshold_q <= tdata[PRIO_BITS-1:0];
            end else if (offset == OFF_COMPLETE) begin
              integer cid;
              cid = req_wdata_i[SRC_BITS-1:0];
              if ((cid >= 1) && (cid <= NUM_SOURCES) && in_service_q[cid]) begin
                in_service_q[cid] <= 1'b0;
                if (level_active[cid]) begin
                  pending_q[cid] <= 1'b1;
                end
              end
            end
            // CAPABILITY / PENDING / INPUT_STATE / RESET_LEVEL / CLAIM writes: WI
          end
        end
      end
    end
  end
endmodule
