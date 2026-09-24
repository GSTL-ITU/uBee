`timescale 1ns/1ps
// Routes a single native initiator to CLINT, PLIC, GPIO, TIMER, UART, DMEM, AXI, or error.
// At most one outstanding transaction (matches uBee_core DMEM behaviour).
module uBee_request_router #(
  parameter logic [31:0] DMEM_BASE     = 32'h2000_0000,
  parameter logic [31:0] DMEM_MASK     = 32'hFFFF_0000,
  parameter logic [31:0] AXI_BASE      = 32'h6000_0000,
  parameter logic [31:0] AXI_MASK      = 32'hF000_0000,
  parameter logic [31:0] CLINT_BASE    = 32'h4000_0000,
  parameter logic [31:0] CLINT_MASK    = 32'hFFFF_F000,
  parameter logic [31:0] PLIC_BASE     = 32'h4000_1000,
  parameter logic [31:0] PLIC_MASK     = 32'hFFFF_F000,
  parameter logic [31:0] GPIO_BASE     = 32'h4000_2000,
  parameter logic [31:0] GPIO_MASK     = 32'hFFFF_F000,
  parameter logic [31:0] TIMER_BASE    = 32'h4000_3000,
  parameter logic [31:0] TIMER_MASK    = 32'hFFFF_F000,
  parameter logic [31:0] UART_BASE     = 32'h4000_4000,
  parameter logic [31:0] UART_MASK     = 32'hFFFF_F000,
  parameter logic [31:0] MONITOR_BASE  = 32'h4000_5000,
  parameter logic [31:0] MONITOR_MASK = 32'hFFFF_F000,
  parameter logic [31:0] IMEM_ALIAS_BASE = 32'h8000_0000,
  parameter logic [31:0] IMEM_ALIAS_MASK = 32'hFFFF_0000,
  parameter bit          AXI_ENABLE    = 1'b1,
  parameter bit          CLINT_ENABLE  = 1'b1,
  parameter bit          PLIC_ENABLE   = 1'b1,
  parameter bit          GPIO_ENABLE   = 1'b1,
  parameter bit          TIMER_ENABLE  = 1'b1,
  parameter bit          UART_ENABLE   = 1'b1,
  parameter bit          MONITOR_ENABLE = 1'b1,
  parameter bit          IMEM_ALIAS_ENABLE = 1'b0
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

  output logic        axi_req_valid_o,
  input  logic        axi_req_ready_i,
  output logic        axi_req_write_o,
  output logic [31:0] axi_req_addr_o,
  output logic [31:0] axi_req_wdata_o,
  output logic [3:0]  axi_req_wstrb_o,
  input  logic        axi_rsp_valid_i,
  output logic        axi_rsp_ready_o,
  input  logic [31:0] axi_rsp_rdata_i,
  input  logic        axi_rsp_err_i,

  output logic        clint_req_valid_o,
  input  logic        clint_req_ready_i,
  output logic        clint_req_write_o,
  output logic [31:0] clint_req_addr_o,
  output logic [31:0] clint_req_wdata_o,
  output logic [3:0]  clint_req_wstrb_o,
  input  logic        clint_rsp_valid_i,
  output logic        clint_rsp_ready_o,
  input  logic [31:0] clint_rsp_rdata_i,
  input  logic        clint_rsp_err_i,

  output logic        plic_req_valid_o,
  input  logic        plic_req_ready_i,
  output logic        plic_req_write_o,
  output logic [31:0] plic_req_addr_o,
  output logic [31:0] plic_req_wdata_o,
  output logic [3:0]  plic_req_wstrb_o,
  input  logic        plic_rsp_valid_i,
  output logic        plic_rsp_ready_o,
  input  logic [31:0] plic_rsp_rdata_i,
  input  logic        plic_rsp_err_i,

  output logic        gpio_req_valid_o,
  input  logic        gpio_req_ready_i,
  output logic        gpio_req_write_o,
  output logic [31:0] gpio_req_addr_o,
  output logic [31:0] gpio_req_wdata_o,
  output logic [3:0]  gpio_req_wstrb_o,
  input  logic        gpio_rsp_valid_i,
  output logic        gpio_rsp_ready_o,
  input  logic [31:0] gpio_rsp_rdata_i,
  input  logic        gpio_rsp_err_i,

  output logic        timer_req_valid_o,
  input  logic        timer_req_ready_i,
  output logic        timer_req_write_o,
  output logic [31:0] timer_req_addr_o,
  output logic [31:0] timer_req_wdata_o,
  output logic [3:0]  timer_req_wstrb_o,
  input  logic        timer_rsp_valid_i,
  output logic        timer_rsp_ready_o,
  input  logic [31:0] timer_rsp_rdata_i,
  input  logic        timer_rsp_err_i,

  output logic        uart_req_valid_o,
  input  logic        uart_req_ready_i,
  output logic        uart_req_write_o,
  output logic [31:0] uart_req_addr_o,
  output logic [31:0] uart_req_wdata_o,
  output logic [3:0]  uart_req_wstrb_o,
  input  logic        uart_rsp_valid_i,
  output logic        uart_rsp_ready_o,
  input  logic [31:0] uart_rsp_rdata_i,
  input  logic        uart_rsp_err_i,

  output logic        monitor_req_valid_o,
  input  logic        monitor_req_ready_i,
  output logic        monitor_req_write_o,
  output logic [31:0] monitor_req_addr_o,
  output logic [31:0] monitor_req_wdata_o,
  output logic [3:0]  monitor_req_wstrb_o,
  input  logic        monitor_rsp_valid_i,
  output logic        monitor_rsp_ready_o,
  input  logic [31:0] monitor_rsp_rdata_i,
  input  logic        monitor_rsp_err_i
);
  typedef enum logic [3:0] {
    SEL_NONE  = 4'd0,
    SEL_CLINT = 4'd1,
    SEL_PLIC  = 4'd2,
    SEL_GPIO  = 4'd3,
    SEL_TIMER = 4'd4,
    SEL_UART  = 4'd5,
    SEL_MONITOR = 4'd6,
    SEL_DMEM  = 4'd7,
    SEL_AXI   = 4'd8,
    SEL_ERR   = 4'd9
  } sel_e;

  logic clint_hit;
  logic plic_hit;
  logic gpio_hit;
  logic timer_hit;
  logic uart_hit;
  logic monitor_addr_match;
  logic monitor_hit;
  logic imem_alias_hit;
  logic dmem_hit;
  logic axi_hit;
  sel_e   decode_sel;
  sel_e   pending_sel_q;
  logic   err_rsp_valid_q;
  logic   request_fire;
  logic   waiting_rsp;

  assign clint_hit = CLINT_ENABLE &&
                     ((req_addr_i & CLINT_MASK) == (CLINT_BASE & CLINT_MASK));
  assign plic_hit  = PLIC_ENABLE &&
                     ((req_addr_i & PLIC_MASK) == (PLIC_BASE & PLIC_MASK));
  assign gpio_hit  = GPIO_ENABLE &&
                     ((req_addr_i & GPIO_MASK) == (GPIO_BASE & GPIO_MASK));
  assign timer_hit = TIMER_ENABLE &&
                     ((req_addr_i & TIMER_MASK) == (TIMER_BASE & TIMER_MASK));
  assign uart_hit  = UART_ENABLE &&
                     ((req_addr_i & UART_MASK) == (UART_BASE & UART_MASK));
  assign monitor_addr_match =
    ((req_addr_i & MONITOR_MASK) == (MONITOR_BASE & MONITOR_MASK));
  assign monitor_hit = MONITOR_ENABLE && monitor_addr_match;
  assign imem_alias_hit = IMEM_ALIAS_ENABLE &&
    ((req_addr_i & IMEM_ALIAS_MASK) == (IMEM_ALIAS_BASE & IMEM_ALIAS_MASK));
  assign dmem_hit  = ((req_addr_i & DMEM_MASK) == (DMEM_BASE & DMEM_MASK)) ||
                     imem_alias_hit;
  assign axi_hit   = AXI_ENABLE &&
                     ((req_addr_i & AXI_MASK) == (AXI_BASE & AXI_MASK));

  // Decode priority: CLINT -> PLIC -> GPIO -> TIMER -> UART -> MONITOR -> DMEM -> AXI -> ERR
  always_comb begin
    if (clint_hit) begin
      decode_sel = SEL_CLINT;
    end else if (plic_hit) begin
      decode_sel = SEL_PLIC;
    end else if (gpio_hit) begin
      decode_sel = SEL_GPIO;
    end else if (timer_hit) begin
      decode_sel = SEL_TIMER;
    end else if (uart_hit) begin
      decode_sel = SEL_UART;
    end else if (monitor_addr_match && !MONITOR_ENABLE) begin
      decode_sel = SEL_ERR;
    end else if (monitor_hit) begin
      decode_sel = SEL_MONITOR;
    end else if (dmem_hit) begin
      decode_sel = SEL_DMEM;
    end else if (axi_hit) begin
      decode_sel = SEL_AXI;
    end else begin
      decode_sel = SEL_ERR;
    end
  end

  assign waiting_rsp = (pending_sel_q != SEL_NONE);

  always_comb begin
    req_ready_o = 1'b0;
    unique case (decode_sel)
      SEL_CLINT: req_ready_o = !waiting_rsp && clint_req_ready_i;
      SEL_PLIC:  req_ready_o = !waiting_rsp && plic_req_ready_i;
      SEL_GPIO:  req_ready_o = !waiting_rsp && gpio_req_ready_i;
      SEL_TIMER: req_ready_o = !waiting_rsp && timer_req_ready_i;
      SEL_UART:  req_ready_o = !waiting_rsp && uart_req_ready_i;
      SEL_MONITOR: req_ready_o = !waiting_rsp && monitor_req_ready_i;
      SEL_DMEM:  req_ready_o = !waiting_rsp && dmem_req_ready_i;
      SEL_AXI:   req_ready_o = !waiting_rsp && axi_req_ready_i;
      default:   req_ready_o = !waiting_rsp && (!err_rsp_valid_q || rsp_ready_i);
    endcase
  end

  assign request_fire = req_valid_i && req_ready_o;

  assign clint_req_valid_o = request_fire && (decode_sel == SEL_CLINT);
  assign clint_req_write_o = req_write_i;
  assign clint_req_addr_o  = req_addr_i;
  assign clint_req_wdata_o = req_wdata_i;
  assign clint_req_wstrb_o = req_wstrb_i;

  assign plic_req_valid_o = request_fire && (decode_sel == SEL_PLIC);
  assign plic_req_write_o = req_write_i;
  assign plic_req_addr_o  = req_addr_i;
  assign plic_req_wdata_o = req_wdata_i;
  assign plic_req_wstrb_o = req_wstrb_i;

  assign gpio_req_valid_o = request_fire && (decode_sel == SEL_GPIO);
  assign gpio_req_write_o = req_write_i;
  assign gpio_req_addr_o  = req_addr_i;
  assign gpio_req_wdata_o = req_wdata_i;
  assign gpio_req_wstrb_o = req_wstrb_i;

  assign timer_req_valid_o = request_fire && (decode_sel == SEL_TIMER);
  assign timer_req_write_o = req_write_i;
  assign timer_req_addr_o  = req_addr_i;
  assign timer_req_wdata_o = req_wdata_i;
  assign timer_req_wstrb_o = req_wstrb_i;

  assign uart_req_valid_o = request_fire && (decode_sel == SEL_UART);
  assign uart_req_write_o = req_write_i;
  assign uart_req_addr_o  = req_addr_i;
  assign uart_req_wdata_o = req_wdata_i;
  assign uart_req_wstrb_o = req_wstrb_i;

  assign monitor_req_valid_o = request_fire && (decode_sel == SEL_MONITOR);
  assign monitor_req_write_o = req_write_i;
  assign monitor_req_addr_o  = req_addr_i;
  assign monitor_req_wdata_o = req_wdata_i;
  assign monitor_req_wstrb_o = req_wstrb_i;

  assign dmem_req_valid_o = request_fire && (decode_sel == SEL_DMEM);
  assign dmem_req_write_o = req_write_i;
  assign dmem_req_addr_o  = req_addr_i;
  assign dmem_req_wdata_o = req_wdata_i;
  assign dmem_req_wstrb_o = req_wstrb_i;

  assign axi_req_valid_o = request_fire && (decode_sel == SEL_AXI);
  assign axi_req_write_o = req_write_i;
  assign axi_req_addr_o  = req_addr_i;
  assign axi_req_wdata_o = req_wdata_i;
  assign axi_req_wstrb_o = req_wstrb_i;

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      pending_sel_q   <= SEL_NONE;
      err_rsp_valid_q <= 1'b0;
    end else begin
      if (request_fire) begin
        pending_sel_q <= decode_sel;
        if (decode_sel == SEL_ERR) begin
          err_rsp_valid_q <= 1'b1;
        end
      end

      if (pending_sel_q == SEL_CLINT && clint_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_PLIC && plic_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_GPIO && gpio_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_TIMER && timer_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_UART && uart_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_MONITOR && monitor_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_DMEM && dmem_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_AXI && axi_rsp_valid_i && rsp_ready_i) begin
        pending_sel_q <= SEL_NONE;
      end else if (pending_sel_q == SEL_ERR && err_rsp_valid_q && rsp_ready_i) begin
        pending_sel_q   <= SEL_NONE;
        err_rsp_valid_q <= 1'b0;
      end
    end
  end

  always_comb begin
    rsp_valid_o       = 1'b0;
    rsp_rdata_o       = 32'b0;
    rsp_err_o         = 1'b0;
    clint_rsp_ready_o = 1'b0;
    plic_rsp_ready_o  = 1'b0;
    gpio_rsp_ready_o  = 1'b0;
    timer_rsp_ready_o = 1'b0;
    uart_rsp_ready_o  = 1'b0;
    monitor_rsp_ready_o = 1'b0;
    dmem_rsp_ready_o  = 1'b0;
    axi_rsp_ready_o   = 1'b0;

    unique case (pending_sel_q)
      SEL_CLINT: begin
        rsp_valid_o       = clint_rsp_valid_i;
        rsp_rdata_o       = clint_rsp_rdata_i;
        rsp_err_o         = clint_rsp_err_i;
        clint_rsp_ready_o = rsp_ready_i;
      end
      SEL_PLIC: begin
        rsp_valid_o      = plic_rsp_valid_i;
        rsp_rdata_o      = plic_rsp_rdata_i;
        rsp_err_o        = plic_rsp_err_i;
        plic_rsp_ready_o = rsp_ready_i;
      end
      SEL_GPIO: begin
        rsp_valid_o      = gpio_rsp_valid_i;
        rsp_rdata_o      = gpio_rsp_rdata_i;
        rsp_err_o        = gpio_rsp_err_i;
        gpio_rsp_ready_o = rsp_ready_i;
      end
      SEL_TIMER: begin
        rsp_valid_o       = timer_rsp_valid_i;
        rsp_rdata_o       = timer_rsp_rdata_i;
        rsp_err_o         = timer_rsp_err_i;
        timer_rsp_ready_o = rsp_ready_i;
      end
      SEL_UART: begin
        rsp_valid_o      = uart_rsp_valid_i;
        rsp_rdata_o      = uart_rsp_rdata_i;
        rsp_err_o        = uart_rsp_err_i;
        uart_rsp_ready_o = rsp_ready_i;
      end
      SEL_MONITOR: begin
        rsp_valid_o         = monitor_rsp_valid_i;
        rsp_rdata_o         = monitor_rsp_rdata_i;
        rsp_err_o           = monitor_rsp_err_i;
        monitor_rsp_ready_o = rsp_ready_i;
      end
      SEL_DMEM: begin
        rsp_valid_o      = dmem_rsp_valid_i;
        rsp_rdata_o      = dmem_rsp_rdata_i;
        rsp_err_o        = dmem_rsp_err_i;
        dmem_rsp_ready_o = rsp_ready_i;
      end
      SEL_AXI: begin
        rsp_valid_o     = axi_rsp_valid_i;
        rsp_rdata_o     = axi_rsp_rdata_i;
        rsp_err_o       = axi_rsp_err_i;
        axi_rsp_ready_o = rsp_ready_i;
      end
      SEL_ERR: begin
        rsp_valid_o = err_rsp_valid_q;
        rsp_rdata_o = 32'b0;
        rsp_err_o   = 1'b1;
      end
      default: begin
      end
    endcase
  end
endmodule
