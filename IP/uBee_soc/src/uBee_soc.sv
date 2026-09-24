// FPGA / Vivado IP top: core + BRAM + CLINT + PLIC + router + AXI4-Lite master.
// Peripherals (UART, GPIO, ...) are Vivado catalog IPs in Block Design.
`include "uBee_perf_events.svh"

module uBee_soc #(
  parameter logic [31:0] RESET_VECTOR       = 32'h8000_0000,
  parameter logic [31:0] MTVEC_RESET        = 32'h8000_0000,
  parameter bit          SHADOW_BANK_ENABLE = 1'b1,
  parameter bit          AXI_ENABLE         = 1'b1,
  parameter bit          CLINT_ENABLE       = 1'b1,
  parameter bit          PLIC_ENABLE        = 1'b1,
  parameter bit          GPIO_ENABLE        = 1'b1,
  parameter bit          TIMER_ENABLE       = 1'b1,
  parameter bit          UART_ENABLE        = 1'b1,
  parameter bit          MONITOR_ENABLE     = 1'b1,
  parameter bit          MUL_ENABLE         = 1'b1,
  parameter bit          DIV_ENABLE         = 1'b1,
  parameter bit          BP_ENABLE          = 1'b1,
  parameter bit          TRACE_ENABLE       = 1'b1,
  parameter bit          DEBUG_ENABLE       = 1'b1,
  parameter integer      IMEM_WORDS         = 16384,
  parameter integer      DMEM_WORDS         = 16384,
  parameter integer      PLIC_SOURCES       = 16,
  parameter integer      PLIC_PRIO_BITS     = 3,
  parameter bit          IRQ_SYNC_ENABLE    = 1'b1,
  parameter logic [31:0] IRQ_MODE_RESET     = 32'h0000_0000,
  parameter logic [15:0] IRQ_INITIAL_LEVEL  = 16'h0000,
  parameter logic [31:0] IMEM_BASE          = 32'h8000_0000,
  parameter logic [31:0] DMEM_BASE          = 32'h2000_0000,
  parameter logic [31:0] DMEM_MASK          = 32'hFFFF_0000,
  parameter logic [31:0] AXI_BASE           = 32'h6000_0000,
  parameter logic [31:0] AXI_MASK           = 32'hF000_0000,
  parameter logic [31:0] CLINT_BASE         = 32'h4000_0000,
  parameter logic [31:0] CLINT_MASK         = 32'hFFFF_F000,
  parameter logic [31:0] PLIC_BASE          = 32'h4000_1000,
  parameter logic [31:0] PLIC_MASK          = 32'hFFFF_F000,
  parameter logic [31:0] GPIO_BASE          = 32'h4000_2000,
  parameter logic [31:0] GPIO_MASK          = 32'hFFFF_F000,
  parameter logic [31:0] TIMER_BASE         = 32'h4000_3000,
  parameter logic [31:0] TIMER_MASK         = 32'hFFFF_F000,
  parameter logic [31:0] UART_BASE          = 32'h4000_4000,
  parameter logic [31:0] UART_MASK          = 32'hFFFF_F000,
  parameter logic [31:0] MONITOR_BASE       = 32'h4000_5000,
  parameter logic [31:0] MONITOR_MASK      = 32'hFFFF_F000,
  parameter integer      TIMER_IRQ_ID       = 2,
  parameter integer      UART_IRQ_ID        = 3,
  parameter integer      GPIO_WIDTH         = 32,
  parameter IMEM_INIT_FILE = "",
  parameter DMEM_INIT_FILE = "",
  parameter int unsigned BP_ENTRIES = 128,
  parameter bit BP_GSHARE = 1'b0,
  parameter int unsigned GHR_WIDTH = 5
) (
  input  logic        clk_i,
  input  logic        rst_ni,

  // External interrupt sources: bit n -> PLIC source ID (n+1)
  input  logic [PLIC_SOURCES-1:0] ext_irq_i,

  input  logic [GPIO_WIDTH-1:0] gpio_in_i = '0,
  output logic [GPIO_WIDTH-1:0] gpio_out_o,
  output logic [GPIO_WIDTH-1:0] gpio_oe_o,
  output logic                  uart_tx_o,
  input  logic                  uart_rx_i = 1'b1,

  // AXI4-Lite master
  output logic [31:0] m_axi_awaddr_o,
  output logic        m_axi_awvalid_o,
  input  logic        m_axi_awready_i,
  output logic [2:0]  m_axi_awprot_o,

  output logic [31:0] m_axi_wdata_o,
  output logic [3:0]  m_axi_wstrb_o,
  output logic        m_axi_wvalid_o,
  input  logic        m_axi_wready_i,

  input  logic [1:0]  m_axi_bresp_i,
  input  logic        m_axi_bvalid_i,
  output logic        m_axi_bready_o,

  output logic [31:0] m_axi_araddr_o,
  output logic        m_axi_arvalid_o,
  input  logic        m_axi_arready_i,
  output logic [2:0]  m_axi_arprot_o,

  input  logic [31:0] m_axi_rdata_i,
  input  logic [1:0]  m_axi_rresp_i,
  input  logic        m_axi_rvalid_i,
  output logic        m_axi_rready_o,

  // Optional commit trace (tie off in IP when TRACE_ENABLE=0)
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

  // Continuous debug observe (tie off in IP when DEBUG_ENABLE=0)
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
  output logic [31:0] dbg_csr_minstreth_o
`ifdef SIMULATION
  ,
  input logic        tb_dmem_we_i    = 1'b0,
  input logic [31:0] tb_dmem_widx_i  = 32'b0,
  input logic [31:0] tb_dmem_wdata_i = 32'b0
