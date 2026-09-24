// Native valid/ready → AXI4-Lite master (single outstanding).
// AW/W may handshake independently; native write response waits for B.
module uBee_native_to_axi4lite (
  input  logic        clk_i,
  input  logic        rst_ni,

  // Native initiator (from router)
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

  output logic        busy_o
);
  typedef enum logic [2:0] {
    ST_IDLE       = 3'd0,
    ST_WR_ISSUE   = 3'd1,
    ST_WR_WAIT_B  = 3'd2,
    ST_RD_ISSUE   = 3'd3,
    ST_RD_WAIT_R  = 3'd4,
    ST_RSP        = 3'd5
  } state_e;

  state_e state_q;
  logic [31:0] addr_q;
  logic [31:0] wdata_q;
  logic [3:0]  wstrb_q;
  logic        aw_done_q;
  logic        w_done_q;
  logic [31:0] rsp_rdata_q;
  logic        rsp_err_q;

  assign m_axi_awprot_o = 3'b000;
  assign m_axi_arprot_o = 3'b000;

  assign m_axi_awaddr_o  = addr_q;
  assign m_axi_wdata_o   = wdata_q;
  assign m_axi_wstrb_o   = wstrb_q;
  assign m_axi_araddr_o  = addr_q;

  assign m_axi_awvalid_o = (state_q == ST_WR_ISSUE) && !aw_done_q;
  assign m_axi_wvalid_o  = (state_q == ST_WR_ISSUE) && !w_done_q;
  assign m_axi_bready_o  = (state_q == ST_WR_WAIT_B);
  assign m_axi_arvalid_o = (state_q == ST_RD_ISSUE);
  assign m_axi_rready_o  = (state_q == ST_RD_WAIT_R);

  assign req_ready_o = (state_q == ST_IDLE);
  assign rsp_valid_o = (state_q == ST_RSP);
  assign rsp_rdata_o = rsp_rdata_q;
  assign rsp_err_o   = rsp_err_q;
  assign busy_o      = (state_q != ST_IDLE);

  always_ff @(posedge clk_i) begin
    if (!rst_ni) begin
      state_q     <= ST_IDLE;
      addr_q      <= 32'b0;
      wdata_q     <= 32'b0;
      wstrb_q     <= 4'b0;
      aw_done_q   <= 1'b0;
      w_done_q    <= 1'b0;
      rsp_rdata_q <= 32'b0;
      rsp_err_q   <= 1'b0;
    end else begin
      unique case (state_q)
        ST_IDLE: begin
          if (req_valid_i) begin
            addr_q    <= req_addr_i;
            wdata_q   <= req_wdata_i;
            wstrb_q   <= req_wstrb_i;
            aw_done_q <= 1'b0;
            w_done_q  <= 1'b0;
            if (req_write_i) begin
              state_q <= ST_WR_ISSUE;
            end else begin
              state_q <= ST_RD_ISSUE;
            end
          end
        end

        ST_WR_ISSUE: begin
          if (!aw_done_q && m_axi_awvalid_o && m_axi_awready_i) begin
            aw_done_q <= 1'b1;
          end
          if (!w_done_q && m_axi_wvalid_o && m_axi_wready_i) begin
            w_done_q <= 1'b1;
          end
          if ((aw_done_q || (m_axi_awvalid_o && m_axi_awready_i)) &&
              (w_done_q  || (m_axi_wvalid_o  && m_axi_wready_i))) begin
            state_q <= ST_WR_WAIT_B;
          end
        end

        ST_WR_WAIT_B: begin
          if (m_axi_bvalid_i && m_axi_bready_o) begin
            rsp_rdata_q <= 32'b0;
            rsp_err_q   <= (m_axi_bresp_i != 2'b00);
            state_q     <= ST_RSP;
          end
        end

        ST_RD_ISSUE: begin
          if (m_axi_arvalid_o && m_axi_arready_i) begin
            state_q <= ST_RD_WAIT_R;
          end
        end

        ST_RD_WAIT_R: begin
          if (m_axi_rvalid_i && m_axi_rready_o) begin
            rsp_rdata_q <= m_axi_rdata_i;
            rsp_err_q   <= (m_axi_rresp_i != 2'b00);
            state_q     <= ST_RSP;
          end
        end

        ST_RSP: begin
          if (rsp_ready_i) begin
            state_q <= ST_IDLE;
          end
        end

        default: state_q <= ST_IDLE;
      endcase
    end
  end
endmodule
