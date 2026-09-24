`timescale 1ns/1ps
`include "uBee_defs.svh"
`include "uBee_perf_events.svh"

// Instruction-retire-aligned performance events for uBee_monitor.
module uBee_perf_probe #(
  parameter bit MUL_ENABLE = 1'b1,
  parameter bit DIV_ENABLE = 1'b1
) (
  input  logic               clk_i,
  input  logic               rst_ni,

  input  logic               pipeline_advance_i,
  input  logic               decode_stall_i,
  input  logic               execute_stall_i,
  input  logic               memory_stall_i,
  input  logic               load_use_stall_i,
  input  logic [1:0]         forward_rs1_select_i,
  input  logic [1:0]         forward_rs2_select_i,
  input  logic               presented_valid_i,
  input  logic               fetch_queue_redirect_valid_i,
  input  logic               pipeline_flush_redirect_i,
  input  logic               prediction_redirect_valid_i,
  input  logic               exception_flush_i,
  input  logic               fetch_buffer_consume_i,

  input  logic               imem_req_valid_i,
  input  logic               imem_req_ready_i,
  input  logic               fetch_pending_i,
  input  logic               fetch_response_fire_i,

  /* verilator lint_off UNUSEDSIGNAL */
  input  logic               dmem_response_fire_i,
  input  logic               dmem_rsp_err_i,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic               memory_response_error_i,
  input  logic               fetch_response_err_i,
  input  logic               fetch_discard_i,

  input  logic               mem_wb_valid_i,
  input  logic               mem_wb_exception_valid_i,
  input  logic               mem_wb_memory_valid_i,
  input  logic               mem_wb_memory_write_i,
  /* verilator lint_off UNUSEDSIGNAL */
  input  logic [31:0]        mem_wb_instruction_i,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic               mem_wb_branch_valid_i,
  input  logic               mem_wb_branch_taken_i,
  input  logic               mem_wb_prediction_correct_i,
  input  logic               mem_wb_prediction_mispredict_i,

  input  logic               id_ex_valid_i,
  input  logic [2:0]         id_ex_branch_operation_i,
  input  logic               id_ex_jump_i,
  input  logic               recovery_redirect_valid_i,
  input  logic               architectural_redirect_valid_i,
  input  logic               interrupt_trap_valid_i,

  output uBee_perf_events_t  events_o
);
  logic retire;
  logic is_branch_opcode;
  logic is_load_opcode;
  logic is_store_opcode;
  logic is_lui_opcode;
  logic is_auipc_opcode;
  logic is_op_imm_opcode;
  logic is_op_opcode;
  logic is_jal_opcode;
  logic is_jalr_opcode;
  logic is_system_opcode;
  logic is_misc_mem_opcode;
  logic is_m_extension;
  logic mul_funct3;
  logic div_funct3;
  logic redirect_or_exception;
  logic branch_stall_arm_q;
  logic branch_stall_active_q;
  logic branch_stall_start;
  logic branch_stall_cancel;
  logic imem_bus_error_pulse;
  logic dmem_bus_error_pulse;

  assign retire = mem_wb_valid_i && !mem_wb_exception_valid_i;

  assign is_branch_opcode = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_BRANCH;
  assign is_load_opcode   = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_LOAD;
  assign is_store_opcode  = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_STORE;
  assign is_lui_opcode    = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_LUI;
  assign is_auipc_opcode  = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_AUIPC;
  assign is_op_imm_opcode = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_OP_IMM;
  assign is_op_opcode     = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_OP;
  assign is_jal_opcode    = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_JAL;
  assign is_jalr_opcode   = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_JALR;
  assign is_system_opcode = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_SYSTEM;
  assign is_misc_mem_opcode = mem_wb_instruction_i[6:0] == `UBEE_OPCODE_MISC_MEM;
  assign is_m_extension = is_op_opcode &&
    (mem_wb_instruction_i[31:25] == 7'b0000001);

  assign mul_funct3 = MUL_ENABLE && is_m_extension && !mem_wb_instruction_i[14];
  assign div_funct3 = DIV_ENABLE && is_m_extension && mem_wb_instruction_i[14];

  assign redirect_or_exception = fetch_queue_redirect_valid_i || exception_flush_i;

  // Branch stall counts recovery bubbles after a mispredict until fetch consumes.
  assign branch_stall_start =
    recovery_redirect_valid_i &&
    id_ex_valid_i &&
    (id_ex_branch_operation_i != `UBEE_BRANCH_NONE) &&
    !id_ex_jump_i;

  assign branch_stall_cancel =
    architectural_redirect_valid_i &&
    !recovery_redirect_valid_i;

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      branch_stall_arm_q   <= 1'b0;
      branch_stall_active_q <= 1'b0;
    end else begin
      if (branch_stall_cancel) begin
        branch_stall_arm_q    <= 1'b0;
        branch_stall_active_q <= 1'b0;
      end else if (branch_stall_arm_q) begin
        branch_stall_arm_q    <= 1'b0;
        branch_stall_active_q <= 1'b1;
      end else if (branch_stall_start) begin
        branch_stall_arm_q <= 1'b1;
      end else if (branch_stall_active_q && fetch_buffer_consume_i) begin
        branch_stall_active_q <= 1'b0;
      end
    end
  end

  assign imem_bus_error_pulse =
    fetch_response_fire_i && !fetch_discard_i && fetch_response_err_i;
  assign dmem_bus_error_pulse = memory_response_error_i;

  always_comb begin
    events_o = '0;

    events_o.retired = retire;
    events_o.stall = memory_stall_i || decode_stall_i || execute_stall_i;
    events_o.flush = pipeline_flush_redirect_i || exception_flush_i;
    events_o.spec_redirect = prediction_redirect_valid_i;
    events_o.bubble = pipeline_advance_i && !redirect_or_exception &&
      ((decode_stall_i && !execute_stall_i) || execute_stall_i ||
       (!decode_stall_i && !execute_stall_i && !presented_valid_i));
    events_o.hazard = load_use_stall_i ||
      (forward_rs1_select_i != `UBEE_FORWARD_NONE) ||
      (forward_rs2_select_i != `UBEE_FORWARD_NONE);
    events_o.load_use = pipeline_advance_i && load_use_stall_i;
    events_o.branch = retire && is_branch_opcode;
    events_o.branch_taken = retire &&
      mem_wb_branch_valid_i && mem_wb_branch_taken_i;
    events_o.imem_wait = (imem_req_valid_i && !imem_req_ready_i) ||
      (fetch_pending_i && !fetch_response_fire_i);
    events_o.dmem_wait = memory_stall_i;
    events_o.bus_error = imem_bus_error_pulse || dmem_bus_error_pulse;
    events_o.irq_taken = interrupt_trap_valid_i;
    events_o.alu_use = retire &&
      (is_lui_opcode || is_auipc_opcode || is_op_imm_opcode ||
       (is_op_opcode && !is_m_extension)) &&
      !is_branch_opcode && !is_jal_opcode && !is_jalr_opcode &&
      !is_load_opcode && !is_store_opcode &&
      !is_system_opcode && !is_misc_mem_opcode;
    events_o.mul_use = retire && is_m_extension && mul_funct3;
    events_o.div_use = retire && is_m_extension && div_funct3;
    events_o.load_count = retire && mem_wb_memory_valid_i &&
      !mem_wb_memory_write_i;
    events_o.store_count = retire && mem_wb_memory_valid_i &&
      mem_wb_memory_write_i;
    events_o.branch_stall = branch_stall_active_q && !fetch_buffer_consume_i;
    events_o.mem_wait_total = events_o.imem_wait || events_o.dmem_wait;
    events_o.prediction_correct = retire && mem_wb_prediction_correct_i;
    events_o.mispredict = retire && mem_wb_prediction_mispredict_i;
  end
endmodule
