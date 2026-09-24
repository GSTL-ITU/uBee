`timescale 1ns/1ps
`include "uBee_defs.svh"

module uBee_csr_trap #(
  parameter logic [31:0] MTVEC_RESET = 32'h0000_0000,
  parameter bit SHADOW_BANK_ENABLE = 1'b1
) (
  input  logic        clk_i,
  input  logic        rst_ni,

  input  logic [11:0] csr_check_addr_i,
  output logic        csr_check_valid_o,
  input  logic [11:0] csr_access_addr_i,
  output logic [31:0] csr_access_rdata_o,
  output logic        csr_access_valid_o,
  input  logic        csr_write_i,
  input  logic [11:0] csr_write_addr_i,
  input  logic [31:0] csr_write_data_i,

  input  logic        trap_enter_i,
  input  logic        trap_interrupt_i,
  // cause[31] ignored: interrupt bit comes from trap_interrupt_i
  /* verilator lint_off UNUSEDSIGNAL */
  input  logic [31:0] trap_cause_i,
  /* verilator lint_on  UNUSEDSIGNAL */
  input  logic [31:0] trap_value_i,
  // epc[0] ignored: IALIGN forces mepc[0]=0
  /* verilator lint_off UNUSEDSIGNAL */
  input  logic [31:0] trap_epc_i,
  /* verilator lint_on  UNUSEDSIGNAL */
  input  logic        mret_i,
  input  logic        instruction_retired_i,

  input  logic        irq_software_i,
  input  logic        irq_timer_i,
  input  logic        irq_external_i,

  output logic [31:0] trap_vector_o,
  output logic [31:0] mepc_o,
  output logic        interrupt_request_o,
  output logic [31:0] interrupt_cause_o,
  output logic        shadow_bank_o,

  // Continuous observe ports (student BD waveform / IP debug pins)
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
  output logic [31:0] dbg_csr_minstreth_o
);
  logic [31:0] mstatus_q;
  logic [31:0] mie_q;
  logic [31:0] mtvec_q;
  logic [31:0] mscratch_q;
  logic [31:0] mepc_q;
  logic [31:0] mcause_q;
  logic [31:0] mtval_q;
  logic [31:0] mip_software_q;
  logic [63:0] mcycle_q;
  logic [63:0] minstret_q;
  logic [31:0] mip_value;
  logic        irq_ext_pend;
  logic        irq_sw_pend;
  logic        irq_tm_pend;

  function automatic logic csr_address_valid(input logic [11:0] address);
    begin
      unique case (address)
        `UBEE_CSR_MSTATUS,
        `UBEE_CSR_MIE,
        `UBEE_CSR_MTVEC,
        `UBEE_CSR_MSCRATCH,
        `UBEE_CSR_MEPC,
        `UBEE_CSR_MCAUSE,
        `UBEE_CSR_MTVAL,
        `UBEE_CSR_MIP,
        `UBEE_CSR_MCYCLE,
        `UBEE_CSR_MCYCLEH,
        `UBEE_CSR_MINSTRET,
        `UBEE_CSR_MINSTRETH: csr_address_valid = 1'b1;
        default:             csr_address_valid = 1'b0;
      endcase
    end
  endfunction

  assign csr_check_valid_o  = csr_address_valid(csr_check_addr_i);
  assign csr_access_valid_o = csr_address_valid(csr_access_addr_i);

  always_comb begin
    mip_value = mip_software_q;
    mip_value[3]  = mip_software_q[3]  | irq_software_i;
    mip_value[7]  = mip_software_q[7]  | irq_timer_i;
    mip_value[11] = mip_software_q[11] | irq_external_i;

    csr_access_rdata_o = 32'b0;
    unique case (csr_access_addr_i)
      `UBEE_CSR_MSTATUS:   csr_access_rdata_o = mstatus_q;
      `UBEE_CSR_MIE:       csr_access_rdata_o = mie_q;
      `UBEE_CSR_MTVEC:     csr_access_rdata_o = mtvec_q;
      `UBEE_CSR_MSCRATCH:  csr_access_rdata_o = mscratch_q;
      `UBEE_CSR_MEPC:      csr_access_rdata_o = mepc_q;
      `UBEE_CSR_MCAUSE:    csr_access_rdata_o = mcause_q;
      `UBEE_CSR_MTVAL:     csr_access_rdata_o = mtval_q;
      `UBEE_CSR_MIP:       csr_access_rdata_o = mip_value;
      `UBEE_CSR_MCYCLE:    csr_access_rdata_o = mcycle_q[31:0];
      `UBEE_CSR_MCYCLEH:   csr_access_rdata_o = mcycle_q[63:32];
      `UBEE_CSR_MINSTRET:  csr_access_rdata_o = minstret_q[31:0];
      `UBEE_CSR_MINSTRETH: csr_access_rdata_o = minstret_q[63:32];
      default:             csr_access_rdata_o = 32'b0;
    endcase
  end

  assign irq_ext_pend = mie_q[11] & mip_value[11];
  assign irq_sw_pend  = mie_q[3]  & mip_value[3];
  assign irq_tm_pend  = mie_q[7]  & mip_value[7];
  assign interrupt_request_o = mstatus_q[3] &&
    (irq_ext_pend || irq_sw_pend || irq_tm_pend);

  always_comb begin
    if (irq_ext_pend) begin
      interrupt_cause_o = `UBEE_INTERRUPT_EXTERNAL;
    end else if (irq_sw_pend) begin
      interrupt_cause_o = `UBEE_INTERRUPT_SOFTWARE;
    end else begin
      interrupt_cause_o = `UBEE_INTERRUPT_TIMER;
    end
  end

  assign trap_vector_o = {mtvec_q[31:2], 2'b00};
  assign mepc_o        = mepc_q;

  assign dbg_csr_mstatus_o   = mstatus_q;
  assign dbg_csr_mie_o       = mie_q;
  assign dbg_csr_mtvec_o     = mtvec_q;
  assign dbg_csr_mscratch_o  = mscratch_q;
  assign dbg_csr_mepc_o      = mepc_q;
  assign dbg_csr_mcause_o    = mcause_q;
  assign dbg_csr_mtval_o     = mtval_q;
  assign dbg_csr_mip_o       = mip_value;
  assign dbg_csr_mcycle_o    = mcycle_q[31:0];
  assign dbg_csr_mcycleh_o   = mcycle_q[63:32];
  assign dbg_csr_minstret_o  = minstret_q[31:0];
  assign dbg_csr_minstreth_o = minstret_q[63:32];

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      mstatus_q      <= 32'h0000_1800;
      mie_q          <= 32'b0;
      mtvec_q        <= {MTVEC_RESET[31:2], 2'b00};
      mscratch_q     <= 32'b0;
      mepc_q         <= 32'b0;
      mcause_q       <= 32'b0;
      mtval_q        <= 32'b0;
      mip_software_q <= 32'b0;
      shadow_bank_o  <= 1'b0;
    end else begin
      if (csr_write_i) begin
        unique case (csr_write_addr_i)
          `UBEE_CSR_MSTATUS: begin
            mstatus_q[3]     <= csr_write_data_i[3];
            mstatus_q[7]     <= csr_write_data_i[7];
            mstatus_q[12:11] <= 2'b11;
          end
          `UBEE_CSR_MIE: begin
            mie_q       <= 32'b0;
            mie_q[3]    <= csr_write_data_i[3];
            mie_q[7]    <= csr_write_data_i[7];
            mie_q[11]   <= csr_write_data_i[11];
          end
          `UBEE_CSR_MTVEC:    mtvec_q    <= {csr_write_data_i[31:2], 2'b00};
          `UBEE_CSR_MSCRATCH: mscratch_q <= csr_write_data_i;
          `UBEE_CSR_MEPC:     mepc_q     <= {csr_write_data_i[31:1], 1'b0};
          `UBEE_CSR_MCAUSE:   mcause_q   <= csr_write_data_i;
          `UBEE_CSR_MTVAL:    mtval_q    <= csr_write_data_i;
          `UBEE_CSR_MIP: begin
            mip_software_q       <= 32'b0;
            mip_software_q[3]    <= csr_write_data_i[3];
            mip_software_q[7]    <= csr_write_data_i[7];
            mip_software_q[11]   <= csr_write_data_i[11];
          end
          default: begin end
        endcase
      end

      if (mret_i) begin
        mstatus_q[3]     <= mstatus_q[7];
        mstatus_q[7]     <= 1'b1;
        mstatus_q[12:11] <= 2'b11;
        shadow_bank_o    <= 1'b0;
      end

      if (trap_enter_i) begin
        // IALIGN=16 (C): mepc[0] WARL 0, mepc[1] must be writable.
        mepc_q            <= {trap_epc_i[31:1], 1'b0};
        mcause_q          <= trap_interrupt_i
          ? {1'b1, trap_cause_i[30:0]}
          : {1'b0, trap_cause_i[30:0]};
        mtval_q           <= trap_value_i;
        mstatus_q[7]      <= mstatus_q[3];
        mstatus_q[3]      <= 1'b0;
        mstatus_q[12:11]  <= 2'b11;
        shadow_bank_o     <= SHADOW_BANK_ENABLE;
      end
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      mcycle_q <= 64'b0;
    end else if (csr_write_i && (csr_write_addr_i == `UBEE_CSR_MCYCLE)) begin
      mcycle_q <= {mcycle_q[63:32], csr_write_data_i};
    end else if (csr_write_i && (csr_write_addr_i == `UBEE_CSR_MCYCLEH)) begin
      mcycle_q <= {csr_write_data_i, mcycle_q[31:0]};
    end else begin
      mcycle_q <= mcycle_q + 64'd1;
    end
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      minstret_q <= 64'b0;
    end else if (csr_write_i && (csr_write_addr_i == `UBEE_CSR_MINSTRET)) begin
      minstret_q <= {minstret_q[63:32], csr_write_data_i};
    end else if (csr_write_i && (csr_write_addr_i == `UBEE_CSR_MINSTRETH)) begin
      minstret_q <= {csr_write_data_i, minstret_q[31:0]};
    end else if (instruction_retired_i) begin
      minstret_q <= minstret_q + 64'd1;
    end
  end
endmodule
