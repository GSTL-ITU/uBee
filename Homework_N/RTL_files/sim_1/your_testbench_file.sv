`timescale 1ns/1ps

// Behavioral TB for student Vivado BD wrapper `design_1_wrapper`.
//
// Ports:
//   clk_100MHz, reset_rtl_0 (ACTIVE_HIGH), uart_rtl_0_rxd, uart_rtl_0_txd
//
// IMEM/DMEM init on uBee_soc (Customize IP) — use absolute paths, e.g.:
//   <lesson_root>/Compiled_Assembly_Files/imem.hex
//   <lesson_root>/Compiled_Assembly_Files/dmem.hex
// Relative paths often fail because $readmemh resolves against Vivado's CWD.
//
// UART Lite: 9600 baud @ 100 MHz ACLK
// Expected: "Hello from uBee on Arty A7\r\n" (28 bytes)
//
// Vivado: Simulation top = tb_design_1_hello

module tb_design_1_hello;
  localparam integer CLK_HZ       = 100000000;
  localparam integer BAUD         = 9600;
  localparam integer BIT_TICKS    = CLK_HZ / BAUD;
  localparam integer HALF_BIT     = BIT_TICKS / 2;
  localparam integer EXPECT_CHARS = 28;
  localparam integer RESET_CYCLES = 20000;
  localparam integer TIMEOUT_MS   = 200;

  // ASCII: "Hello from uBee on Arty A7\r\n"
  reg [7:0] HELLO [0:27];

  reg  clk_100MHz  = 1'b0;
  reg  reset_rtl_0 = 1'b1;  // ACTIVE_HIGH
  reg  uart_rxd    = 1'b1;
  wire uart_txd;

  integer i;

  initial begin
    HELLO[ 0] = 8'h48; HELLO[ 1] = 8'h65; HELLO[ 2] = 8'h6C; HELLO[ 3] = 8'h6C;
    HELLO[ 4] = 8'h6F; HELLO[ 5] = 8'h20; HELLO[ 6] = 8'h66; HELLO[ 7] = 8'h72;
    HELLO[ 8] = 8'h6F; HELLO[ 9] = 8'h6D; HELLO[10] = 8'h20; HELLO[11] = 8'h75;
    HELLO[12] = 8'h42; HELLO[13] = 8'h65; HELLO[14] = 8'h65; HELLO[15] = 8'h20;
    HELLO[16] = 8'h6F; HELLO[17] = 8'h6E; HELLO[18] = 8'h20; HELLO[19] = 8'h41;
    HELLO[20] = 8'h72; HELLO[21] = 8'h74; HELLO[22] = 8'h79; HELLO[23] = 8'h20;
    HELLO[24] = 8'h41; HELLO[25] = 8'h37; HELLO[26] = 8'h0D; HELLO[27] = 8'h0A;
  end

  always #5 clk_100MHz = ~clk_100MHz;

  design_1_wrapper dut (
    .clk_100MHz     (clk_100MHz),
    .reset_rtl_0    (reset_rtl_0),
    .uart_rtl_0_rxd (uart_rxd),
    .uart_rtl_0_txd (uart_txd)
  );

  localparam [1:0] ST_IDLE  = 2'd0;
  localparam [1:0] ST_START = 2'd1;
  localparam [1:0] ST_DATA  = 2'd2;
  localparam [1:0] ST_STOP  = 2'd3;

  reg  [1:0] state_q;
  integer    tick_q;
  integer    bit_idx_q;
  integer    char_idx_q;
  reg  [7:0] shift_q;
  reg        rx_q;
  reg        rx_d1_q;
  reg        char_valid;
  reg  [7:0] char_byte;
  reg        fail_q;
  reg        done_q;

  always @(posedge clk_100MHz) begin
    if (reset_rtl_0) begin
      state_q    <= ST_IDLE;
      tick_q     <= 0;
      bit_idx_q  <= 0;
      char_idx_q <= 0;
      shift_q    <= 8'b0;
      rx_q       <= 1'b1;
      rx_d1_q    <= 1'b1;
      char_valid <= 1'b0;
      char_byte  <= 8'b0;
      fail_q     <= 1'b0;
      done_q     <= 1'b0;
    end else begin
      char_valid <= 1'b0;
      rx_d1_q    <= rx_q;
      rx_q       <= uart_txd;

      case (state_q)
        ST_IDLE: begin
          if (rx_d1_q && !rx_q) begin
            tick_q  <= HALF_BIT;
            state_q <= ST_START;
          end
        end
        ST_START: begin
          if (tick_q == 0) begin
            if (!rx_q) begin
              tick_q    <= BIT_TICKS - 1;
              bit_idx_q <= 0;
              shift_q   <= 8'b0;
              state_q   <= ST_DATA;
            end else begin
              state_q <= ST_IDLE;
            end
          end else begin
            tick_q <= tick_q - 1;
          end
        end
        ST_DATA: begin
          if (tick_q == 0) begin
            shift_q[bit_idx_q] <= rx_q;
            if (bit_idx_q == 7) begin
              tick_q  <= BIT_TICKS - 1;
              state_q <= ST_STOP;
            end else begin
              bit_idx_q <= bit_idx_q + 1;
              tick_q    <= BIT_TICKS - 1;
            end
          end else begin
            tick_q <= tick_q - 1;
          end
        end
        default: begin
          if (tick_q == 0) begin
            char_valid <= 1'b1;
            char_byte  <= shift_q;
            state_q    <= ST_IDLE;
          end else begin
            tick_q <= tick_q - 1;
          end
        end
      endcase

      if (char_valid && (char_idx_q < EXPECT_CHARS)) begin
        $write("%c", char_byte);
        if (char_byte != HELLO[char_idx_q]) begin
          $display("\nFAIL: UART idx=%0d exp=%02x got=%02x",
                   char_idx_q, HELLO[char_idx_q], char_byte);
          fail_q <= 1'b1;
        end
        if ((char_idx_q + 1) == EXPECT_CHARS) begin
          done_q <= 1'b1;
        end
        char_idx_q <= char_idx_q + 1;
      end
    end
  end

  integer timeout_cycles;
  integer cycle_i;

  initial begin
    $display("TB: design_1_wrapper UART hello @ %0d baud (CLK=%0d Hz)", BAUD, CLK_HZ);
    reset_rtl_0 = 1'b1;
    repeat (RESET_CYCLES) @(posedge clk_100MHz);
    reset_rtl_0 = 1'b0;
    $display("TB: reset released");

    timeout_cycles = TIMEOUT_MS * 100000;
    for (cycle_i = 0; cycle_i < timeout_cycles; cycle_i = cycle_i + 1) begin
      @(posedge clk_100MHz);
      if (done_q) begin
        repeat (10000) @(posedge clk_100MHz);
        if (fail_q)
          $display("\nFAIL: tb_design_1_hello (character mismatch)");
        else
          $display("\nPASS: tb_design_1_hello");
        $finish;
      end
    end

    $display("\nFAIL: tb_design_1_hello timeout after %0d ms", TIMEOUT_MS);
    $display("  Check IMEM/DMEM init, AXI UART 0x60000000, baud=%0d", BAUD);
    $finish;
  end
endmodule