`endif
);
  logic rst_n_sync_q;
  logic rst_n_meta_q;

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      rst_n_meta_q <= 1'b0;
      rst_n_sync_q <= 1'b0;
    end else begin
      rst_n_meta_q <= 1'b1;
      rst_n_sync_q <= rst_n_meta_q;
    end
  end

  logic        imem_req_valid;
  logic        imem_req_ready;
  logic [31:0] imem_req_addr;
  logic        imem_rsp_valid;
  logic        imem_rsp_ready;
  logic [31:0] imem_rsp_rdata;
  logic        imem_rsp_err;

  logic        dmem_req_valid;
  logic        dmem_req_ready;
  logic        dmem_req_write;
  logic [31:0] dmem_req_addr;
  logic [31:0] dmem_req_wdata;
  logic [3:0]  dmem_req_wstrb;
  logic        dmem_rsp_valid;
  logic        dmem_rsp_ready;
  logic [31:0] dmem_rsp_rdata;
  logic        dmem_rsp_err;

  logic        irq_software;
  logic        irq_timer;
  logic        irq_external;

  logic        trace_valid;
  logic [31:0] trace_pc;
  logic [31:0] trace_instruction;
  logic        trace_rd_write;
  logic [4:0]  trace_rd_addr;
  logic [31:0] trace_rd_data;
  logic        trace_mem_valid;
  logic        trace_mem_write;
  logic [31:0] trace_mem_addr;
  logic [31:0] trace_mem_wdata;
  logic [3:0]  trace_mem_wstrb;
  logic        trace_trap;
  logic [31:0] trace_cause;

  logic [31:0] dbg_fetch_pc;
  logic [31:0] dbg_if_pc;
  logic        dbg_if_valid;
  logic [31:0] dbg_ex_pc;
  logic        dbg_ex_valid;
  logic [31:0] dbg_mem_pc;
  logic        dbg_mem_valid;
  logic [31:0] dbg_wb_pc;
  logic        dbg_wb_valid;
  logic        dbg_rf_we;
  logic [4:0]  dbg_rf_waddr;
  logic [31:0] dbg_rf_wdata;
  logic [31:0] dbg_csr_mstatus;
  logic [31:0] dbg_csr_mie;
  logic [31:0] dbg_csr_mtvec;
  logic [31:0] dbg_csr_mscratch;
  logic [31:0] dbg_csr_mepc;
  logic [31:0] dbg_csr_mcause;
  logic [31:0] dbg_csr_mtval;
  logic [31:0] dbg_csr_mip;
  logic [31:0] dbg_csr_mcycle;
  logic [31:0] dbg_csr_mcycleh;
  logic [31:0] dbg_csr_minstret;
  logic [31:0] dbg_csr_minstreth;

  logic        local_dmem_req_valid;
  logic        local_dmem_req_ready;
  logic        local_dmem_req_write;
  logic [31:0] local_dmem_req_addr;
  logic [31:0] local_dmem_req_wdata;
  logic [3:0]  local_dmem_req_wstrb;
  logic        local_dmem_rsp_valid;
  logic        local_dmem_rsp_ready;
  logic [31:0] local_dmem_rsp_rdata;
  logic        local_dmem_rsp_err;

  logic        axi_req_valid;
  logic        axi_req_ready;
  logic        axi_req_write;
  logic [31:0] axi_req_addr;
  logic [31:0] axi_req_wdata;
  logic [3:0]  axi_req_wstrb;
  logic        axi_rsp_valid;
  logic        axi_rsp_ready;
  logic [31:0] axi_rsp_rdata;
  logic        axi_rsp_err;

  logic        clint_req_valid;
  logic        clint_req_ready;
  logic        clint_req_write;
  logic [31:0] clint_req_addr;
  logic [31:0] clint_req_wdata;
  logic [3:0]  clint_req_wstrb;
  logic        clint_rsp_valid;
  logic        clint_rsp_ready;
  logic [31:0] clint_rsp_rdata;
  logic        clint_rsp_err;

  logic        plic_req_valid;
  logic        plic_req_ready;
  logic        plic_req_write;
  logic [31:0] plic_req_addr;
  logic [31:0] plic_req_wdata;
  logic [3:0]  plic_req_wstrb;
  logic        plic_rsp_valid;
  logic        plic_rsp_ready;
  logic [31:0] plic_rsp_rdata;
  logic        plic_rsp_err;

  logic        gpio_req_valid;
  logic        gpio_req_ready;
  logic        gpio_req_write;
  logic [31:0] gpio_req_addr;
  logic [31:0] gpio_req_wdata;
  logic [3:0]  gpio_req_wstrb;
  logic        gpio_rsp_valid;
  logic        gpio_rsp_ready;
  logic [31:0] gpio_rsp_rdata;
  logic        gpio_rsp_err;

  logic        timer_req_valid;
  logic        timer_req_ready;
  logic        timer_req_write;
  logic [31:0] timer_req_addr;
  logic [31:0] timer_req_wdata;
  logic [3:0]  timer_req_wstrb;
  logic        timer_rsp_valid;
  logic        timer_rsp_ready;
  logic [31:0] timer_rsp_rdata;
  logic        timer_rsp_err;
  logic        timer_irq;

  logic        uart_req_valid;
  logic        uart_req_ready;
  logic        uart_req_write;
  logic [31:0] uart_req_addr;
  logic [31:0] uart_req_wdata;
  logic [3:0]  uart_req_wstrb;
  logic        uart_rsp_valid;
  logic        uart_rsp_ready;
  logic [31:0] uart_rsp_rdata;
  logic        uart_rsp_err;
  logic        native_uart_irq;

  logic        monitor_req_valid;
  logic        monitor_req_ready;
  logic        monitor_req_write;
  logic [31:0] monitor_req_addr;
  logic [31:0] monitor_req_wdata;
  logic [3:0]  monitor_req_wstrb;
  logic        monitor_rsp_valid;
  logic        monitor_rsp_ready;
  logic [31:0] monitor_rsp_rdata;
  logic        monitor_rsp_err;

  uBee_perf_events_t core_perf_events;
  uBee_perf_events_t monitor_perf_events;
  logic              axi_bridge_busy;
  logic              axi_wait_level;

  logic [PLIC_SOURCES-1:0] plic_irq_combined;

  uBee_top #(
    .RESET_VECTOR       (RESET_VECTOR),
    .MTVEC_RESET        (MTVEC_RESET),
    .SHADOW_BANK_ENABLE (SHADOW_BANK_ENABLE),
    .MUL_ENABLE         (MUL_ENABLE),
    .DIV_ENABLE         (DIV_ENABLE),
    .BP_ENABLE          (BP_ENABLE),
    .DEBUG_ENABLE       (DEBUG_ENABLE),
    .DMEM_BASE          (DMEM_BASE),
    .DMEM_MASK          (DMEM_MASK),
    .BP_ENTRIES         (BP_ENTRIES),
    .BP_GSHARE          (BP_GSHARE),
    .GHR_WIDTH          (GHR_WIDTH)
  ) u_top (
    .clk_i               (clk_i),
    .rst_ni              (rst_n_sync_q),
    .imem_req_valid_o    (imem_req_valid),
    .imem_req_ready_i    (imem_req_ready),
    .imem_req_addr_o     (imem_req_addr),
    .imem_rsp_valid_i    (imem_rsp_valid),
    .imem_rsp_ready_o    (imem_rsp_ready),
    .imem_rsp_rdata_i    (imem_rsp_rdata),
    .imem_rsp_err_i      (imem_rsp_err),
    .dmem_req_valid_o    (dmem_req_valid),
    .dmem_req_ready_i    (dmem_req_ready),
    .dmem_req_write_o    (dmem_req_write),
    .dmem_req_addr_o     (dmem_req_addr),
    .dmem_req_wdata_o    (dmem_req_wdata),
    .dmem_req_wstrb_o    (dmem_req_wstrb),
    .dmem_rsp_valid_i    (dmem_rsp_valid),
    .dmem_rsp_ready_o    (dmem_rsp_ready),
    .dmem_rsp_rdata_i    (dmem_rsp_rdata),
    .dmem_rsp_err_i      (dmem_rsp_err),
    .irq_software_i      (irq_software),
    .irq_timer_i         (irq_timer),
    .irq_external_i      (irq_external),
    .trace_valid_o       (trace_valid),
    .trace_pc_o          (trace_pc),
    .trace_instruction_o (trace_instruction),
    .trace_rd_write_o    (trace_rd_write),
    .trace_rd_addr_o     (trace_rd_addr),
    .trace_rd_data_o     (trace_rd_data),
    .trace_mem_valid_o   (trace_mem_valid),
    .trace_mem_write_o   (trace_mem_write),
    .trace_mem_addr_o    (trace_mem_addr),
    .trace_mem_wdata_o   (trace_mem_wdata),
    .trace_mem_wstrb_o   (trace_mem_wstrb),
    .trace_trap_o        (trace_trap),
    .trace_cause_o       (trace_cause),
    .dbg_fetch_pc_o      (dbg_fetch_pc),
    .dbg_if_pc_o         (dbg_if_pc),
    .dbg_if_valid_o      (dbg_if_valid),
    .dbg_ex_pc_o         (dbg_ex_pc),
    .dbg_ex_valid_o      (dbg_ex_valid),
    .dbg_mem_pc_o        (dbg_mem_pc),
    .dbg_mem_valid_o     (dbg_mem_valid),
    .dbg_wb_pc_o         (dbg_wb_pc),
    .dbg_wb_valid_o      (dbg_wb_valid),
    .dbg_rf_we_o         (dbg_rf_we),
    .dbg_rf_waddr_o      (dbg_rf_waddr),
    .dbg_rf_wdata_o      (dbg_rf_wdata),
    .dbg_csr_mstatus_o   (dbg_csr_mstatus),
    .dbg_csr_mie_o       (dbg_csr_mie),
    .dbg_csr_mtvec_o     (dbg_csr_mtvec),
    .dbg_csr_mscratch_o  (dbg_csr_mscratch),
    .dbg_csr_mepc_o      (dbg_csr_mepc),
    .dbg_csr_mcause_o    (dbg_csr_mcause),
    .dbg_csr_mtval_o     (dbg_csr_mtval),
    .dbg_csr_mip_o       (dbg_csr_mip),
    .dbg_csr_mcycle_o    (dbg_csr_mcycle),
    .dbg_csr_mcycleh_o   (dbg_csr_mcycleh),
    .dbg_csr_minstret_o  (dbg_csr_minstret),
    .dbg_csr_minstreth_o (dbg_csr_minstreth),
    .perf_events_o       (core_perf_events)
  );

  always_comb begin
    monitor_perf_events = core_perf_events;
    monitor_perf_events.axi_wait = axi_wait_level;
  end

  assign axi_wait_level = AXI_ENABLE &&
    ((axi_req_valid && !axi_req_ready) || axi_bridge_busy);

  uBee_imem #(
    .WORDS        (IMEM_WORDS),
    .BASE_ADDRESS (IMEM_BASE),
    .INIT_FILE    (IMEM_INIT_FILE)
  ) u_imem (
    .clk_i       (clk_i),
    .rst_ni      (rst_n_sync_q),
    .req_valid_i (imem_req_valid),
    .req_ready_o (imem_req_ready),
    .req_addr_i  (imem_req_addr),
    .rsp_valid_o (imem_rsp_valid),
    .rsp_ready_i (imem_rsp_ready),
    .rsp_rdata_o (imem_rsp_rdata),
    .rsp_err_o   (imem_rsp_err)
  );

  uBee_request_router #(
    .DMEM_BASE     (DMEM_BASE),
    .DMEM_MASK     (DMEM_MASK),
    .AXI_BASE      (AXI_BASE),
    .AXI_MASK      (AXI_MASK),
    .CLINT_BASE    (CLINT_BASE),
    .CLINT_MASK    (CLINT_MASK),
    .PLIC_BASE     (PLIC_BASE),
    .PLIC_MASK     (PLIC_MASK),
    .GPIO_BASE     (GPIO_BASE),
    .GPIO_MASK     (GPIO_MASK),
    .TIMER_BASE    (TIMER_BASE),
    .TIMER_MASK    (TIMER_MASK),
    .UART_BASE     (UART_BASE),
    .UART_MASK     (UART_MASK),
    .MONITOR_BASE  (MONITOR_BASE),
    .MONITOR_MASK  (MONITOR_MASK),
    .AXI_ENABLE    (AXI_ENABLE),
    .CLINT_ENABLE  (CLINT_ENABLE),
    .PLIC_ENABLE   (PLIC_ENABLE),
    .GPIO_ENABLE   (GPIO_ENABLE),
    .TIMER_ENABLE  (TIMER_ENABLE),
    .UART_ENABLE   (UART_ENABLE),
    .MONITOR_ENABLE(MONITOR_ENABLE)
  ) u_router (
    .clk_i             (clk_i),
    .rst_ni            (rst_n_sync_q),
    .req_valid_i       (dmem_req_valid),
    .req_ready_o       (dmem_req_ready),
    .req_write_i       (dmem_req_write),
    .req_addr_i        (dmem_req_addr),
    .req_wdata_i       (dmem_req_wdata),
    .req_wstrb_i       (dmem_req_wstrb),
    .rsp_valid_o       (dmem_rsp_valid),
    .rsp_ready_i       (dmem_rsp_ready),
    .rsp_rdata_o       (dmem_rsp_rdata),
    .rsp_err_o         (dmem_rsp_err),
    .dmem_req_valid_o  (local_dmem_req_valid),
    .dmem_req_ready_i  (local_dmem_req_ready),
    .dmem_req_write_o  (local_dmem_req_write),
    .dmem_req_addr_o   (local_dmem_req_addr),
    .dmem_req_wdata_o  (local_dmem_req_wdata),
    .dmem_req_wstrb_o  (local_dmem_req_wstrb),
    .dmem_rsp_valid_i  (local_dmem_rsp_valid),
    .dmem_rsp_ready_o  (local_dmem_rsp_ready),
    .dmem_rsp_rdata_i  (local_dmem_rsp_rdata),
    .dmem_rsp_err_i    (local_dmem_rsp_err),
    .axi_req_valid_o   (axi_req_valid),
    .axi_req_ready_i   (axi_req_ready),
    .axi_req_write_o   (axi_req_write),
    .axi_req_addr_o    (axi_req_addr),
    .axi_req_wdata_o   (axi_req_wdata),
    .axi_req_wstrb_o   (axi_req_wstrb),
    .axi_rsp_valid_i   (axi_rsp_valid),
    .axi_rsp_ready_o   (axi_rsp_ready),
    .axi_rsp_rdata_i   (axi_rsp_rdata),
    .axi_rsp_err_i     (axi_rsp_err),
    .clint_req_valid_o (clint_req_valid),
    .clint_req_ready_i (clint_req_ready),
    .clint_req_write_o (clint_req_write),
    .clint_req_addr_o  (clint_req_addr),
    .clint_req_wdata_o (clint_req_wdata),
    .clint_req_wstrb_o (clint_req_wstrb),
    .clint_rsp_valid_i (clint_rsp_valid),
    .clint_rsp_ready_o (clint_rsp_ready),
    .clint_rsp_rdata_i (clint_rsp_rdata),
    .clint_rsp_err_i   (clint_rsp_err),
    .plic_req_valid_o  (plic_req_valid),
    .plic_req_ready_i  (plic_req_ready),
    .plic_req_write_o  (plic_req_write),
    .plic_req_addr_o   (plic_req_addr),
    .plic_req_wdata_o  (plic_req_wdata),
    .plic_req_wstrb_o  (plic_req_wstrb),
    .plic_rsp_valid_i  (plic_rsp_valid),
    .plic_rsp_ready_o  (plic_rsp_ready),
    .plic_rsp_rdata_i  (plic_rsp_rdata),
    .plic_rsp_err_i    (plic_rsp_err),
    .gpio_req_valid_o  (gpio_req_valid),
    .gpio_req_ready_i  (gpio_req_ready),
    .gpio_req_write_o  (gpio_req_write),
    .gpio_req_addr_o   (gpio_req_addr),
    .gpio_req_wdata_o  (gpio_req_wdata),
    .gpio_req_wstrb_o  (gpio_req_wstrb),
    .gpio_rsp_valid_i  (gpio_rsp_valid),
    .gpio_rsp_ready_o  (gpio_rsp_ready),
    .gpio_rsp_rdata_i  (gpio_rsp_rdata),
    .gpio_rsp_err_i    (gpio_rsp_err),
    .timer_req_valid_o (timer_req_valid),
    .timer_req_ready_i (timer_req_ready),
    .timer_req_write_o (timer_req_write),
    .timer_req_addr_o  (timer_req_addr),
    .timer_req_wdata_o (timer_req_wdata),
    .timer_req_wstrb_o (timer_req_wstrb),
    .timer_rsp_valid_i (timer_rsp_valid),
    .timer_rsp_ready_o (timer_rsp_ready),
    .timer_rsp_rdata_i (timer_rsp_rdata),
    .timer_rsp_err_i   (timer_rsp_err),
    .uart_req_valid_o  (uart_req_valid),
    .uart_req_ready_i  (uart_req_ready),
    .uart_req_write_o  (uart_req_write),
    .uart_req_addr_o   (uart_req_addr),
    .uart_req_wdata_o  (uart_req_wdata),
    .uart_req_wstrb_o  (uart_req_wstrb),
    .uart_rsp_valid_i  (uart_rsp_valid),
    .uart_rsp_ready_o  (uart_rsp_ready),
    .uart_rsp_rdata_i  (uart_rsp_rdata),
    .uart_rsp_err_i    (uart_rsp_err),
    .monitor_req_valid_o (monitor_req_valid),
    .monitor_req_ready_i (monitor_req_ready),
    .monitor_req_write_o (monitor_req_write),
    .monitor_req_addr_o  (monitor_req_addr),
    .monitor_req_wdata_o (monitor_req_wdata),
    .monitor_req_wstrb_o (monitor_req_wstrb),
    .monitor_rsp_valid_i (monitor_rsp_valid),
    .monitor_rsp_ready_o (monitor_rsp_ready),
    .monitor_rsp_rdata_i (monitor_rsp_rdata),
    .monitor_rsp_err_i   (monitor_rsp_err)
  );

  uBee_dmem #(
    .WORDS        (DMEM_WORDS),
    .BASE_ADDRESS (DMEM_BASE),
    .INIT_FILE    (DMEM_INIT_FILE)
  ) u_dmem (
    .clk_i       (clk_i),
    .rst_ni      (rst_n_sync_q),
    .req_valid_i (local_dmem_req_valid),
    .req_ready_o (local_dmem_req_ready),
    .req_write_i (local_dmem_req_write),
    .req_addr_i  (local_dmem_req_addr),
    .req_wdata_i (local_dmem_req_wdata),
    .req_wstrb_i (local_dmem_req_wstrb),
    .rsp_valid_o (local_dmem_rsp_valid),
    .rsp_ready_i (local_dmem_rsp_ready),
    .rsp_rdata_o (local_dmem_rsp_rdata),
    .rsp_err_o   (local_dmem_rsp_err)
`ifdef SIMULATION
    ,
    .tb_mem_we_i    (tb_dmem_we_i),
    .tb_mem_widx_i  (tb_dmem_widx_i),
    .tb_mem_wdata_i (tb_dmem_wdata_i)
