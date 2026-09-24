// Native UART with parametrizable FIFOs, 8N1, 16x RX oversampling (TRM).
`timescale 1ns/1ps
module uBee_uart #(
  parameter logic [31:0] BASE_ADDRESS = 32'h4000_4000,
  parameter integer      FIFO_DEPTH   = 16
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

  output logic        uart_tx_o,
  input  logic        uart_rx_i,
  output logic        irq_o
);
  /* verilator lint_off UNUSEDPARAM */
  localparam logic [31:0] UNUSED_BASE = BASE_ADDRESS;
  /* verilator lint_on UNUSEDPARAM */

  localparam integer FIFO_PTR_W = (FIFO_DEPTH <= 2) ? 1 :
    ((FIFO_DEPTH <= 4) ? 2 : ((FIFO_DEPTH <= 8) ? 3 : 4));

  localparam logic [11:0] OFF_DATA       = 12'h000;
  localparam logic [11:0] OFF_STATUS     = 12'h004;
  localparam logic [11:0] OFF_CTRL       = 12'h008;
  localparam logic [11:0] OFF_BAUDDIV    = 12'h00C;
  localparam logic [11:0] OFF_IRQ_STATUS = 12'h010;
  localparam logic [11:0] OFF_FIFO_LEVEL = 12'h014;
  localparam logic [11:0] OFF_CAPABILITY = 12'h018;

  logic        tx_en_q;
  logic        rx_en_q;
  logic        rx_irq_en_q;
  logic        tx_irq_en_q;
  logic        err_irq_en_q;
  logic        loopback_q;
  logic [31:0] bauddiv_q;

  logic [7:0]  tx_fifo_mem [FIFO_DEPTH-1:0];
  logic [FIFO_PTR_W-1:0] tx_head_q;
  logic [FIFO_PTR_W-1:0] tx_tail_q;
  logic [FIFO_PTR_W:0]   tx_count_q;

  logic [7:0]  rx_fifo_mem [FIFO_DEPTH-1:0];
  logic [FIFO_PTR_W-1:0] rx_head_q;
  logic [FIFO_PTR_W-1:0] rx_tail_q;
  logic [FIFO_PTR_W:0]   rx_count_q;

  logic        overrun_q;
  logic        frame_err_q;
  logic        irq_rx_q;
  logic        irq_tx_q;
  logic        irq_err_q;

  logic        tx_busy_q;
  logic [3:0]  tx_bit_idx_q;
  logic [9:0]  tx_shift_q;
  logic [31:0] tx_baud_cnt_q;

  logic [31:0] rx_baud_cnt_q;
  logic [3:0]  rx_sample_idx_q;
  logic [7:0]  rx_shift_q;
  logic        rx_active_q;
  logic        rx_prev_q;

  logic        rsp_valid_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;
  logic        write_stall_q;

  logic [11:0] offset;
  logic        word_aligned;
  logic        request_fire;
  logic [31:0] read_data;
  logic        reg_hit;
  logic        tx_full;
  logic        tx_empty;
  logic        rx_full;
  logic        rx_valid;
  logic        rx_sample;
  logic        rx_bit;
  logic        tx_tick;

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
  assign req_ready_o  = (!rsp_valid_q || rsp_ready_i) && !write_stall_q;
  assign request_fire = req_valid_i && req_ready_o;
  assign rsp_valid_o  = rsp_valid_q;
  assign rsp_rdata_o  = rsp_rdata_q;
  assign rsp_err_o    = rsp_err_q;

  assign tx_full  = (tx_count_q == FIFO_DEPTH);
  assign tx_empty = (tx_count_q == 0);
  assign rx_full  = (rx_count_q == FIFO_DEPTH);
  assign rx_valid = (rx_count_q != 0);

  assign rx_sample = loopback_q ? uart_tx_o : uart_rx_i;
  assign rx_bit    = rx_sample;

  assign tx_tick = (bauddiv_q != 32'b0) && (tx_baud_cnt_q >= (bauddiv_q - 32'd1));
  assign irq_o   = (irq_rx_q && rx_irq_en_q) ||
                   (irq_tx_q && tx_irq_en_q) ||
                   (irq_err_q && err_irq_en_q);

  always_comb begin
    read_data = 32'b0;
    reg_hit   = 1'b0;
    unique case (offset)
      OFF_DATA: begin
        read_data = rx_valid ? {24'b0, rx_fifo_mem[rx_tail_q]} : 32'b0;
        reg_hit   = 1'b1;
      end
      OFF_STATUS: begin
        read_data = {22'b0, frame_err_q, overrun_q, tx_busy_q, tx_full,
                     tx_empty, rx_full, rx_valid};
        reg_hit = 1'b1;
      end
      OFF_CTRL: begin
        read_data = {15'b0, loopback_q, 5'b0, err_irq_en_q, tx_irq_en_q,
                     rx_irq_en_q, 6'b0, rx_en_q, tx_en_q};
        reg_hit = 1'b1;
      end
      OFF_BAUDDIV: begin read_data = bauddiv_q; reg_hit = 1'b1; end
      OFF_IRQ_STATUS: begin
        read_data = {22'b0, irq_err_q, irq_rx_q, irq_tx_q};
        reg_hit = 1'b1;
      end
      OFF_FIFO_LEVEL: begin
        read_data = {tx_count_q[7:0], 8'b0, rx_count_q[7:0]};
        reg_hit = 1'b1;
      end
      OFF_CAPABILITY: begin
        read_data = {16'b0, FIFO_DEPTH[15:0]};
        reg_hit = 1'b1;
      end
      default: begin end
    endcase
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      tx_en_q        <= 1'b0;
      rx_en_q        <= 1'b0;
      rx_irq_en_q    <= 1'b0;
      tx_irq_en_q    <= 1'b0;
      err_irq_en_q   <= 1'b0;
      loopback_q     <= 1'b0;
      bauddiv_q      <= 32'd434;
      tx_head_q      <= '0;
      tx_tail_q      <= '0;
      tx_count_q     <= '0;
      rx_head_q      <= '0;
      rx_tail_q      <= '0;
      rx_count_q     <= '0;
      overrun_q      <= 1'b0;
      frame_err_q    <= 1'b0;
      irq_rx_q       <= 1'b0;
      irq_tx_q       <= 1'b0;
      irq_err_q      <= 1'b0;
      tx_busy_q      <= 1'b0;
      tx_bit_idx_q   <= 4'd0;
      tx_shift_q     <= 10'b1111111111;
      tx_baud_cnt_q  <= 32'b0;
      rx_baud_cnt_q  <= 32'b0;
      rx_sample_idx_q<= 4'd0;
      rx_shift_q     <= 8'b0;
      rx_active_q    <= 1'b0;
      rx_prev_q      <= 1'b1;
      rsp_valid_q    <= 1'b0;
      rsp_rdata_q    <= 32'b0;
      rsp_err_q      <= 1'b0;
      write_stall_q  <= 1'b0;
      uart_tx_o      <= 1'b1;
    end else begin
      write_stall_q <= 1'b0;

      // TX serializer
      if (tx_tick) begin
        tx_baud_cnt_q <= 32'b0;
      end else if (tx_busy_q || !tx_empty) begin
        tx_baud_cnt_q <= tx_baud_cnt_q + 32'd1;
      end

      if (!tx_busy_q && !tx_empty && tx_en_q && bauddiv_q >= 32'd8) begin
        tx_busy_q    <= 1'b1;
        tx_bit_idx_q <= 4'd0;
        tx_shift_q   <= {1'b1, tx_fifo_mem[tx_tail_q], 1'b0};
        tx_tail_q    <= tx_tail_q + FIFO_PTR_W'(1);
        tx_count_q   <= tx_count_q - FIFO_PTR_W'(1);
        if (tx_irq_en_q && (tx_count_q <= 1)) begin
          irq_tx_q <= 1'b1;
        end
      end else if (tx_busy_q && tx_tick) begin
        uart_tx_o <= tx_shift_q[0];
        tx_shift_q <= {1'b1, tx_shift_q[9:1]};
        if (tx_bit_idx_q == 4'd9) begin
          tx_busy_q    <= 1'b0;
          tx_bit_idx_q <= 4'd0;
        end else begin
          tx_bit_idx_q <= tx_bit_idx_q + 4'd1;
        end
      end else if (!tx_busy_q) begin
        uart_tx_o <= 1'b1;
      end

      // RX oversampler (16x baud)
      if (rx_en_q && bauddiv_q >= 32'd8) begin
        logic [31:0] rx_tick_div;
        rx_tick_div = bauddiv_q >> 2;
        if (rx_baud_cnt_q >= rx_tick_div) begin
          rx_baud_cnt_q <= 32'b0;
          if (!rx_active_q && !rx_bit) begin
            rx_active_q    <= 1'b1;
            rx_sample_idx_q<= 4'd0;
            rx_shift_q     <= 8'b0;
          end else if (rx_active_q) begin
            if (rx_sample_idx_q == 4'd7) begin
              rx_shift_q[rx_sample_idx_q[2:0]] <= rx_bit;
            end else if (rx_sample_idx_q == 4'd15) begin
              if (rx_bit) begin
                frame_err_q <= 1'b1;
                irq_err_q   <= 1'b1;
              end else begin
                if (rx_full) begin
                  overrun_q <= 1'b1;
                  irq_err_q <= 1'b1;
                end else begin
                  rx_fifo_mem[rx_head_q] <= rx_shift_q;
                  rx_head_q              <= rx_head_q + FIFO_PTR_W'(1);
                  rx_count_q             <= rx_count_q + FIFO_PTR_W'(1);
                  irq_rx_q               <= 1'b1;
                end
              end
              rx_active_q <= 1'b0;
            end else if (rx_sample_idx_q >= 4'd1 && rx_sample_idx_q <= 4'd8) begin
              rx_shift_q[rx_sample_idx_q[2:0] - 1] <= rx_bit;
            end
            rx_sample_idx_q <= rx_sample_idx_q + 4'd1;
          end
        end else begin
          rx_baud_cnt_q <= rx_baud_cnt_q + 32'd1;
        end
      end
      rx_prev_q <= rx_bit;

      if (request_fire && word_aligned && req_write_i && reg_hit) begin
        unique case (offset)
          OFF_DATA: begin
            if (tx_full) begin
              write_stall_q <= 1'b1;
            end else begin
              tx_fifo_mem[tx_head_q] <= req_wdata_i[7:0];
              tx_head_q              <= tx_head_q + FIFO_PTR_W'(1);
              tx_count_q             <= tx_count_q + FIFO_PTR_W'(1);
            end
          end
          OFF_CTRL: begin
            // xvlog: cannot index a function call result with [...] — mask instead.
            tx_en_q      <= |(apply_wstrb({31'b0, tx_en_q}, req_wdata_i, req_wstrb_i) & 32'h1);
            rx_en_q      <= |(apply_wstrb({31'b0, rx_en_q}, req_wdata_i, req_wstrb_i) & 32'h2);
            rx_irq_en_q  <= |(apply_wstrb({31'b0, rx_irq_en_q}, req_wdata_i, req_wstrb_i) & 32'h100);
            tx_irq_en_q  <= |(apply_wstrb({31'b0, tx_irq_en_q}, req_wdata_i, req_wstrb_i) & 32'h200);
            err_irq_en_q <= |(apply_wstrb({31'b0, err_irq_en_q}, req_wdata_i, req_wstrb_i) & 32'h400);
            loopback_q   <= |(apply_wstrb({31'b0, loopback_q}, req_wdata_i, req_wstrb_i) & 32'h10000);
          end
          OFF_BAUDDIV: bauddiv_q <= apply_wstrb(bauddiv_q, req_wdata_i, req_wstrb_i);
          OFF_IRQ_STATUS: begin
            if (req_wstrb_i[0] && req_wdata_i[0]) irq_tx_q <= 1'b0;
            if (req_wstrb_i[0] && req_wdata_i[1]) irq_rx_q <= 1'b0;
            if (req_wstrb_i[0] && req_wdata_i[2]) irq_err_q <= 1'b0;
            if (req_wstrb_i[0] && req_wdata_i[8]) overrun_q <= 1'b0;
            if (req_wstrb_i[0] && req_wdata_i[9]) frame_err_q <= 1'b0;
          end
          default: begin end
        endcase
      end

      if (request_fire && word_aligned && !req_write_i && offset == OFF_DATA &&
          reg_hit && !rx_valid) begin
        write_stall_q <= 1'b1;
      end else if (request_fire && word_aligned && !req_write_i &&
                   offset == OFF_DATA && reg_hit && rx_valid) begin
        rx_tail_q  <= rx_tail_q + FIFO_PTR_W'(1);
        rx_count_q <= rx_count_q - FIFO_PTR_W'(1);
        if (rx_count_q == 1) irq_rx_q <= 1'b0;
      end

      if (rsp_valid_q && rsp_ready_i) begin
        rsp_valid_q <= 1'b0;
      end

      if (request_fire && !write_stall_q) begin
        rsp_valid_q <= 1'b1;
        rsp_err_q   <= !word_aligned || !reg_hit;
        rsp_rdata_q <= (word_aligned && reg_hit) ? read_data : 32'b0;
      end
    end
  end
endmodule
