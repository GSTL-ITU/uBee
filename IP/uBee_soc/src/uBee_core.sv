`timescale 1ns/1ps
`include "uBee_defs.svh"
`include "uBee_perf_events.svh"

module uBee_core #(
  parameter logic [31:0] RESET_VECTOR = 32'h8000_0000,
  parameter logic [31:0] MTVEC_RESET = 32'h8000_0000,
  parameter bit SHADOW_BANK_ENABLE = 1'b1,
  parameter bit MUL_ENABLE = 1'b1,
  parameter bit DIV_ENABLE = 1'b1,
  parameter bit BP_ENABLE = 1'b1,
  parameter bit DEBUG_ENABLE = 1'b1,
  parameter logic [31:0] DMEM_BASE = 32'h2000_0000,
  parameter logic [31:0] DMEM_MASK = 32'hFFFF_0000,
  parameter int unsigned BP_ENTRIES = 128,
  parameter bit BP_GSHARE = 1'b0,
  parameter int unsigned GHR_WIDTH = 5
) (
  input  logic        clk_i,
  input  logic        rst_ni,

  output logic        imem_req_valid_o,
  input  logic        imem_req_ready_i,
  output logic [31:0] imem_req_addr_o,
  input  logic        imem_rsp_valid_i,
  output logic        imem_rsp_ready_o,
  input  logic [31:0] imem_rsp_rdata_i,
  input  logic        imem_rsp_err_i,

  output logic        dmem_req_valid_o,
  input  logic        dmem_req_ready_i,
  output logic        dmem_req_write_o,
  output logic [31:0] dmem_req_addr_o,
  output logic [31:0] dmem_req_wdata_o,
  output logic [3:0]  dmem_req_wstrb_o,
  input  logic        dmem_rsp_valid_i,
  output logic        dmem_rsp_ready_o,
  input  logic [31:0] dmem_rsp_rdata_i,
  input  logic        dmem_rsp_err_i,

  input  logic        irq_software_i,
  input  logic        irq_timer_i,
  input  logic        irq_external_i,

  output logic        trace_valid_o,
  output logic [31:0] trace_pc_o,
  output logic [31:0] trace_instruction_o,
  output logic        trace_rd_write_o,
  output logic [4:0]  trace_rd_addr_o,
  output logic [31:0] trace_rd_data_o,
  output logic        trace_mem_valid_o,
  output logic        trace_mem_write_o,
  output logic [31:0] trace_mem_addr_o,
  output logic [31:0] trace_mem_wdata_o,
  output logic [3:0]  trace_mem_wstrb_o,
  output logic        trace_trap_o,
  output logic [31:0] trace_cause_o,

  // Continuous debug observe (always present; student BD waveform)
  output logic [31:0] dbg_fetch_pc_o,
  output logic [31:0] dbg_if_pc_o,
  output logic        dbg_if_valid_o,
  output logic [31:0] dbg_ex_pc_o,
  output logic        dbg_ex_valid_o,
  output logic [31:0] dbg_mem_pc_o,
  output logic        dbg_mem_valid_o,
  output logic [31:0] dbg_wb_pc_o,
  output logic        dbg_wb_valid_o,
  output logic        dbg_rf_we_o,
  output logic [4:0]  dbg_rf_waddr_o,
  output logic [31:0] dbg_rf_wdata_o,
  output logic [31:0] dbg_csr_mstatus_o,
  output logic [31:0] dbg_csr_mie_o,
  output logic [31:0] dbg_csr_mtvec_o,
  output logic [31:0] dbg_csr_mscratch_o,
  output logic [31:0] dbg_csr_mepc_o,
  output logic [31:0] dbg_csr_mcause_o,
  output logic [31:0] dbg_csr_mtval_o,
  output logic [31:0] dbg_csr_mip_o,
  output logic [31:0] dbg_csr_mcycle_o,
  output logic [31:0] dbg_csr_mcycleh_o,
  output logic [31:0] dbg_csr_minstret_o,
  output logic [31:0] dbg_csr_minstreth_o,

  output uBee_perf_events_t perf_events_o
);
  // ----------------------------------------------------------------------
  // Instruction fetch — 48-bit fetch queue with 16-bit granularity (RVC).
  //
  // fq_data_q[47:0]  : 3 half-words (hw0=lsb, hw2=msb)
  // fq_valid_q[2:0]  : valid bits per half-word
  // fq_err_q[2:0]    : error bits per half-word
  // fq_pc_q          : PC of fq_data_q[15:0] (hw0)
  //
  // An instruction is always sourced from hw0 (fq_data_q[15:0]):
  //   hw0[1:0] != 2'b11 → compressed 16-bit → consume 1 HW, shift left by 1
  //   hw0[1:0] == 2'b11 → 32-bit → consume 2 HW, shift left by 2
  //
  // Each IMEM response fills hw1:hw0 or hw2:hw1 depending on occupancy.
  // The fetch PC advances in 4-byte steps; bit[1] of the initial PC sets the
  // starting half-word offset (for redirects to 2-byte aligned targets).
  // ----------------------------------------------------------------------

  // IMEM request / response wires
  logic        fetch_request_fire;
  logic        fetch_response_fire;

  // Fetch queue
  logic [47:0] fq_data_q;      // [15:0]=hw0, [31:16]=hw1, [47:32]=hw2
  logic [2:0]  fq_valid_q;
  logic [2:0]  fq_err_q;
  logic [31:0] fq_pc_q;        // PC of hw0

  // Fetch address (always 4-byte aligned)
  logic [31:0] fetch_pc_q;
  logic        fetch_pending_q;
  logic        fetch_discard_q;

  // "Presented" instruction to the IF/ID stage
  logic [31:0] presented_word;
  logic [31:0] presented_pc;
  logic        presented_error;
  logic        presented_valid;
  logic        presented_is_compressed;  // combinatorial shortcut

  // Decoded/decompressed outputs
  logic [31:0] fetch_buffer_decompressed;
  logic        fetch_buffer_is_compressed;

  // fetch_buffer_consume: true when the front-end issues an instruction
  logic        fetch_buffer_consume;

  // IF/ID state.
  logic        if_id_valid_q;
  logic [31:0] if_id_pc_q;
  logic [31:0] if_id_instruction_q;
  logic        if_id_fetch_error_q;
  logic        if_id_is_compressed_q;  // C extension: 1 if current instr was 16-bit
  logic        if_id_predicted_taken_q;
  logic [$clog2(BP_ENTRIES)-1:0] if_id_bp_index_q;

  // Decoder outputs and decode-stage exception classification.
  logic        decode_illegal;
  logic        decode_uses_rs1;
  logic        decode_uses_rs2;
  logic        decode_rd_write;
  logic [2:0]  decode_immediate_format;
  logic [4:0]  decode_alu_operation;
  logic [1:0]  decode_operand_a_select;
  logic        decode_operand_b_select;
  logic [2:0]  decode_branch_operation;
  logic        decode_jump;
  logic        decode_jump_register;
  logic        decode_memory_read;
  logic        decode_memory_write;
  logic [1:0]  decode_memory_size;
  logic        decode_memory_unsigned;
  logic [2:0]  decode_writeback_select;
  logic        decode_fence;
  logic [3:0]  decode_system_operation;
  logic [11:0] decode_csr_address;
  logic [31:0] decode_immediate;
  logic [31:0] decode_rs1_data;
  logic [31:0] decode_rs2_data;
  logic [31:0] decode_rs1_value;
  logic [31:0] decode_rs2_value;
  logic        decode_instruction_valid;
  /* verilator lint_off UNUSEDSIGNAL */
  logic        decode_csr_known; // decoder output, not used (unknown CSRs treated as NOP)
  /* verilator lint_on UNUSEDSIGNAL */
  logic        decode_exception_valid;
  logic [31:0] decode_exception_cause;
  logic [31:0] decode_exception_value;
  logic        decode_serializing;

  // ID/EX state.
  logic        id_ex_valid_q;
  logic [31:0] id_ex_pc_q;
  logic [31:0] id_ex_instruction_q;
  logic [31:0] id_ex_pc_next_q;
  logic [31:0] id_ex_rs1_data_q;
  logic [31:0] id_ex_rs2_data_q;
  logic [4:0]  id_ex_rs1_addr_q;
  logic [4:0]  id_ex_rs2_addr_q;
  logic        id_ex_uses_rs1_q;
  logic        id_ex_uses_rs2_q;
  logic [31:0] id_ex_immediate_q;
  logic [4:0]  id_ex_rd_addr_q;
  logic        id_ex_rd_write_q;
  logic [4:0]  id_ex_alu_operation_q;
  logic [1:0]  id_ex_operand_a_select_q;
  logic        id_ex_operand_b_select_q;
  logic [2:0]  id_ex_branch_operation_q;
  logic        id_ex_jump_q;
  logic        id_ex_jump_register_q;
  logic        id_ex_memory_read_q;
  logic        id_ex_memory_write_q;
  logic [1:0]  id_ex_memory_size_q;
  logic        id_ex_memory_unsigned_q;
  logic [2:0]  id_ex_writeback_select_q;
  logic [3:0]  id_ex_system_operation_q;
  logic [11:0] id_ex_csr_address_q;
  logic        id_ex_exception_valid_q;
  logic [31:0] id_ex_exception_cause_q;
  logic [31:0] id_ex_exception_value_q;
  logic        id_ex_serializing_q;
  logic        id_ex_predicted_taken_q;
  logic [$clog2(BP_ENTRIES)-1:0] id_ex_bp_index_q;

  // Execute values, forwarding, redirect and exception classification.
  logic [31:0] execute_rs1_value;
  logic [31:0] execute_rs2_value;
  logic [31:0] execute_operand_a;
  logic [31:0] execute_operand_b;
  logic [31:0] execute_alu_result;
  logic        execute_branch_taken;
  logic        execute_control_taken;
  logic [31:0] execute_control_target;
  logic        execute_target_misaligned;
  logic        execute_memory_misaligned;
  logic        execute_new_exception_valid;
  logic [31:0] execute_new_exception_cause;
  logic [31:0] execute_new_exception_value;
  logic        execute_exception_valid;
  logic [31:0] execute_exception_cause;
  logic [31:0] execute_exception_value;
  logic        execute_exception_flush;
  logic        execute_jump_redirect_valid;
  logic        execute_branch_mispredict;
  logic        recovery_redirect_valid;
  logic [31:0] recovery_redirect_target;
  logic        prediction_redirect_valid;
  logic [31:0] prediction_redirect_target;
  logic        fetch_queue_redirect_valid;
  logic        pipeline_flush_redirect;
  logic [1:0]  forward_rs1_select;
  logic [1:0]  forward_rs2_select;
  logic [31:0] ex_mem_forward_value;
  logic        load_use_stall;
  logic [31:0] execute_csr_operand;
  logic [31:0] execute_csr_read_data;
  logic        execute_csr_access_valid;
  logic        execute_csr_write_enable;
  logic [31:0] execute_csr_write_data;

  // EX/MEM state.
  logic        ex_mem_valid_q;
  logic [31:0] ex_mem_pc_q;
  logic [31:0] ex_mem_instruction_q;
  logic [31:0] ex_mem_pc_next_q;
  logic [31:0] ex_mem_next_arch_pc_q;
  logic [31:0] ex_mem_alu_result_q;
  logic [31:0] ex_mem_store_data_q;
  logic [4:0]  ex_mem_rd_addr_q;
  logic        ex_mem_rd_write_q;
  logic        ex_mem_memory_read_q;
  logic        ex_mem_memory_write_q;
  logic [1:0]  ex_mem_memory_size_q;
  logic        ex_mem_memory_unsigned_q;
  logic [2:0]  ex_mem_writeback_select_q;
  logic [3:0]  ex_mem_system_operation_q;
  logic [11:0] ex_mem_csr_address_q;
  logic [31:0] ex_mem_csr_read_data_q;
  logic        ex_mem_csr_write_enable_q;
  logic [31:0] ex_mem_csr_write_data_q;
  logic        ex_mem_exception_valid_q;
  logic [31:0] ex_mem_exception_cause_q;
  logic [31:0] ex_mem_exception_value_q;
  logic        ex_mem_serializing_q;
  logic        ex_mem_branch_valid_q;
  logic        ex_mem_branch_taken_q;
  logic        ex_mem_prediction_correct_q;
  logic        ex_mem_prediction_mispredict_q;

  // Data-memory transaction and load/store formatting.
  logic        dmem_pending_q;
  logic        dmem_request_fire;
  logic        dmem_response_fire;
  logic        memory_operation;
  logic        memory_stall;
  logic        local_dmem_hit;
  logic        local_dmem_store;
  logic        posted_store_accept;
  logic        posted_valid_q;
  logic [31:0] posted_pc_q;
  logic [31:0] posted_addr_q;
  logic        posted_wb_hold;
  logic        posted_rsp_err;
  logic        memory_response_error;
  logic        memory_exception_flush;
  logic [31:0] load_shifted_data;
  logic [31:0] load_result;

  // MEM/WB state.
  logic        mem_wb_valid_q;
  logic [31:0] mem_wb_pc_q;
  logic [31:0] mem_wb_instruction_q;
  logic [31:0] mem_wb_next_arch_pc_q;
  logic [4:0]  mem_wb_rd_addr_q;
  logic        mem_wb_rd_write_q;
  logic [31:0] mem_wb_result_q;
  logic        mem_wb_memory_valid_q;
  logic        mem_wb_memory_write_q;
  logic [31:0] mem_wb_memory_addr_q;
  logic [31:0] mem_wb_memory_wdata_q;
  logic [3:0]  mem_wb_memory_wstrb_q;
  logic [3:0]  mem_wb_system_operation_q;
  logic [11:0] mem_wb_csr_address_q;
  logic        mem_wb_csr_write_enable_q;
  logic [31:0] mem_wb_csr_write_data_q;
  logic        mem_wb_exception_valid_q;
  logic [31:0] mem_wb_exception_cause_q;
  logic [31:0] mem_wb_exception_value_q;
  logic        mem_wb_serializing_q;
  logic        mem_wb_branch_valid_q;
  logic        mem_wb_branch_taken_q;
  logic        mem_wb_prediction_correct_q;
  logic        mem_wb_prediction_mispredict_q;

  // CSR/trap commit controls and pipeline-wide flow controls.
  logic        csr_write_commit;
  logic        csr_mret_commit;
  logic        csr_instruction_retired;
  logic [31:0] csr_trap_vector;
  logic [31:0] csr_mepc;
  logic        csr_interrupt_request;
  logic [31:0] csr_interrupt_cause;
  logic        csr_shadow_bank;
  logic        rf_write_enable;
  logic        synchronous_trap_valid;
  logic        interrupt_trap_valid;
  logic        csr_trap_enter;
  logic        csr_trap_interrupt;
  logic [31:0] csr_trap_cause;
  logic [31:0] csr_trap_value;
  logic [31:0] csr_trap_epc;
  logic        architectural_redirect_valid;
  logic [31:0] architectural_redirect_target;
  logic        fetch_redirect_valid;
  logic [31:0] fetch_redirect_target;
  logic        pipeline_serializing_busy;
  logic        decode_stall;
  logic        execute_stall;
  logic        pipeline_advance;
  logic        front_end_advance;
  logic        pipeline_empty;
  logic        exception_flush;

  // Multiply/divide EX stall controls.
  logic        execute_is_mul;
  logic        execute_is_div;
  logic        muldiv_pending;
  logic        muldiv_done;
  logic        muldiv_start;
  logic        muldiv_started_q;
  logic        muldiv_flush;
  logic        muldiv_result_ack;
  logic [31:0] alu_result;
  logic [31:0] mul_result;
  logic [31:0] div_result;
  logic        mul_done;
  logic        div_done;
  logic        mul_busy;
  logic        div_busy;

  // Interrupts are boundary events and therefore use a one-entry trace pulse.
  logic        interrupt_trace_pending_q;
  logic [31:0] interrupt_trace_pc_q;
  logic [31:0] interrupt_trace_cause_q;

  // ----------------------------------------------------------------------
  // Decode, CSR and register-file units.
  // ----------------------------------------------------------------------
  uBee_decoder #(
    .MUL_ENABLE (MUL_ENABLE),
    .DIV_ENABLE (DIV_ENABLE)
  ) u_decoder (
    .instruction_i       (if_id_instruction_q),
    .illegal_o           (decode_illegal),
    .uses_rs1_o          (decode_uses_rs1),
    .uses_rs2_o          (decode_uses_rs2),
    .rd_write_o          (decode_rd_write),
    .immediate_format_o  (decode_immediate_format),
    .alu_operation_o     (decode_alu_operation),
    .operand_a_select_o  (decode_operand_a_select),
    .operand_b_select_o  (decode_operand_b_select),
    .branch_operation_o  (decode_branch_operation),
    .jump_o              (decode_jump),
    .jump_register_o     (decode_jump_register),
    .memory_read_o       (decode_memory_read),
    .memory_write_o      (decode_memory_write),
    .memory_size_o       (decode_memory_size),
    .memory_unsigned_o   (decode_memory_unsigned),
    .writeback_select_o  (decode_writeback_select),
    .fence_o             (decode_fence),
    .system_operation_o  (decode_system_operation),
    .csr_address_o       (decode_csr_address)
  );

  uBee_immediate_gen u_immediate_gen (
    .instruction_i (if_id_instruction_q),
    .format_i      (decode_immediate_format),
    .immediate_o   (decode_immediate)
  );

  assign rf_write_enable = mem_wb_valid_q && mem_wb_rd_write_q &&
                           !mem_wb_exception_valid_q;

  uBee_regfile #(
    .SHADOW_BANK_ENABLE (SHADOW_BANK_ENABLE)
  ) u_regfile (
    .clk_i          (clk_i),
    .bank_select_i  (csr_shadow_bank),
    .rs1_addr_i     (if_id_instruction_q[19:15]),
    .rs1_data_o     (decode_rs1_data),
    .rs2_addr_i     (if_id_instruction_q[24:20]),
    .rs2_data_o     (decode_rs2_data),
    .rd_write_i     (rf_write_enable),
    .rd_addr_i      (mem_wb_rd_addr_q),
    .rd_data_i      (mem_wb_result_q)
  );

  logic [31:0] dbg_csr_mstatus_w;
  logic [31:0] dbg_csr_mie_w;
  logic [31:0] dbg_csr_mtvec_w;
  logic [31:0] dbg_csr_mscratch_w;
  logic [31:0] dbg_csr_mepc_w;
  logic [31:0] dbg_csr_mcause_w;
  logic [31:0] dbg_csr_mtval_w;
  logic [31:0] dbg_csr_mip_w;
  logic [31:0] dbg_csr_mcycle_w;
  logic [31:0] dbg_csr_mcycleh_w;
  logic [31:0] dbg_csr_minstret_w;
  logic [31:0] dbg_csr_minstreth_w;

  uBee_csr_trap #(
    .MTVEC_RESET        (MTVEC_RESET),
    .SHADOW_BANK_ENABLE (SHADOW_BANK_ENABLE)
  ) u_csr_trap (
    .clk_i                 (clk_i),
    .rst_ni                (rst_ni),
    .csr_check_addr_i      (decode_csr_address),
    .csr_check_valid_o     (decode_csr_known),
    .csr_access_addr_i     (id_ex_csr_address_q),
    .csr_access_rdata_o    (execute_csr_read_data),
    .csr_access_valid_o    (execute_csr_access_valid),
    .csr_write_i           (csr_write_commit),
    .csr_write_addr_i      (mem_wb_csr_address_q),
    .csr_write_data_i      (mem_wb_csr_write_data_q),
    .trap_enter_i          (csr_trap_enter),
    .trap_interrupt_i      (csr_trap_interrupt),
    .trap_cause_i          (csr_trap_cause),
    .trap_value_i          (csr_trap_value),
    .trap_epc_i            (csr_trap_epc),
    .mret_i                (csr_mret_commit),
    .instruction_retired_i (csr_instruction_retired),
    .irq_software_i        (irq_software_i),
    .irq_timer_i           (irq_timer_i),
    .irq_external_i        (irq_external_i),
    .trap_vector_o         (csr_trap_vector),
    .mepc_o                (csr_mepc),
    .interrupt_request_o   (csr_interrupt_request),
    .interrupt_cause_o     (csr_interrupt_cause),
    .shadow_bank_o         (csr_shadow_bank),
    .dbg_csr_mstatus_o     (dbg_csr_mstatus_w),
    .dbg_csr_mie_o         (dbg_csr_mie_w),
    .dbg_csr_mtvec_o       (dbg_csr_mtvec_w),
    .dbg_csr_mscratch_o    (dbg_csr_mscratch_w),
    .dbg_csr_mepc_o        (dbg_csr_mepc_w),
    .dbg_csr_mcause_o      (dbg_csr_mcause_w),
    .dbg_csr_mtval_o       (dbg_csr_mtval_w),
    .dbg_csr_mip_o         (dbg_csr_mip_w),
    .dbg_csr_mcycle_o      (dbg_csr_mcycle_w),
    .dbg_csr_mcycleh_o     (dbg_csr_mcycleh_w),
    .dbg_csr_minstret_o    (dbg_csr_minstret_w),
    .dbg_csr_minstreth_o   (dbg_csr_minstreth_w)
  );

  assign decode_instruction_valid = if_id_valid_q;

  always_comb begin
    decode_exception_valid = 1'b0;
    decode_exception_cause = 32'b0;
    decode_exception_value = 32'b0;

    if (if_id_fetch_error_q) begin
      decode_exception_valid = if_id_valid_q;
      decode_exception_cause = `UBEE_CAUSE_INSTRUCTION_ACCESS_FAULT;
      decode_exception_value = if_id_pc_q;
    end else if (decode_illegal) begin
      decode_exception_valid = if_id_valid_q;
      decode_exception_cause = `UBEE_CAUSE_ILLEGAL_INSTRUCTION;
      decode_exception_value = if_id_instruction_q;
    end else if (decode_system_operation == `UBEE_SYSTEM_ECALL) begin
      decode_exception_valid = if_id_valid_q;
      decode_exception_cause = `UBEE_CAUSE_ECALL_MACHINE;
    end else if (decode_system_operation == `UBEE_SYSTEM_EBREAK) begin
      decode_exception_valid = if_id_valid_q;
      decode_exception_cause = `UBEE_CAUSE_BREAKPOINT;
    end
  end

  assign decode_serializing = decode_exception_valid ||
    (decode_system_operation != `UBEE_SYSTEM_NONE);

  // WB-to-ID bypass preserves write-first architectural behavior without
  // complicating the two asynchronous register-file read ports.
  always_comb begin
    decode_rs1_value = decode_rs1_data;
    if (decode_uses_rs1 && (if_id_instruction_q[19:15] != 5'd0) &&
        mem_wb_valid_q && mem_wb_rd_write_q &&
        !mem_wb_exception_valid_q &&
        (if_id_instruction_q[19:15] == mem_wb_rd_addr_q)) begin
      decode_rs1_value = mem_wb_result_q;
    end

    decode_rs2_value = decode_rs2_data;
    if (decode_uses_rs2 && (if_id_instruction_q[24:20] != 5'd0) &&
        mem_wb_valid_q && mem_wb_rd_write_q &&
        !mem_wb_exception_valid_q &&
        (if_id_instruction_q[24:20] == mem_wb_rd_addr_q)) begin
      decode_rs2_value = mem_wb_result_q;
    end
  end

  uBee_hazard_unit u_hazard_unit (
    .decode_valid_i           (decode_instruction_valid && !decode_exception_valid),
    .decode_uses_rs1_i        (decode_uses_rs1),
    .decode_uses_rs2_i        (decode_uses_rs2),
    .decode_rs1_addr_i        (if_id_instruction_q[19:15]),
    .decode_rs2_addr_i        (if_id_instruction_q[24:20]),
    .execute_valid_i          (id_ex_valid_q && !id_ex_exception_valid_q),
    .execute_uses_rs1_i       (id_ex_uses_rs1_q),
    .execute_uses_rs2_i       (id_ex_uses_rs2_q),
    .execute_rs1_addr_i       (id_ex_rs1_addr_q),
    .execute_rs2_addr_i       (id_ex_rs2_addr_q),
    .execute_memory_read_i    (id_ex_memory_read_q),
    .execute_rd_write_i       (id_ex_rd_write_q),
    .execute_rd_addr_i        (id_ex_rd_addr_q),
    .memory_valid_i           (ex_mem_valid_q && !ex_mem_exception_valid_q),
    .memory_rd_write_i        (ex_mem_rd_write_q),
    .memory_read_i            (ex_mem_memory_read_q),
    .memory_rd_addr_i         (ex_mem_rd_addr_q),
    .writeback_valid_i        (mem_wb_valid_q && !mem_wb_exception_valid_q),
    .writeback_rd_write_i     (mem_wb_rd_write_q),
    .writeback_rd_addr_i      (mem_wb_rd_addr_q),
    .load_use_stall_o         (load_use_stall),
    .forward_rs1_o            (forward_rs1_select),
    .forward_rs2_o            (forward_rs2_select)
  );

  always_comb begin
    unique case (ex_mem_writeback_select_q)
      `UBEE_WB_PC_NEXT: ex_mem_forward_value = ex_mem_pc_next_q;
      `UBEE_WB_CSR:     ex_mem_forward_value = ex_mem_csr_read_data_q;
      default:          ex_mem_forward_value = ex_mem_alu_result_q;
    endcase
  end

  always_comb begin
    execute_rs1_value = id_ex_rs1_data_q;
    unique case (forward_rs1_select)
      `UBEE_FORWARD_EX_MEM: execute_rs1_value = ex_mem_forward_value;
      `UBEE_FORWARD_MEM_WB: execute_rs1_value = mem_wb_result_q;
      default: begin end
    endcase

    execute_rs2_value = id_ex_rs2_data_q;
    unique case (forward_rs2_select)
      `UBEE_FORWARD_EX_MEM: execute_rs2_value = ex_mem_forward_value;
      `UBEE_FORWARD_MEM_WB: execute_rs2_value = mem_wb_result_q;
      default: begin end
    endcase

    unique case (id_ex_operand_a_select_q)
      `UBEE_OPERAND_A_PC:   execute_operand_a = id_ex_pc_q;
      `UBEE_OPERAND_A_ZERO: execute_operand_a = 32'b0;
      default:              execute_operand_a = execute_rs1_value;
    endcase

    execute_operand_b = (id_ex_operand_b_select_q == `UBEE_OPERAND_B_IMM)
      ? id_ex_immediate_q
      : execute_rs2_value;
  end

  // -----------------------------------------------------------------------
  // Instruction presentation from the fetch queue.
  //
  // hw0 = fq_data_q[15:0]  is always the head of the queue.
  // hw0[1:0] != 2'b11 → 16-bit compressed (need fq_valid_q[0])
  // hw0[1:0] == 2'b11 → 32-bit            (need fq_valid_q[1:0])
  // -----------------------------------------------------------------------
  assign presented_is_compressed = (fq_data_q[1:0] != 2'b11);

  always_comb begin
    presented_valid = 1'b0;
    presented_word  = 32'b0;
    presented_pc    = fq_pc_q;
    presented_error = fq_err_q[0];

    if (presented_is_compressed) begin
      // 16-bit: only hw0 needed
      if (fq_valid_q[0]) begin
        presented_valid = 1'b1;
        presented_word  = {16'b0, fq_data_q[15:0]};
        presented_error = fq_err_q[0];
      end
    end else begin
      // 32-bit: need hw0 and hw1
      if (fq_valid_q[1:0] == 2'b11) begin
        presented_valid = 1'b1;
        presented_word  = {fq_data_q[31:16], fq_data_q[15:0]};
        presented_error = fq_err_q[0] | fq_err_q[1];
      end
    end
  end

  uBee_decompress u_decompress (
    .instr_i          (presented_word),
    .instr_o          (fetch_buffer_decompressed),
    .is_compressed_o  (fetch_buffer_is_compressed)
  );

  localparam int BP_bit_size = $clog2(BP_ENTRIES);
  // BP_GSHARE=0: PC-fold bimodal. BP_GSHARE=1: index = pc_fold XOR zero-ext(GHR).
  localparam bit BP_USE_GSHARE = BP_GSHARE;
  // Branch prediction lookup at the presented-instruction boundary.
  logic        presented_is_branch;
  logic [31:0] presented_branch_imm;
  logic [BP_bit_size-1:0]  bp_lookup_index;
  logic        bp_predict_taken;
  logic        bp_update_valid;
  logic [BP_bit_size-1:0]  bp_update_index;
  logic        bp_update_taken;
  logic        execute_prediction_correct;
  logic        execute_prediction_mispredict;
  logic [BP_bit_size-1:0] bp_pc_hash_lookup;
  logic [BP_bit_size-1:0] bp_pc_hash_update;

  assign presented_is_branch = presented_valid &&
    (fetch_buffer_decompressed[6:0] == `UBEE_OPCODE_BRANCH);

  always_comb begin
    presented_branch_imm = {
      {19{fetch_buffer_decompressed[31]}},
      fetch_buffer_decompressed[31],
      fetch_buffer_decompressed[7],
      fetch_buffer_decompressed[30:25],
      fetch_buffer_decompressed[11:8],
      1'b0
    };
  end

  assign bp_pc_hash_lookup =
    presented_pc[2*BP_bit_size : BP_bit_size+1] ^ presented_pc[BP_bit_size:1];
  assign bp_pc_hash_update =
    id_ex_pc_q[2*BP_bit_size : BP_bit_size+1] ^ id_ex_pc_q[BP_bit_size:1];

  if (BP_USE_GSHARE) begin : g_bp_gshare
    // GHR_WIDTH must satisfy 1 <= GHR_WIDTH <= BP_bit_size (sweep: 2..5 for BP_ENTRIES=32).
    if (GHR_WIDTH < 1 || GHR_WIDTH > BP_bit_size) begin : g_bp_ghr_width_bad
      $error("GHR_WIDTH (%0d) must be in [1, BP_bit_size=%0d] when BP_GSHARE=1",
             GHR_WIDTH, BP_bit_size);
    end

    logic [GHR_WIDTH-1:0] ghr_q;
    logic [BP_bit_size-1:0] bp_ghr_pad;

    assign bp_ghr_pad = BP_bit_size'(ghr_q);
    assign bp_lookup_index = bp_pc_hash_lookup ^ bp_ghr_pad;
    assign bp_update_index = id_ex_bp_index_q;

    always_ff @(posedge clk_i) begin
      if (!rst_ni) begin
        ghr_q <= '0;
      end else if (BP_ENABLE && bp_update_valid) begin
        // Shift in resolved taken/not-taken at LSB (works for GHR_WIDTH >= 1).
        ghr_q <= {ghr_q, bp_update_taken}[GHR_WIDTH-1:0];
      end
    end
  end else begin : g_bp_bimodal
    assign bp_lookup_index = bp_pc_hash_lookup;
    assign bp_update_index = bp_pc_hash_update;
  end

  uBee_branch_predictor #(
    .BP_ENABLE (BP_ENABLE),
    .BP_ENTRIES (BP_ENTRIES),
    .BP_BIT_SIZE (BP_bit_size)
  ) u_branch_predictor (
    .clk_i           (clk_i),
    .rst_ni          (rst_ni),
    .lookup_index_i  (bp_lookup_index),
    .predict_taken_o (bp_predict_taken),
    .update_valid_i  (bp_update_valid),
    .update_index_i  (bp_update_index),
    .update_taken_i  (bp_update_taken)
  );

  assign prediction_redirect_target = presented_pc + presented_branch_imm;
  assign prediction_redirect_valid = BP_ENABLE && front_end_advance &&
    !pipeline_flush_redirect && !exception_flush &&
    presented_is_branch && bp_predict_taken;

  uBee_alu u_alu (
    .operand_a_i (execute_operand_a),
    .operand_b_i (execute_operand_b),
    .operation_i (id_ex_alu_operation_q),
    .result_o    (alu_result)
  );

  if (MUL_ENABLE) begin : g_mul
    uBee_mul u_mul (
      .clk_i         (clk_i),
      .rst_ni        (rst_ni),
      .start_i       (muldiv_start && execute_is_mul),
      .flush_i       (muldiv_flush),
      .result_ack_i  (muldiv_result_ack && execute_is_mul),
      .operation_i   (id_ex_alu_operation_q),
      .operand_a_i   (execute_operand_a),
      .operand_b_i   (execute_operand_b),
      .busy_o        (mul_busy),
      .done_o        (mul_done),
      .result_o      (mul_result)
    );
  end else begin : g_no_mul
    assign mul_busy   = 1'b0;
    assign mul_done   = 1'b0;
    assign mul_result = 32'b0;
  end

  if (DIV_ENABLE) begin : g_div
    uBee_div u_div (
      .clk_i         (clk_i),
      .rst_ni        (rst_ni),
      .start_i       (muldiv_start && execute_is_div),
      .flush_i       (muldiv_flush),
      .result_ack_i  (muldiv_result_ack && execute_is_div),
      .operation_i   (id_ex_alu_operation_q),
      .operand_a_i   (execute_operand_a),
      .operand_b_i   (execute_operand_b),
      .busy_o        (div_busy),
      .done_o        (div_done),
      .result_o      (div_result)
    );
  end else begin : g_no_div
    assign div_busy   = 1'b0;
    assign div_done   = 1'b0;
    assign div_result = 32'b0;
  end

  assign execute_is_mul = (id_ex_alu_operation_q >= `UBEE_ALU_MUL) &&
                          (id_ex_alu_operation_q <= `UBEE_ALU_MULHU);
  assign execute_is_div = (id_ex_alu_operation_q >= `UBEE_ALU_DIV) &&
                          (id_ex_alu_operation_q <= `UBEE_ALU_REMU);
  assign muldiv_pending = id_ex_valid_q && !id_ex_exception_valid_q &&
                          (execute_is_mul || execute_is_div);
  assign muldiv_done = execute_is_mul ? mul_done : div_done;
  assign muldiv_flush = exception_flush || pipeline_flush_redirect;
  assign muldiv_start = muldiv_pending && !muldiv_started_q && !muldiv_done;
  assign execute_stall = muldiv_pending && !muldiv_done;
  assign muldiv_result_ack = pipeline_advance && !execute_stall &&
                             muldiv_pending && muldiv_done;
  assign execute_alu_result = muldiv_pending
    ? (execute_is_mul ? mul_result : div_result)
    : alu_result;

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      muldiv_started_q <= 1'b0;
    end else if (muldiv_flush) begin
      muldiv_started_q <= 1'b0;
    end else if (muldiv_start) begin
      muldiv_started_q <= 1'b1;
    end else if (muldiv_result_ack) begin
      muldiv_started_q <= 1'b0;
    end
  end

  uBee_branch_compare u_branch_compare (
    .operand_a_i (execute_rs1_value),
    .operand_b_i (execute_rs2_value),
    .operation_i (id_ex_branch_operation_q),
    .taken_o     (execute_branch_taken)
  );

  assign execute_control_taken = id_ex_jump_q ||
    ((id_ex_branch_operation_q != `UBEE_BRANCH_NONE) && execute_branch_taken);
  assign execute_control_target = id_ex_jump_register_q
    ? {execute_alu_result[31:1], 1'b0}
    : execute_alu_result;
  // With C extension, jump targets need only be 16-bit (2-byte) aligned.
  // Only bit[0] set indicates a genuine misaligned target.
  assign execute_target_misaligned = execute_control_taken &&
    execute_control_target[0];

  always_comb begin
    execute_memory_misaligned = 1'b0;
    if (id_ex_memory_read_q || id_ex_memory_write_q) begin
      unique case (id_ex_memory_size_q)
        `UBEE_MEMORY_HALF: execute_memory_misaligned = execute_alu_result[0];
        `UBEE_MEMORY_WORD: execute_memory_misaligned =
          (execute_alu_result[1:0] != 2'b00);
        default: execute_memory_misaligned = 1'b0;
      endcase
    end
  end

  always_comb begin
    execute_new_exception_valid = 1'b0;
    execute_new_exception_cause = 32'b0;
    execute_new_exception_value = 32'b0;

    if (id_ex_valid_q && !id_ex_exception_valid_q &&
        execute_target_misaligned) begin
      execute_new_exception_valid = 1'b1;
      execute_new_exception_cause = `UBEE_CAUSE_INSTRUCTION_ADDRESS_MISALIGNED;
      execute_new_exception_value = execute_control_target;
    end else if (id_ex_valid_q && !id_ex_exception_valid_q &&
                 execute_memory_misaligned) begin
      execute_new_exception_valid = 1'b1;
      execute_new_exception_cause = id_ex_memory_write_q
        ? `UBEE_CAUSE_STORE_ADDRESS_MISALIGNED
        : `UBEE_CAUSE_LOAD_ADDRESS_MISALIGNED;
      execute_new_exception_value = execute_alu_result;
    end
  end

  assign execute_exception_valid = id_ex_exception_valid_q ||
    execute_new_exception_valid;
  assign execute_exception_cause = id_ex_exception_valid_q
    ? id_ex_exception_cause_q
    : execute_new_exception_cause;
  assign execute_exception_value = id_ex_exception_valid_q
    ? id_ex_exception_value_q
    : execute_new_exception_value;
  assign execute_exception_flush = pipeline_advance &&
    execute_new_exception_valid;

  assign execute_jump_redirect_valid = pipeline_advance && id_ex_valid_q &&
    id_ex_jump_q && execute_control_taken && !execute_exception_valid;

  assign execute_branch_mispredict = id_ex_valid_q &&
    (id_ex_branch_operation_q != `UBEE_BRANCH_NONE) && !id_ex_jump_q &&
    !execute_exception_valid &&
    (id_ex_predicted_taken_q != execute_branch_taken);

  assign recovery_redirect_valid = pipeline_advance && execute_branch_mispredict;
  assign recovery_redirect_target = execute_branch_taken
    ? execute_control_target
    : id_ex_pc_next_q;

  assign execute_prediction_correct = BP_ENABLE && id_ex_valid_q &&
    (id_ex_branch_operation_q != `UBEE_BRANCH_NONE) && !id_ex_jump_q &&
    !execute_exception_valid &&
    (id_ex_predicted_taken_q == execute_branch_taken);
  assign execute_prediction_mispredict = BP_ENABLE && execute_branch_mispredict;

  assign bp_update_valid = pipeline_advance && id_ex_valid_q &&
    (id_ex_branch_operation_q != `UBEE_BRANCH_NONE) && !id_ex_jump_q &&
    !execute_exception_valid;
  assign bp_update_taken = execute_branch_taken;

  always_comb begin
    execute_csr_operand = execute_rs1_value;
    if ((id_ex_system_operation_q == `UBEE_SYSTEM_CSR_RWI) ||
        (id_ex_system_operation_q == `UBEE_SYSTEM_CSR_RSI) ||
        (id_ex_system_operation_q == `UBEE_SYSTEM_CSR_RCI)) begin
      execute_csr_operand = id_ex_immediate_q;
    end

    execute_csr_write_enable = 1'b0;
    execute_csr_write_data   = execute_csr_read_data;
    unique case (id_ex_system_operation_q)
      `UBEE_SYSTEM_CSR_RW,
      `UBEE_SYSTEM_CSR_RWI: begin
        execute_csr_write_enable = 1'b1;
        execute_csr_write_data   = execute_csr_operand;
      end
      `UBEE_SYSTEM_CSR_RS,
      `UBEE_SYSTEM_CSR_RSI: begin
        execute_csr_write_enable = (execute_csr_operand != 32'b0);
        execute_csr_write_data   = execute_csr_read_data | execute_csr_operand;
      end
      `UBEE_SYSTEM_CSR_RC,
      `UBEE_SYSTEM_CSR_RCI: begin
        execute_csr_write_enable = (execute_csr_operand != 32'b0);
        execute_csr_write_data   = execute_csr_read_data & ~execute_csr_operand;
      end
      default: begin end
    endcase
  end

  // ----------------------------------------------------------------------
  // Data native interface.
  // ----------------------------------------------------------------------
  assign memory_operation = ex_mem_valid_q &&
    !ex_mem_exception_valid_q &&
    (ex_mem_memory_read_q || ex_mem_memory_write_q);
  assign local_dmem_hit = ((ex_mem_alu_result_q & DMEM_MASK) ==
                           (DMEM_BASE & DMEM_MASK));
  assign local_dmem_store = memory_operation && ex_mem_memory_write_q &&
    local_dmem_hit;
  assign dmem_req_valid_o = memory_operation && !dmem_pending_q;
  assign dmem_req_write_o = ex_mem_memory_write_q;
  assign dmem_req_addr_o  = ex_mem_alu_result_q;
  // Accept posted-store responses even when MEM holds a younger non-mem op.
  assign dmem_rsp_ready_o = dmem_pending_q;

  always_comb begin
    dmem_req_wdata_o = ex_mem_store_data_q;
    dmem_req_wstrb_o = 4'b0000;
    unique case (ex_mem_memory_size_q)
      `UBEE_MEMORY_BYTE: begin
        dmem_req_wdata_o = {24'b0, ex_mem_store_data_q[7:0]} <<
          {ex_mem_alu_result_q[1:0], 3'b000};
        dmem_req_wstrb_o = 4'b0001 << ex_mem_alu_result_q[1:0];
      end
      `UBEE_MEMORY_HALF: begin
        dmem_req_wdata_o = ex_mem_alu_result_q[1]
          ? {ex_mem_store_data_q[15:0], 16'b0}
          : {16'b0, ex_mem_store_data_q[15:0]};
        dmem_req_wstrb_o = ex_mem_alu_result_q[1] ? 4'b1100 : 4'b0011;
      end
      default: begin
        dmem_req_wdata_o = ex_mem_store_data_q;
        dmem_req_wstrb_o = 4'b1111;
      end
    endcase
  end

  assign dmem_request_fire  = dmem_req_valid_o && dmem_req_ready_i;
  assign dmem_response_fire = dmem_rsp_valid_i && dmem_rsp_ready_o;
  assign posted_store_accept = local_dmem_store && dmem_request_fire;
  // Load / MMIO store: stall until a rsp that belongs to the MEM op.
  // When posted_valid, an arriving rsp is owned by the posted store — not by MEM.
  assign memory_stall = memory_operation &&
    !(posted_store_accept || (dmem_response_fire && !posted_valid_q));
  assign posted_wb_hold = posted_valid_q && !dmem_response_fire;
  // Response belongs to MEM op only when no posted store owns the outstanding txn.
  assign memory_response_error = memory_operation && dmem_response_fire &&
    dmem_rsp_err_i && !posted_valid_q;
  assign posted_rsp_err = posted_valid_q && dmem_response_fire && dmem_rsp_err_i;
  assign memory_exception_flush = memory_response_error || posted_rsp_err;
  assign pipeline_advance = !memory_stall && !posted_wb_hold;

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      dmem_pending_q <= 1'b0;
    end else if (dmem_response_fire) begin
      dmem_pending_q <= 1'b0;
    end else if (dmem_request_fire) begin
      dmem_pending_q <= 1'b1;
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      posted_valid_q <= 1'b0;
      posted_pc_q    <= 32'b0;
      posted_addr_q  <= 32'b0;
    end else if (dmem_response_fire && posted_valid_q) begin
      posted_valid_q <= 1'b0;
    end else if (posted_store_accept) begin
      posted_valid_q <= 1'b1;
      posted_pc_q    <= ex_mem_pc_q;
      posted_addr_q  <= ex_mem_alu_result_q;
    end
  end

  always_comb begin
    load_shifted_data = dmem_rsp_rdata_i >>
      {ex_mem_alu_result_q[1:0], 3'b000};
    unique case (ex_mem_memory_size_q)
      `UBEE_MEMORY_BYTE: load_result = ex_mem_memory_unsigned_q
        ? {24'b0, load_shifted_data[7:0]}
        : {{24{load_shifted_data[7]}}, load_shifted_data[7:0]};
      `UBEE_MEMORY_HALF: load_result = ex_mem_memory_unsigned_q
        ? {16'b0, load_shifted_data[15:0]}
        : {{16{load_shifted_data[15]}}, load_shifted_data[15:0]};
      default: load_result = load_shifted_data;
    endcase
  end

  // ----------------------------------------------------------------------
  // Trap selection, interrupt drain and redirect controls.
  // ----------------------------------------------------------------------
  assign synchronous_trap_valid = (mem_wb_valid_q &&
    mem_wb_exception_valid_q) || posted_rsp_err;
  assign csr_mret_commit = mem_wb_valid_q && !mem_wb_exception_valid_q &&
    !posted_rsp_err &&
    (mem_wb_system_operation_q == `UBEE_SYSTEM_MRET);
  assign csr_write_commit = mem_wb_valid_q && !mem_wb_exception_valid_q &&
    !posted_rsp_err &&
    mem_wb_csr_write_enable_q;
  assign csr_instruction_retired = mem_wb_valid_q &&
    !mem_wb_exception_valid_q && !posted_rsp_err;

  assign pipeline_empty = !if_id_valid_q && !id_ex_valid_q &&
    !ex_mem_valid_q && !mem_wb_valid_q && !presented_valid &&
    !fq_valid_q[0] && !fetch_pending_q && !dmem_pending_q &&
    !posted_valid_q &&
    !mul_busy && !div_busy;
  assign interrupt_trap_valid = csr_interrupt_request && pipeline_empty;

  assign csr_trap_enter = synchronous_trap_valid || interrupt_trap_valid;
  assign csr_trap_interrupt = interrupt_trap_valid;
  assign csr_trap_cause = interrupt_trap_valid
    ? csr_interrupt_cause
    : (posted_rsp_err
      ? `UBEE_CAUSE_STORE_ACCESS_FAULT
      : mem_wb_exception_cause_q);
  assign csr_trap_value = interrupt_trap_valid
    ? 32'b0
    : (posted_rsp_err ? posted_addr_q : mem_wb_exception_value_q);
  assign csr_trap_epc = interrupt_trap_valid
    ? fetch_pc_q   // pipeline_empty => next architectural PC (RVC-safe)
    : (posted_rsp_err ? posted_pc_q : mem_wb_pc_q);

  assign architectural_redirect_valid = synchronous_trap_valid ||
    csr_mret_commit || interrupt_trap_valid;
  assign architectural_redirect_target = csr_mret_commit
    ? csr_mepc
    : csr_trap_vector;
  assign fetch_queue_redirect_valid = architectural_redirect_valid ||
    execute_jump_redirect_valid || recovery_redirect_valid ||
    prediction_redirect_valid;
  assign pipeline_flush_redirect = architectural_redirect_valid ||
    execute_jump_redirect_valid || recovery_redirect_valid;
  assign fetch_redirect_valid = fetch_queue_redirect_valid;
  assign fetch_redirect_target = architectural_redirect_valid
    ? architectural_redirect_target
    : (execute_jump_redirect_valid
      ? execute_control_target
      : (recovery_redirect_valid
        ? recovery_redirect_target
        : prediction_redirect_target));
  assign exception_flush = execute_exception_flush || memory_exception_flush;

  assign pipeline_serializing_busy =
    (id_ex_valid_q && id_ex_serializing_q) ||
    (ex_mem_valid_q && ex_mem_serializing_q) ||
    (mem_wb_valid_q && mem_wb_serializing_q);
  assign decode_stall = load_use_stall || pipeline_serializing_busy;
  assign front_end_advance = pipeline_advance && !decode_stall && !execute_stall;

  // front-end issues an instruction this cycle
  assign fetch_buffer_consume = front_end_advance &&
    !pipeline_flush_redirect && !exception_flush && presented_valid;

  // -----------------------------------------------------------------------
  // Combinational next-state for the fetch queue.
  // This merges consume (shift) and IMEM fill into one net result so that
  // the always_ff block only has one assignment per register.
  // -----------------------------------------------------------------------
  logic [47:0] fq_data_next;
  logic [2:0]  fq_valid_next;
  logic [2:0]  fq_err_next;
  logic [31:0] fq_pc_q_next;

  // Post-consume occupancy (combinational) — used by both the fetch request
  // gate and the response fill logic.
  /* verilator lint_off UNUSEDSIGNAL */
  logic [2:0] fq_valid_after_consume;
  /* verilator lint_on UNUSEDSIGNAL */
  always_comb begin
    if (fetch_buffer_consume) begin
      if (fetch_buffer_is_compressed)
        fq_valid_after_consume = {1'b0,  fq_valid_q[2:1]};
      else
        fq_valid_after_consume = {2'b00, fq_valid_q[2]};
    end else begin
      fq_valid_after_consume = fq_valid_q;
    end
  end

  // Skip the lower half-word of the first response after a redirect to a
  // half-word-aligned target (fetch_redirect_target[1]==1).
  // After a redirect to a half-word-aligned target (target[1]==1), the first
  // IMEM response contains the instruction in rdata[31:16], not rdata[15:0].
  // fq_fill_skip_lo is a registered flag cleared as soon as the first fill fires.
  logic fq_fill_skip_lo;

  // Combinational next-state for fq_data/valid/err/pc.
  // Merges consume shift and IMEM fill so the FF only needs one write.
  always_comb begin
    // -----------------------------------------------------------------------
    // Compute the post-consume (shifted) queue state as intermediate values.
    // -----------------------------------------------------------------------
    logic [47:0] s_data;   // post-consume data
    logic [2:0]  s_valid;  // post-consume valid
    logic [2:0]  s_err;    // post-consume error
    logic [31:0] s_pc;     // post-consume PC

    if (fetch_buffer_consume) begin
      if (fetch_buffer_is_compressed) begin
        s_data  = {16'b0, fq_data_q[47:16]};
        s_valid = {1'b0,  fq_valid_q[2:1]};
        s_err   = {1'b0,  fq_err_q[2:1]};
        s_pc    = fq_pc_q + 32'd2;
      end else begin
        s_data  = {32'b0, fq_data_q[47:32]};
        s_valid = {2'b0,  fq_valid_q[2]};
        s_err   = {2'b0,  fq_err_q[2]};
        s_pc    = fq_pc_q + 32'd4;
      end
    end else begin
      s_data  = fq_data_q;
      s_valid = fq_valid_q;
      s_err   = fq_err_q;
      s_pc    = fq_pc_q;
    end

    // -----------------------------------------------------------------------
    // Apply IMEM fill on top of the post-consume state.
    // -----------------------------------------------------------------------
    fq_data_next  = s_data;
    fq_valid_next = s_valid;
    fq_err_next   = s_err;
    fq_pc_q_next  = s_pc;

    if (fetch_response_fire && !fetch_discard_q) begin
      // fq_valid_after_consume == s_valid (same computation)
      case (s_valid)
        3'b000: begin
          if (fq_fill_skip_lo) begin
            // HW-aligned redirect: skip rdata_lo, use rdata_hi as hw0
            fq_data_next  = {32'b0, imem_rsp_rdata_i[31:16]};
            fq_err_next   = {2'b0,  imem_rsp_err_i};
            fq_valid_next = 3'b001;
          end else begin
            // Normal: hw0=rdata_lo, hw1=rdata_hi
            fq_data_next  = {16'b0, imem_rsp_rdata_i[31:16], imem_rsp_rdata_i[15:0]};
            fq_err_next   = {1'b0,  imem_rsp_err_i,           imem_rsp_err_i};
            fq_valid_next = 3'b011;
          end
        end
        3'b001: begin
          // hw0 valid, hw1+hw2 empty → fill hw1=rdata_lo, hw2=rdata_hi
          fq_data_next  = {imem_rsp_rdata_i[31:16], imem_rsp_rdata_i[15:0], s_data[15:0]};
          fq_err_next   = {imem_rsp_err_i,           imem_rsp_err_i,          s_err[0]};
          fq_valid_next = {1'b1,                     1'b1,                    s_valid[0]};
        end
        3'b011: begin
          // Only one free slot — must not happen if requests require two free
          // slots (see imem_req_valid_o). Keep as a safe no-op rather than
          // dropping rdata[31:16] by accepting a partial word.
        end
        default: begin end
      endcase
    end
  end

  // ----------------------------------------------------------------------
  // Instruction native interface.
  // Each IMEM response supplies two half-words.  Only request a fetch when
  // at least two queue slots are free (hw1 empty ⇒ hw1 and hw2 free after
  // the invariant that slots pack toward hw0); otherwise the high half of
  // the returned word is dropped and the RVC stream desynchronizes.
  // ----------------------------------------------------------------------
  assign imem_req_valid_o = !fetch_pending_q &&
    !fq_valid_after_consume[1] &&
    !fetch_queue_redirect_valid && !exception_flush &&
    !pipeline_serializing_busy && !csr_interrupt_request;
  assign imem_req_addr_o  = fetch_pc_q;
  assign imem_rsp_ready_o = fetch_pending_q;
  assign fetch_request_fire  = imem_req_valid_o && imem_req_ready_i;
  assign fetch_response_fire = imem_rsp_valid_i && imem_rsp_ready_o;

  // ----------------------------------------------------------------------
  // Fetch FSM — 48-bit fetch queue (3 half-words), shift-register style.
  //
  // fq_data_q[47:0]:  hw0=fq_data_q[15:0], hw1=[31:16], hw2=[47:32]
  // fq_valid_q[2:0]:  valid bit per half-word
  // fq_err_q[2:0]:    error bit per half-word
  // fq_pc_q:          PC of hw0
  //
  // On instruction consume: shift the queue left by 1 (compressed) or 2 (32-bit)
  //   half-words and clear the freed slots.
  //
  // On IMEM response: fill the first empty slot(s) from the MSB side.
  //   Each response provides 2 half-words (hw_lo = rdata[15:0], hw_hi = [31:16]).
  //
  // On redirect: flush the queue and load the new fetch PC (word-aligned).
  //   If the target is half-word aligned (bit[1]==1) we pre-shift so that
  //   hw0 starts at the correct half-word on the first response.
  // ----------------------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      fetch_pc_q         <= RESET_VECTOR;
      fetch_pending_q    <= 1'b0;
      fetch_discard_q    <= 1'b0;
      fq_data_q          <= 48'b0;
      fq_valid_q         <= 3'b000;
      fq_err_q           <= 3'b000;
      fq_pc_q            <= RESET_VECTOR;
      fq_fill_skip_lo    <= 1'b0;
    end else if (fetch_redirect_valid) begin
      // Flush and restart.
      // Fetch from word-aligned base; if target[1]==1 we want the upper half
      // of that word as hw0, so set fq_fill_skip_lo to skip rdata[15:0] on first fill.
      fetch_pc_q       <= {fetch_redirect_target[31:2], 2'b00};
      fq_valid_q       <= 3'b000;
      fq_data_q        <= 48'b0;
      fq_pc_q          <= fetch_redirect_target;
      fq_fill_skip_lo  <= fetch_redirect_target[1];
      if (fetch_response_fire) begin
        fetch_pending_q <= 1'b0;
        fetch_discard_q <= 1'b0;
      end else if (fetch_pending_q) begin
        fetch_discard_q <= 1'b1;
      end else begin
        fetch_discard_q <= 1'b0;
      end
    end else if (exception_flush) begin
      fq_valid_q      <= 3'b000;
      fq_fill_skip_lo <= 1'b0;
      if (fetch_response_fire) begin
        fetch_pending_q <= 1'b0;
        fetch_discard_q <= 1'b0;
      end else if (fetch_pending_q) begin
        fetch_discard_q <= 1'b1;
      end
    end else begin
      // ------------------------------------------------------------------
      // Consume + Fill: compute net queue state in one step.
      // Using combinational next-state values (nxt_*) that capture both
      // the shift-on-consume and the IMEM fill in the same clock edge.
      // ------------------------------------------------------------------
      if (fetch_request_fire) begin
        fetch_pending_q <= 1'b1;
        fetch_pc_q      <= fetch_pc_q + 32'd4;
      end

      if (fetch_response_fire) begin
        fetch_pending_q <= 1'b0;
        fetch_discard_q <= 1'b0;
        if (!fetch_discard_q)
          fq_fill_skip_lo <= 1'b0;  // consumed on first valid fill
      end

      fq_pc_q    <= fq_pc_q_next;
      fq_data_q  <= fq_data_next;
      fq_valid_q <= fq_valid_next;
      fq_err_q   <= fq_err_next;
    end
  end

  // ----------------------------------------------------------------------
  // Pipeline state updates.
  // ----------------------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      if_id_valid_q         <= 1'b0;
      if_id_pc_q            <= 32'b0;
      if_id_instruction_q   <= 32'b0;
      if_id_fetch_error_q   <= 1'b0;
      if_id_is_compressed_q <= 1'b0;
      if_id_predicted_taken_q <= 1'b0;
      if_id_bp_index_q      <= '0;
    end else if (pipeline_advance) begin
      if (pipeline_flush_redirect || exception_flush) begin
        if_id_valid_q         <= 1'b0;
        if_id_fetch_error_q   <= 1'b0;
        if_id_is_compressed_q <= 1'b0;
        if_id_predicted_taken_q <= 1'b0;
        if_id_bp_index_q      <= '0;
      end else if (!decode_stall && !execute_stall) begin
        if (presented_valid) begin
          if_id_valid_q         <= 1'b1;
          if_id_pc_q            <= presented_pc;
          if_id_instruction_q   <= fetch_buffer_decompressed;
          if_id_fetch_error_q   <= presented_error;
          if_id_is_compressed_q <= fetch_buffer_is_compressed;
          if_id_predicted_taken_q <= BP_ENABLE && presented_is_branch &&
            bp_predict_taken;
          if_id_bp_index_q      <= bp_lookup_index;
        end else begin
          if_id_valid_q         <= 1'b0;
          if_id_fetch_error_q   <= 1'b0;
          if_id_is_compressed_q <= 1'b0;
          if_id_predicted_taken_q <= 1'b0;
          if_id_bp_index_q      <= '0;
        end
      end
      // When decode is stalled we must NOT consume the presented instruction.
      // The fetch_buffer_consume signal already accounts for this via front_end_advance.
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      id_ex_valid_q             <= 1'b0;
      id_ex_pc_q                <= 32'b0;
      id_ex_instruction_q       <= 32'b0;
      id_ex_pc_next_q           <= 32'b0;
      id_ex_rs1_data_q          <= 32'b0;
      id_ex_rs2_data_q          <= 32'b0;
      id_ex_rs1_addr_q          <= 5'b0;
      id_ex_rs2_addr_q          <= 5'b0;
      id_ex_uses_rs1_q          <= 1'b0;
      id_ex_uses_rs2_q          <= 1'b0;
      id_ex_immediate_q         <= 32'b0;
      id_ex_rd_addr_q           <= 5'b0;
      id_ex_rd_write_q          <= 1'b0;
      id_ex_alu_operation_q     <= `UBEE_ALU_ADD;
      id_ex_operand_a_select_q  <= `UBEE_OPERAND_A_RS1;
      id_ex_operand_b_select_q  <= `UBEE_OPERAND_B_RS2;
      id_ex_branch_operation_q  <= `UBEE_BRANCH_NONE;
      id_ex_jump_q              <= 1'b0;
      id_ex_jump_register_q     <= 1'b0;
      id_ex_memory_read_q       <= 1'b0;
      id_ex_memory_write_q      <= 1'b0;
      id_ex_memory_size_q       <= `UBEE_MEMORY_WORD;
      id_ex_memory_unsigned_q   <= 1'b0;
      id_ex_writeback_select_q  <= `UBEE_WB_NONE;
      id_ex_system_operation_q  <= `UBEE_SYSTEM_NONE;
      id_ex_csr_address_q       <= 12'b0;
      id_ex_exception_valid_q   <= 1'b0;
      id_ex_exception_cause_q   <= 32'b0;
      id_ex_exception_value_q   <= 32'b0;
      id_ex_serializing_q       <= 1'b0;
      id_ex_predicted_taken_q   <= 1'b0;
      id_ex_bp_index_q          <= '0;
    end else if (pipeline_advance) begin
      if (pipeline_flush_redirect || exception_flush) begin
        id_ex_valid_q           <= 1'b0;
        id_ex_exception_valid_q <= 1'b0;
        id_ex_serializing_q     <= 1'b0;
        id_ex_predicted_taken_q <= 1'b0;
        id_ex_bp_index_q        <= '0;
      end else if (decode_stall || execute_stall) begin
        // decode_stall: bubble EX (load-use). execute_stall: hold mul/div in EX.
        if (decode_stall && !execute_stall) begin
          id_ex_valid_q           <= 1'b0;
          id_ex_exception_valid_q <= 1'b0;
          id_ex_serializing_q     <= 1'b0;
        end
      end else begin
        id_ex_valid_q             <= decode_instruction_valid;
        id_ex_pc_q                <= if_id_pc_q;
        id_ex_instruction_q       <= if_id_instruction_q;
        id_ex_pc_next_q           <= if_id_pc_q + (if_id_is_compressed_q ? 32'd2 : 32'd4);
        id_ex_rs1_data_q          <= decode_uses_rs1 ? decode_rs1_value : 32'b0;
        id_ex_rs2_data_q          <= decode_uses_rs2 ? decode_rs2_value : 32'b0;
        id_ex_rs1_addr_q          <= if_id_instruction_q[19:15];
        id_ex_rs2_addr_q          <= if_id_instruction_q[24:20];
        id_ex_uses_rs1_q          <= decode_uses_rs1 && !decode_exception_valid;
        id_ex_uses_rs2_q          <= decode_uses_rs2 && !decode_exception_valid;
        id_ex_immediate_q         <= decode_immediate;
        id_ex_rd_addr_q           <= if_id_instruction_q[11:7];
        id_ex_rd_write_q          <= decode_rd_write && !decode_exception_valid;
        id_ex_alu_operation_q     <= decode_alu_operation;
        id_ex_operand_a_select_q  <= decode_operand_a_select;
        id_ex_operand_b_select_q  <= decode_operand_b_select;
        id_ex_branch_operation_q  <= decode_exception_valid
          ? `UBEE_BRANCH_NONE : decode_branch_operation;
        id_ex_jump_q              <= decode_jump && !decode_exception_valid;
        id_ex_jump_register_q     <= decode_jump_register && !decode_exception_valid;
        id_ex_memory_read_q       <= decode_memory_read && !decode_exception_valid;
        id_ex_memory_write_q      <= decode_memory_write && !decode_exception_valid;
        id_ex_memory_size_q       <= decode_memory_size;
        id_ex_memory_unsigned_q   <= decode_memory_unsigned;
        id_ex_writeback_select_q  <= decode_writeback_select;
        id_ex_system_operation_q  <= decode_system_operation;
        id_ex_csr_address_q       <= decode_csr_address;
        id_ex_exception_valid_q   <= decode_exception_valid;
        id_ex_exception_cause_q   <= decode_exception_cause;
        id_ex_exception_value_q   <= decode_exception_value;
        id_ex_serializing_q       <= decode_serializing;
        id_ex_predicted_taken_q   <= if_id_predicted_taken_q;
        id_ex_bp_index_q          <= if_id_bp_index_q;
      end
    end else if (memory_stall) begin
      id_ex_rs1_data_q <= execute_rs1_value;
      id_ex_rs2_data_q <= execute_rs2_value;
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      ex_mem_valid_q             <= 1'b0;
      ex_mem_pc_q                <= 32'b0;
      ex_mem_instruction_q       <= 32'b0;
      ex_mem_pc_next_q           <= 32'b0;
      ex_mem_next_arch_pc_q      <= 32'b0;
      ex_mem_alu_result_q        <= 32'b0;
      ex_mem_store_data_q        <= 32'b0;
      ex_mem_rd_addr_q           <= 5'b0;
      ex_mem_rd_write_q          <= 1'b0;
      ex_mem_memory_read_q       <= 1'b0;
      ex_mem_memory_write_q      <= 1'b0;
      ex_mem_memory_size_q       <= `UBEE_MEMORY_WORD;
      ex_mem_memory_unsigned_q   <= 1'b0;
      ex_mem_writeback_select_q  <= `UBEE_WB_NONE;
      ex_mem_system_operation_q  <= `UBEE_SYSTEM_NONE;
      ex_mem_csr_address_q       <= 12'b0;
      ex_mem_csr_read_data_q     <= 32'b0;
      ex_mem_csr_write_enable_q  <= 1'b0;
      ex_mem_csr_write_data_q    <= 32'b0;
      ex_mem_exception_valid_q   <= 1'b0;
      ex_mem_exception_cause_q   <= 32'b0;
      ex_mem_exception_value_q   <= 32'b0;
      ex_mem_serializing_q       <= 1'b0;
      ex_mem_branch_valid_q      <= 1'b0;
      ex_mem_branch_taken_q      <= 1'b0;
      ex_mem_prediction_correct_q <= 1'b0;
      ex_mem_prediction_mispredict_q <= 1'b0;
    end else if (pipeline_advance) begin
      if (architectural_redirect_valid || memory_exception_flush) begin
        ex_mem_valid_q           <= 1'b0;
        ex_mem_exception_valid_q <= 1'b0;
        ex_mem_serializing_q     <= 1'b0;
      end else if (execute_stall) begin
        ex_mem_valid_q           <= 1'b0;
        ex_mem_exception_valid_q <= 1'b0;
        ex_mem_serializing_q     <= 1'b0;
      end else begin
        ex_mem_valid_q            <= id_ex_valid_q;
        ex_mem_pc_q               <= id_ex_pc_q;
        ex_mem_instruction_q      <= id_ex_instruction_q;
        ex_mem_pc_next_q          <= id_ex_pc_next_q;
        ex_mem_next_arch_pc_q     <= execute_control_taken
          ? execute_control_target : id_ex_pc_next_q;
        ex_mem_alu_result_q       <= execute_alu_result;
        ex_mem_store_data_q       <= execute_rs2_value;
        ex_mem_rd_addr_q          <= id_ex_rd_addr_q;
        ex_mem_rd_write_q         <= id_ex_rd_write_q && !execute_exception_valid;
        ex_mem_memory_read_q      <= id_ex_memory_read_q && !execute_exception_valid;
        ex_mem_memory_write_q     <= id_ex_memory_write_q && !execute_exception_valid;
        ex_mem_memory_size_q      <= id_ex_memory_size_q;
        ex_mem_memory_unsigned_q  <= id_ex_memory_unsigned_q;
        ex_mem_writeback_select_q <= id_ex_writeback_select_q;
        ex_mem_system_operation_q <= id_ex_system_operation_q;
        ex_mem_csr_address_q      <= id_ex_csr_address_q;
        ex_mem_csr_read_data_q    <= execute_csr_read_data;
        ex_mem_csr_write_enable_q <= execute_csr_write_enable &&
          execute_csr_access_valid && !execute_exception_valid;
        ex_mem_csr_write_data_q   <= execute_csr_write_data;
        ex_mem_exception_valid_q  <= execute_exception_valid;
        ex_mem_exception_cause_q  <= execute_exception_cause;
        ex_mem_exception_value_q  <= execute_exception_value;
        ex_mem_serializing_q      <= id_ex_serializing_q ||
          execute_new_exception_valid;
        ex_mem_branch_valid_q     <= id_ex_valid_q &&
          (id_ex_branch_operation_q != `UBEE_BRANCH_NONE) && !id_ex_jump_q &&
          !execute_exception_valid;
        ex_mem_branch_taken_q     <= id_ex_valid_q &&
          (id_ex_branch_operation_q != `UBEE_BRANCH_NONE) && !id_ex_jump_q &&
          execute_branch_taken && !execute_exception_valid;
        ex_mem_prediction_correct_q <= execute_prediction_correct;
        ex_mem_prediction_mispredict_q <= execute_prediction_mispredict;
      end
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      mem_wb_valid_q            <= 1'b0;
      mem_wb_pc_q               <= 32'b0;
      mem_wb_instruction_q      <= 32'b0;
      mem_wb_next_arch_pc_q     <= 32'b0;
      mem_wb_rd_addr_q          <= 5'b0;
      mem_wb_rd_write_q         <= 1'b0;
      mem_wb_result_q           <= 32'b0;
      mem_wb_memory_valid_q     <= 1'b0;
      mem_wb_memory_write_q     <= 1'b0;
      mem_wb_memory_addr_q      <= 32'b0;
      mem_wb_memory_wdata_q     <= 32'b0;
      mem_wb_memory_wstrb_q     <= 4'b0;
      mem_wb_system_operation_q <= `UBEE_SYSTEM_NONE;
      mem_wb_csr_address_q      <= 12'b0;
      mem_wb_csr_write_enable_q <= 1'b0;
      mem_wb_csr_write_data_q   <= 32'b0;
      mem_wb_exception_valid_q  <= 1'b0;
      mem_wb_exception_cause_q  <= 32'b0;
      mem_wb_exception_value_q  <= 32'b0;
      mem_wb_serializing_q      <= 1'b0;
      mem_wb_branch_valid_q     <= 1'b0;
      mem_wb_branch_taken_q     <= 1'b0;
      mem_wb_prediction_correct_q <= 1'b0;
      mem_wb_prediction_mispredict_q <= 1'b0;
    end else if (architectural_redirect_valid) begin
      mem_wb_valid_q           <= 1'b0;
      mem_wb_exception_valid_q <= 1'b0;
      mem_wb_serializing_q     <= 1'b0;
    end else if (posted_wb_hold) begin
      // Keep posted store in WB until local DMEM rsp returns.
    end else if (memory_stall) begin
      // An older WB instruction commits once while MEM waits.
      mem_wb_valid_q       <= 1'b0;
      mem_wb_serializing_q <= 1'b0;
    end else begin
      mem_wb_valid_q            <= ex_mem_valid_q;
      mem_wb_pc_q               <= ex_mem_pc_q;
      mem_wb_instruction_q      <= ex_mem_instruction_q;
      mem_wb_next_arch_pc_q     <= ex_mem_next_arch_pc_q;
      mem_wb_rd_addr_q          <= ex_mem_rd_addr_q;
      mem_wb_rd_write_q         <= ex_mem_rd_write_q && !memory_response_error;
      mem_wb_memory_valid_q     <= ex_mem_valid_q &&
        (ex_mem_memory_read_q || ex_mem_memory_write_q);
      mem_wb_memory_write_q     <= ex_mem_memory_write_q;
      mem_wb_memory_addr_q      <= ex_mem_alu_result_q;
      mem_wb_memory_wdata_q     <= ex_mem_memory_write_q ? dmem_req_wdata_o : 32'b0;
      mem_wb_memory_wstrb_q     <= ex_mem_memory_write_q ? dmem_req_wstrb_o : 4'b0;
      mem_wb_system_operation_q <= ex_mem_system_operation_q;
      mem_wb_csr_address_q      <= ex_mem_csr_address_q;
      mem_wb_csr_write_enable_q <= ex_mem_csr_write_enable_q;
      mem_wb_csr_write_data_q   <= ex_mem_csr_write_data_q;
      mem_wb_exception_valid_q  <= ex_mem_exception_valid_q || memory_response_error;
      mem_wb_exception_cause_q  <= memory_response_error
        ? (ex_mem_memory_write_q
           ? `UBEE_CAUSE_STORE_ACCESS_FAULT
           : `UBEE_CAUSE_LOAD_ACCESS_FAULT)
        : ex_mem_exception_cause_q;
      mem_wb_exception_value_q  <= memory_response_error
        ? ex_mem_alu_result_q : ex_mem_exception_value_q;
      mem_wb_serializing_q      <= ex_mem_serializing_q || memory_response_error;

      mem_wb_branch_valid_q     <= ex_mem_branch_valid_q;
      mem_wb_branch_taken_q     <= ex_mem_branch_taken_q;
      mem_wb_prediction_correct_q <= ex_mem_prediction_correct_q;
      mem_wb_prediction_mispredict_q <= ex_mem_prediction_mispredict_q;

      unique case (ex_mem_writeback_select_q)
        `UBEE_WB_ALU:     mem_wb_result_q <= ex_mem_alu_result_q;
        `UBEE_WB_MEMORY:  mem_wb_result_q <= load_result;
        `UBEE_WB_PC_NEXT: mem_wb_result_q <= ex_mem_pc_next_q;
        `UBEE_WB_CSR:     mem_wb_result_q <= ex_mem_csr_read_data_q;
        default:          mem_wb_result_q <= 32'b0;
      endcase
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      interrupt_trace_pending_q <= 1'b0;
      interrupt_trace_pc_q      <= 32'b0;
      interrupt_trace_cause_q   <= 32'b0;
    end else begin
      interrupt_trace_pending_q <= interrupt_trap_valid;
      if (interrupt_trap_valid) begin
        interrupt_trace_pc_q    <= fetch_pc_q;
        interrupt_trace_cause_q <= csr_interrupt_cause;
      end
    end
  end

  // ----------------------------------------------------------------------
  // Architectural commit/event trace.
  // ----------------------------------------------------------------------
  assign trace_valid_o = interrupt_trace_pending_q || mem_wb_valid_q;
  assign trace_pc_o = interrupt_trace_pending_q
    ? interrupt_trace_pc_q : mem_wb_pc_q;
  assign trace_instruction_o = interrupt_trace_pending_q
    ? 32'b0 : mem_wb_instruction_q;
  assign trace_rd_write_o = !interrupt_trace_pending_q && mem_wb_valid_q &&
    mem_wb_rd_write_q && !mem_wb_exception_valid_q &&
    (mem_wb_rd_addr_q != 5'd0);
  assign trace_rd_addr_o = interrupt_trace_pending_q ? 5'b0 : mem_wb_rd_addr_q;
  assign trace_rd_data_o = interrupt_trace_pending_q ? 32'b0 : mem_wb_result_q;
  assign trace_mem_valid_o = !interrupt_trace_pending_q && mem_wb_valid_q &&
    mem_wb_memory_valid_q && !posted_rsp_err;
  assign trace_mem_write_o = !interrupt_trace_pending_q && mem_wb_memory_write_q &&
    !posted_rsp_err;
  assign trace_mem_addr_o = interrupt_trace_pending_q ? 32'b0 : mem_wb_memory_addr_q;
  assign trace_mem_wdata_o = interrupt_trace_pending_q ? 32'b0 : mem_wb_memory_wdata_q;
  assign trace_mem_wstrb_o = interrupt_trace_pending_q ? 4'b0 : mem_wb_memory_wstrb_q;
  assign trace_trap_o = interrupt_trace_pending_q ||
    (mem_wb_valid_q && mem_wb_exception_valid_q) || posted_rsp_err;
  assign trace_cause_o = interrupt_trace_pending_q
    ? interrupt_trace_cause_q
    : (posted_rsp_err
      ? `UBEE_CAUSE_STORE_ACCESS_FAULT
      : (mem_wb_exception_valid_q ? mem_wb_exception_cause_q : 32'b0));

  // ----------------------------------------------------------------------
  // Continuous student/debug observe (pipeline PC, RF write, CSR mirror).
  // DEBUG_ENABLE=0 ties ports to 0 (SoC / IP may also hide them).
  // ----------------------------------------------------------------------
  if (DEBUG_ENABLE) begin : g_debug
    assign dbg_fetch_pc_o      = fetch_pc_q;
    assign dbg_if_pc_o         = if_id_pc_q;
    assign dbg_if_valid_o      = if_id_valid_q;
    assign dbg_ex_pc_o         = id_ex_pc_q;
    assign dbg_ex_valid_o      = id_ex_valid_q;
    assign dbg_mem_pc_o        = ex_mem_pc_q;
    assign dbg_mem_valid_o     = ex_mem_valid_q;
    assign dbg_wb_pc_o         = mem_wb_pc_q;
    assign dbg_wb_valid_o      = mem_wb_valid_q;
    assign dbg_rf_we_o         = rf_write_enable;
    assign dbg_rf_waddr_o      = mem_wb_rd_addr_q;
    assign dbg_rf_wdata_o      = mem_wb_result_q;
    assign dbg_csr_mstatus_o   = dbg_csr_mstatus_w;
    assign dbg_csr_mie_o       = dbg_csr_mie_w;
    assign dbg_csr_mtvec_o     = dbg_csr_mtvec_w;
    assign dbg_csr_mscratch_o  = dbg_csr_mscratch_w;
    assign dbg_csr_mepc_o      = dbg_csr_mepc_w;
    assign dbg_csr_mcause_o    = dbg_csr_mcause_w;
    assign dbg_csr_mtval_o     = dbg_csr_mtval_w;
    assign dbg_csr_mip_o       = dbg_csr_mip_w;
    assign dbg_csr_mcycle_o    = dbg_csr_mcycle_w;
    assign dbg_csr_mcycleh_o   = dbg_csr_mcycleh_w;
    assign dbg_csr_minstret_o  = dbg_csr_minstret_w;
    assign dbg_csr_minstreth_o = dbg_csr_minstreth_w;
  end else begin : g_no_debug
    assign dbg_fetch_pc_o      = 32'b0;
    assign dbg_if_pc_o         = 32'b0;
    assign dbg_if_valid_o      = 1'b0;
    assign dbg_ex_pc_o         = 32'b0;
    assign dbg_ex_valid_o      = 1'b0;
    assign dbg_mem_pc_o        = 32'b0;
    assign dbg_mem_valid_o     = 1'b0;
    assign dbg_wb_pc_o         = 32'b0;
    assign dbg_wb_valid_o      = 1'b0;
    assign dbg_rf_we_o         = 1'b0;
    assign dbg_rf_waddr_o      = 5'b0;
    assign dbg_rf_wdata_o      = 32'b0;
    assign dbg_csr_mstatus_o   = 32'b0;
    assign dbg_csr_mie_o       = 32'b0;
    assign dbg_csr_mtvec_o     = 32'b0;
    assign dbg_csr_mscratch_o  = 32'b0;
    assign dbg_csr_mepc_o      = 32'b0;
    assign dbg_csr_mcause_o    = 32'b0;
    assign dbg_csr_mtval_o     = 32'b0;
    assign dbg_csr_mip_o       = 32'b0;
    assign dbg_csr_mcycle_o    = 32'b0;
    assign dbg_csr_mcycleh_o   = 32'b0;
    assign dbg_csr_minstret_o  = 32'b0;
    assign dbg_csr_minstreth_o = 32'b0;
    // Keep CSR mirror drivers referenced when observe ports are tied off.
    logic unused_dbg_csr;
    assign unused_dbg_csr = ^{dbg_csr_mstatus_w, dbg_csr_mie_w, dbg_csr_mtvec_w,
      dbg_csr_mscratch_w, dbg_csr_mepc_w, dbg_csr_mcause_w, dbg_csr_mtval_w,
      dbg_csr_mip_w, dbg_csr_mcycle_w, dbg_csr_mcycleh_w,
      dbg_csr_minstret_w, dbg_csr_minstreth_w};
  end

  // FENCE is architecturally a serialization point once caches and write
  // buffers are introduced. It has no additional effect in the native-bus core.
  logic unused_decode_fence;
  assign unused_decode_fence = decode_fence ^ ^mem_wb_next_arch_pc_q;

  uBee_perf_probe #(
    .MUL_ENABLE (MUL_ENABLE),
    .DIV_ENABLE (DIV_ENABLE)
  ) u_perf_probe (
    .clk_i                    (clk_i),
    .rst_ni                   (rst_ni),
    .pipeline_advance_i       (pipeline_advance),
    .decode_stall_i           (decode_stall),
    .execute_stall_i          (execute_stall),
    .memory_stall_i           (memory_stall || posted_wb_hold),
    .load_use_stall_i         (load_use_stall),
    .forward_rs1_select_i     (forward_rs1_select),
    .forward_rs2_select_i     (forward_rs2_select),
    .presented_valid_i        (presented_valid),
    .fetch_queue_redirect_valid_i (fetch_queue_redirect_valid),
    .pipeline_flush_redirect_i (pipeline_flush_redirect),
    .prediction_redirect_valid_i (prediction_redirect_valid),
    .exception_flush_i        (exception_flush),
    .fetch_buffer_consume_i   (fetch_buffer_consume),
    .imem_req_valid_i         (imem_req_valid_o),
    .imem_req_ready_i         (imem_req_ready_i),
    .fetch_pending_i          (fetch_pending_q),
    .fetch_response_fire_i    (fetch_response_fire),
    .dmem_response_fire_i     (dmem_response_fire),
    .dmem_rsp_err_i           (dmem_rsp_err_i),
    .memory_response_error_i  (memory_response_error),
    .fetch_response_err_i     (imem_rsp_err_i),
    .fetch_discard_i          (fetch_discard_q),
    .mem_wb_valid_i           (mem_wb_valid_q),
    .mem_wb_exception_valid_i (mem_wb_exception_valid_q || posted_rsp_err),
    .mem_wb_memory_valid_i    (mem_wb_memory_valid_q),
    .mem_wb_memory_write_i    (mem_wb_memory_write_q),
    .mem_wb_instruction_i     (mem_wb_instruction_q),
    .mem_wb_branch_valid_i    (mem_wb_branch_valid_q),
    .mem_wb_branch_taken_i    (mem_wb_branch_taken_q),
    .mem_wb_prediction_correct_i (mem_wb_prediction_correct_q),
    .mem_wb_prediction_mispredict_i (mem_wb_prediction_mispredict_q),
    .id_ex_valid_i            (id_ex_valid_q),
    .id_ex_branch_operation_i (id_ex_branch_operation_q),
    .id_ex_jump_i             (id_ex_jump_q),
    .recovery_redirect_valid_i (recovery_redirect_valid),
    .architectural_redirect_valid_i (architectural_redirect_valid),
    .interrupt_trap_valid_i   (interrupt_trap_valid),
    .events_o                 (perf_events_o)
  );
endmodule
