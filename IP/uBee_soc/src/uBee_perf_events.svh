// Core performance event bundle (one cycle snapshot). Counting semantics are
// defined in the TRM MONITOR chapter and uBee_perf_probe.sv.
`ifndef UBEE_PERF_EVENTS_SVH
`define UBEE_PERF_EVENTS_SVH

typedef struct packed {
  logic retired;
  logic stall;
  logic flush;
  logic bubble;
  logic hazard;
  logic load_use;
  logic branch;
  logic branch_taken;
  logic imem_wait;
  logic dmem_wait;
  logic axi_wait;
  logic bus_error;
  logic irq_taken;
  logic alu_use;
  logic mul_use;
  logic div_use;
  logic load_count;
  logic store_count;
  logic branch_stall;
  logic mem_wait_total;
  logic prediction_correct;
  logic mispredict;
  logic spec_redirect;
} uBee_perf_events_t;

`define UBEE_PERF_EVT_WIDTH $bits(uBee_perf_events_t)

`endif /* UBEE_PERF_EVENTS_SVH */