`endif
  );

  if (CLINT_ENABLE) begin : g_clint
    uBee_clint #(
      .BASE_ADDRESS (CLINT_BASE)
    ) u_clint (
      .clk_i          (clk_i),
      .rst_ni         (rst_n_sync_q),
      .req_valid_i    (clint_req_valid),
      .req_ready_o    (clint_req_ready),
      .req_write_i    (clint_req_write),
      .req_addr_i     (clint_req_addr),
      .req_wdata_i    (clint_req_wdata),
      .req_wstrb_i    (clint_req_wstrb),
      .rsp_valid_o    (clint_rsp_valid),
      .rsp_ready_i    (clint_rsp_ready),
      .rsp_rdata_o    (clint_rsp_rdata),
      .rsp_err_o      (clint_rsp_err),
      .irq_software_o (irq_software),
      .irq_timer_o    (irq_timer)
    );
  end else begin : g_no_clint
    assign clint_req_ready = 1'b0;
    assign clint_rsp_valid = 1'b0;
    assign clint_rsp_rdata = 32'b0;
    assign clint_rsp_err   = 1'b1;
    assign irq_software    = 1'b0;
    assign irq_timer       = 1'b0;
  end

  if (PLIC_ENABLE) begin : g_plic
    uBee_plic #(
      .NUM_SOURCES       (PLIC_SOURCES),
      .PRIO_BITS         (PLIC_PRIO_BITS),
      .IRQ_SYNC_ENABLE   (IRQ_SYNC_ENABLE),
      .IRQ_MODE_RESET    (IRQ_MODE_RESET),
      .IRQ_INITIAL_LEVEL (IRQ_INITIAL_LEVEL),
      .BASE_ADDRESS      (PLIC_BASE)
    ) u_plic (
      .clk_i           (clk_i),
      .rst_ni          (rst_n_sync_q),
      .req_valid_i     (plic_req_valid),
      .req_ready_o     (plic_req_ready),
      .req_write_i     (plic_req_write),
      .req_addr_i      (plic_req_addr),
      .req_wdata_i     (plic_req_wdata),
      .req_wstrb_i     (plic_req_wstrb),
      .rsp_valid_o     (plic_rsp_valid),
      .rsp_ready_i     (plic_rsp_ready),
      .rsp_rdata_o     (plic_rsp_rdata),
      .rsp_err_o       (plic_rsp_err),
      .ext_irq_i       (plic_irq_combined),
      .irq_external_o  (irq_external)
    );
  end else begin : g_no_plic
    assign plic_req_ready = 1'b0;
    assign plic_rsp_valid = 1'b0;
    assign plic_rsp_rdata = 32'b0;
    assign plic_rsp_err   = 1'b1;
    assign irq_external   = 1'b0;
  end

  always_comb begin
    plic_irq_combined = ext_irq_i;
    if (TIMER_ENABLE && (TIMER_IRQ_ID >= 1) && (TIMER_IRQ_ID <= PLIC_SOURCES)) begin
      plic_irq_combined[TIMER_IRQ_ID - 1] =
        ext_irq_i[TIMER_IRQ_ID - 1] | timer_irq;
    end
    if (UART_ENABLE && (UART_IRQ_ID >= 1) && (UART_IRQ_ID <= PLIC_SOURCES)) begin
      plic_irq_combined[UART_IRQ_ID - 1] =
        ext_irq_i[UART_IRQ_ID - 1] | native_uart_irq;
    end
  end

  if (GPIO_ENABLE) begin : g_gpio
    uBee_gpio #(
      .BASE_ADDRESS (GPIO_BASE),
      .GPIO_WIDTH   (GPIO_WIDTH)
    ) u_gpio (
      .clk_i       (clk_i),
      .rst_ni      (rst_n_sync_q),
      .req_valid_i (gpio_req_valid),
      .req_ready_o (gpio_req_ready),
      .req_write_i (gpio_req_write),
      .req_addr_i  (gpio_req_addr),
      .req_wdata_i (gpio_req_wdata),
      .req_wstrb_i (gpio_req_wstrb),
      .rsp_valid_o (gpio_rsp_valid),
      .rsp_ready_i (gpio_rsp_ready),
      .rsp_rdata_o (gpio_rsp_rdata),
      .rsp_err_o   (gpio_rsp_err),
      .gpio_in_i   (gpio_in_i),
      .gpio_out_o  (gpio_out_o),
      .gpio_oe_o   (gpio_oe_o)
    );
  end else begin : g_no_gpio
    assign gpio_req_ready = 1'b0;
    assign gpio_rsp_valid = 1'b0;
    assign gpio_rsp_rdata = 32'b0;
    assign gpio_rsp_err   = 1'b1;
    assign gpio_out_o     = '0;
    assign gpio_oe_o      = '0;
  end

  if (TIMER_ENABLE) begin : g_timer
    uBee_timer #(
      .BASE_ADDRESS (TIMER_BASE)
    ) u_timer (
      .clk_i       (clk_i),
      .rst_ni      (rst_n_sync_q),
      .req_valid_i (timer_req_valid),
      .req_ready_o (timer_req_ready),
      .req_write_i (timer_req_write),
      .req_addr_i  (timer_req_addr),
      .req_wdata_i (timer_req_wdata),
      .req_wstrb_i (timer_req_wstrb),
      .rsp_valid_o (timer_rsp_valid),
      .rsp_ready_i (timer_rsp_ready),
      .rsp_rdata_o (timer_rsp_rdata),
      .rsp_err_o   (timer_rsp_err),
      .irq_o       (timer_irq)
    );
  end else begin : g_no_timer
    assign timer_req_ready = 1'b0;
    assign timer_rsp_valid = 1'b0;
    assign timer_rsp_rdata = 32'b0;
    assign timer_rsp_err   = 1'b1;
    assign timer_irq       = 1'b0;
  end

  if (UART_ENABLE) begin : g_uart
    uBee_uart #(
      .BASE_ADDRESS (UART_BASE)
    ) u_uart (
      .clk_i       (clk_i),
      .rst_ni      (rst_n_sync_q),
      .req_valid_i (uart_req_valid),
      .req_ready_o (uart_req_ready),
      .req_write_i (uart_req_write),
      .req_addr_i  (uart_req_addr),
      .req_wdata_i (uart_req_wdata),
      .req_wstrb_i (uart_req_wstrb),
      .rsp_valid_o (uart_rsp_valid),
      .rsp_ready_i (uart_rsp_ready),
      .rsp_rdata_o (uart_rsp_rdata),
      .rsp_err_o   (uart_rsp_err),
      .uart_tx_o   (uart_tx_o),
      .uart_rx_i   (uart_rx_i),
      .irq_o       (native_uart_irq)
    );
  end else begin : g_no_uart
    assign uart_req_ready    = 1'b0;
    assign uart_rsp_valid    = 1'b0;
    assign uart_rsp_rdata    = 32'b0;
    assign uart_rsp_err      = 1'b1;
    assign uart_tx_o         = 1'b1;
    assign native_uart_irq   = 1'b0;
  end

  if (MONITOR_ENABLE) begin : g_monitor
    uBee_monitor #(
      .BASE_ADDRESS (MONITOR_BASE),
      .AXI_ENABLE   (AXI_ENABLE),
      .BP_ENABLE    (BP_ENABLE)
    ) u_monitor (
      .clk_i          (clk_i),
      .rst_ni         (rst_n_sync_q),
      .req_valid_i    (monitor_req_valid),
      .req_ready_o    (monitor_req_ready),
      .req_write_i    (monitor_req_write),
      .req_addr_i     (monitor_req_addr),
      .req_wdata_i    (monitor_req_wdata),
      .req_wstrb_i    (monitor_req_wstrb),
      .rsp_valid_o    (monitor_rsp_valid),
      .rsp_ready_i    (monitor_rsp_ready),
      .rsp_rdata_o    (monitor_rsp_rdata),
      .rsp_err_o      (monitor_rsp_err),
      .perf_events_i  (monitor_perf_events)
    );
  end else begin : g_no_monitor
    assign monitor_req_ready = 1'b0;
    assign monitor_rsp_valid = 1'b0;
    assign monitor_rsp_rdata = 32'b0;
    assign monitor_rsp_err   = 1'b1;
  end

  if (AXI_ENABLE) begin : g_axi
    uBee_native_to_axi4lite u_axi_bridge (
      .clk_i           (clk_i),
      .rst_ni          (rst_n_sync_q),
      .req_valid_i     (axi_req_valid),
      .req_ready_o     (axi_req_ready),
      .req_write_i     (axi_req_write),
      .req_addr_i      (axi_req_addr),
      .req_wdata_i     (axi_req_wdata),
      .req_wstrb_i     (axi_req_wstrb),
      .rsp_valid_o     (axi_rsp_valid),
      .rsp_ready_i     (axi_rsp_ready),
      .rsp_rdata_o     (axi_rsp_rdata),
      .rsp_err_o       (axi_rsp_err),
      .m_axi_awaddr_o  (m_axi_awaddr_o),
      .m_axi_awvalid_o (m_axi_awvalid_o),
      .m_axi_awready_i (m_axi_awready_i),
      .m_axi_awprot_o  (m_axi_awprot_o),
      .m_axi_wdata_o   (m_axi_wdata_o),
      .m_axi_wstrb_o   (m_axi_wstrb_o),
      .m_axi_wvalid_o  (m_axi_wvalid_o),
      .m_axi_wready_i  (m_axi_wready_i),
      .m_axi_bresp_i   (m_axi_bresp_i),
      .m_axi_bvalid_i  (m_axi_bvalid_i),
      .m_axi_bready_o  (m_axi_bready_o),
      .m_axi_araddr_o  (m_axi_araddr_o),
      .m_axi_arvalid_o (m_axi_arvalid_o),
      .m_axi_arready_i (m_axi_arready_i),
      .m_axi_arprot_o  (m_axi_arprot_o),
      .m_axi_rdata_i   (m_axi_rdata_i),
      .m_axi_rresp_i   (m_axi_rresp_i),
      .m_axi_rvalid_i  (m_axi_rvalid_i),
      .m_axi_rready_o  (m_axi_rready_o),
      .busy_o          (axi_bridge_busy)
    );
  end else begin : g_no_axi
    assign axi_bridge_busy = 1'b0;
    assign axi_req_ready   = 1'b0;
    assign axi_rsp_valid   = 1'b0;
    assign axi_rsp_rdata   = 32'b0;
    assign axi_rsp_err     = 1'b1;
    assign m_axi_awaddr_o  = 32'b0;
    assign m_axi_awvalid_o = 1'b0;
    assign m_axi_awprot_o  = 3'b0;
    assign m_axi_wdata_o   = 32'b0;
    assign m_axi_wstrb_o   = 4'b0;
    assign m_axi_wvalid_o  = 1'b0;
    assign m_axi_bready_o  = 1'b0;
    assign m_axi_araddr_o  = 32'b0;
    assign m_axi_arvalid_o = 1'b0;
    assign m_axi_arprot_o  = 3'b0;
    assign m_axi_rready_o  = 1'b0;
  end

  if (TRACE_ENABLE) begin : g_trace
    assign trace_valid_o       = trace_valid;
    assign trace_pc_o          = trace_pc;
    assign trace_instruction_o = trace_instruction;
    assign trace_rd_write_o    = trace_rd_write;
    assign trace_rd_addr_o     = trace_rd_addr;
    assign trace_rd_data_o     = trace_rd_data;
    assign trace_mem_valid_o   = trace_mem_valid;
    assign trace_mem_write_o   = trace_mem_write;
    assign trace_mem_addr_o    = trace_mem_addr;
    assign trace_mem_wdata_o   = trace_mem_wdata;
    assign trace_mem_wstrb_o   = trace_mem_wstrb;
    assign trace_trap_o        = trace_trap;
    assign trace_cause_o       = trace_cause;
  end else begin : g_no_trace
    assign trace_valid_o       = 1'b0;
    assign trace_pc_o          = 32'b0;
    assign trace_instruction_o = 32'b0;
    assign trace_rd_write_o    = 1'b0;
    assign trace_rd_addr_o     = 5'b0;
    assign trace_rd_data_o     = 32'b0;
    assign trace_mem_valid_o   = 1'b0;
    assign trace_mem_write_o   = 1'b0;
    assign trace_mem_addr_o    = 32'b0;
    assign trace_mem_wdata_o   = 32'b0;
    assign trace_mem_wstrb_o   = 4'b0;
    assign trace_trap_o        = 1'b0;
    assign trace_cause_o       = 32'b0;
  end

  if (DEBUG_ENABLE) begin : g_debug
    assign dbg_fetch_pc_o      = dbg_fetch_pc;
    assign dbg_if_pc_o         = dbg_if_pc;
    assign dbg_if_valid_o      = dbg_if_valid;
    assign dbg_ex_pc_o         = dbg_ex_pc;
    assign dbg_ex_valid_o      = dbg_ex_valid;
    assign dbg_mem_pc_o        = dbg_mem_pc;
    assign dbg_mem_valid_o     = dbg_mem_valid;
    assign dbg_wb_pc_o         = dbg_wb_pc;
    assign dbg_wb_valid_o      = dbg_wb_valid;
    assign dbg_rf_we_o         = dbg_rf_we;
    assign dbg_rf_waddr_o      = dbg_rf_waddr;
    assign dbg_rf_wdata_o      = dbg_rf_wdata;
    assign dbg_csr_mstatus_o   = dbg_csr_mstatus;
    assign dbg_csr_mie_o       = dbg_csr_mie;
    assign dbg_csr_mtvec_o     = dbg_csr_mtvec;
    assign dbg_csr_mscratch_o  = dbg_csr_mscratch;
    assign dbg_csr_mepc_o      = dbg_csr_mepc;
    assign dbg_csr_mcause_o    = dbg_csr_mcause;
    assign dbg_csr_mtval_o     = dbg_csr_mtval;
    assign dbg_csr_mip_o       = dbg_csr_mip;
    assign dbg_csr_mcycle_o    = dbg_csr_mcycle;
    assign dbg_csr_mcycleh_o   = dbg_csr_mcycleh;
    assign dbg_csr_minstret_o  = dbg_csr_minstret;
    assign dbg_csr_minstreth_o = dbg_csr_minstreth;
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
  end
endmodule
