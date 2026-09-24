`timescale 1ns/1ps
`include "uBee_defs.svh"

// RVC (C extension) decompressor for RV32IMC.
// Converts a 16-bit compressed instruction to the equivalent 32-bit form.
// If the input is not a compressed instruction (bits[1:0] == 2'b11),
// the output is the input passed through unchanged.
// All illegal/reserved encodings output 32'b0 (illegal instruction).
module uBee_decompress (
  input  logic [31:0] instr_i,          // raw fetched word
  output logic [31:0] instr_o,          // expanded 32-bit instruction
  output logic        is_compressed_o   // 1 when a 16-bit instruction was expanded
);
  logic [15:0] c;
  assign c = instr_i[15:0];

  // Compressed field aliases
  logic [4:0] rs1, rs2, rd;
  logic [4:0] rs1p, rs2p, rdp;   // x8..x15 subset

  assign rs1  = c[11:7];
  assign rs2  = c[6:2];
  assign rd   = c[11:7];
  assign rdp  = {2'b01, c[4:2]};
  assign rs1p = {2'b01, c[9:7]};
  assign rs2p = {2'b01, c[4:2]};

  always_comb begin
    instr_o         = 32'b0;
    is_compressed_o = 1'b1;

    if (c[1:0] == 2'b11) begin
      // 32-bit instruction — pass through unchanged
      instr_o         = instr_i;
      is_compressed_o = 1'b0;
    end else begin
      case (c[1:0])

        // ----------------------------------------------------------------
        // Quadrant 0  (op = 2'b00)
        // ----------------------------------------------------------------
        2'b00: begin
          case (c[15:13])
            3'b000: begin
              // C.ADDI4SPN → ADDI rdp, x2, nzuimm
              // nzuimm = {c[10:7], c[12:11], c[5], c[6]} << 2
              logic [9:0] uimm;
              uimm = {c[10:7], c[12:11], c[5], c[6], 2'b00};
              instr_o = (uimm == 10'b0) ? 32'b0 :
                        {2'b00, uimm, 5'd2, 3'b000, rdp, 7'b0010011};
            end
            3'b010: begin
              // C.LW → LW rdp, offset(rs1p)
              // offset = {c[5], c[12:10], c[6]} << 2  (7 bits)
              logic [6:0] off;
              off = {c[5], c[12:10], c[6], 2'b00};
              instr_o = {{5{1'b0}}, off, rs1p, 3'b010, rdp, 7'b0000011};
            end
            3'b110: begin
              // C.SW → SW rs2p, offset(rs1p)
              // offset = {c[5], c[12:10], c[6]} << 2  (7 bits, zero-extended)
              // SW encoding: imm[11:5] | rs2 | rs1 | 010 | imm[4:0] | 0100011
              logic [6:0] off;
              off = {c[5], c[12:10], c[6], 2'b00};
              instr_o = {5'b0, off[6:5], rs2p, rs1p, 3'b010, off[4:0], 7'b0100011};
            end
            default: instr_o = 32'b0;
          endcase
        end

        // ----------------------------------------------------------------
        // Quadrant 1  (op = 2'b01)
        // ----------------------------------------------------------------
        2'b01: begin
          case (c[15:13])
            3'b000: begin
              // C.ADDI → ADDI rd, rd, nzimm
              logic [5:0] nzimm;
              nzimm = {c[12], c[6:2]};
              instr_o = {{6{nzimm[5]}}, nzimm, rd, 3'b000, rd, 7'b0010011};
            end
            3'b001: begin
              // C.JAL (RV32 only) → JAL x1, offset
              // CJ offset[11:1] (LSB always 0); stored as joff[20:1]
              logic [20:1] joff;
              joff = {{9{c[12]}}, c[12], c[8], c[10:9], c[6], c[7], c[2], c[11], c[5:3]};
              // JAL encoding: imm[20|10:1|11|19:12] | rd | opcode
              instr_o = {joff[20], joff[10:1], joff[11], joff[19:12], 5'd1, 7'b1101111};
            end
            3'b010: begin
              // C.LI → ADDI rd, x0, imm
              logic [5:0] imm;
              imm = {c[12], c[6:2]};
              instr_o = {{6{imm[5]}}, imm, 5'd0, 3'b000, rd, 7'b0010011};
            end
            3'b011: begin
              if (rd == 5'd2) begin
                // C.ADDI16SP → ADDI x2, x2, nzimm×16
                logic [9:0] nzimm;
                nzimm = {c[12], c[4:3], c[5], c[2], c[6], 4'b0};
                instr_o = (nzimm == 10'b0) ? 32'b0 :
                          {{2{nzimm[9]}}, nzimm, 5'd2, 3'b000, 5'd2, 7'b0010011};
              end else begin
                // C.LUI → LUI rd, nzimm  (nzimm = {c[12],c[6:2]} as upper 20-bit imm)
                logic [19:0] uimm20;
                uimm20 = {{14{c[12]}}, c[12], c[6:2]};
                instr_o = (uimm20 == 20'b0) ? 32'b0 :
                          {uimm20, rd, 7'b0110111};
              end
            end
            3'b100: begin
              case (c[11:10])
                2'b00: begin
                  // C.SRLI → SRLI rs1p, rs1p, shamt[4:0]
                  instr_o = {7'b0000000, c[6:2], rs1p, 3'b101, rs1p, 7'b0010011};
                end
                2'b01: begin
                  // C.SRAI → SRAI rs1p, rs1p, shamt[4:0]
                  instr_o = {7'b0100000, c[6:2], rs1p, 3'b101, rs1p, 7'b0010011};
                end
                2'b10: begin
                  // C.ANDI → ANDI rs1p, rs1p, imm
                  logic [5:0] imm;
                  imm = {c[12], c[6:2]};
                  instr_o = {{6{imm[5]}}, imm, rs1p, 3'b111, rs1p, 7'b0010011};
                end
                2'b11: begin
                  case ({c[12], c[6:5]})
                    3'b000: instr_o = {7'b0100000, rs2p, rs1p, 3'b000, rs1p, 7'b0110011}; // C.SUB
                    3'b001: instr_o = {7'b0000000, rs2p, rs1p, 3'b100, rs1p, 7'b0110011}; // C.XOR
                    3'b010: instr_o = {7'b0000000, rs2p, rs1p, 3'b110, rs1p, 7'b0110011}; // C.OR
                    3'b011: instr_o = {7'b0000000, rs2p, rs1p, 3'b111, rs1p, 7'b0110011}; // C.AND
                    default: instr_o = 32'b0; // reserved (c[12]=1 variants in RV32)
                  endcase
                end
              endcase
            end
            3'b101: begin
              // C.J → JAL x0, offset (LSB always 0)
              logic [20:1] joff;
              joff = {{9{c[12]}}, c[12], c[8], c[10:9], c[6], c[7], c[2], c[11], c[5:3]};
              instr_o = {joff[20], joff[10:1], joff[11], joff[19:12], 5'd0, 7'b1101111};
            end
            3'b110: begin
              // C.BEQZ → BEQ rs1p, x0, offset
              // CB offset[8:1] (LSB always 0)
              logic [8:1] boff;
              boff = {c[12], c[6:5], c[2], c[11:10], c[4:3]};
              // BEQ: {imm[12],imm[10:5],rs2,rs1,000,imm[4:1],imm[11],opcode}
              instr_o = {boff[8], {3{boff[8]}}, boff[7:5], 5'd0, rs1p, 3'b000,
                         boff[4:1], boff[8], 7'b1100011};
            end
            3'b111: begin
              // C.BNEZ → BNE rs1p, x0, offset
              logic [8:1] boff;
              boff = {c[12], c[6:5], c[2], c[11:10], c[4:3]};
              instr_o = {boff[8], {3{boff[8]}}, boff[7:5], 5'd0, rs1p, 3'b001,
                         boff[4:1], boff[8], 7'b1100011};
            end
            default: instr_o = 32'b0;
          endcase
        end

        // ----------------------------------------------------------------
        // Quadrant 2  (op = 2'b10)
        // ----------------------------------------------------------------
        2'b10: begin
          case (c[15:13])
            3'b000: begin
              // C.SLLI → SLLI rd, rd, shamt[4:0]
              instr_o = {7'b0000000, c[6:2], rd, 3'b001, rd, 7'b0010011};
            end
            3'b010: begin
              // C.LWSP → LW rd, offset(x2)
              // offset = {c[3:2], c[12], c[6:4]} << 2
              logic [7:0] off;
              off = {c[3:2], c[12], c[6:4], 2'b00};
              instr_o = (rd == 5'b0) ? 32'b0 :
                        {{4{1'b0}}, off, 5'd2, 3'b010, rd, 7'b0000011};
            end
            3'b100: begin
              if (c[12] == 1'b0) begin
                if (rs2 == 5'b0) begin
                  // C.JR → JALR x0, 0(rs1); rs1=x0 is illegal
                  instr_o = (rs1 == 5'b0) ? 32'b0 :
                            {12'b0, rs1, 3'b000, 5'd0, 7'b1100111};
                end else begin
                  // C.MV → ADD rd, x0, rs2
                  instr_o = {7'b0000000, rs2, 5'd0, 3'b000, rd, 7'b0110011};
                end
              end else begin
                if (rs2 == 5'b0) begin
                  if (rd == 5'b0) begin
                    // C.EBREAK
                    instr_o = 32'h0010_0073;
                  end else begin
                    // C.JALR → JALR x1, 0(rs1); rs1=x0 is illegal
                    instr_o = (rs1 == 5'b0) ? 32'b0 :
                              {12'b0, rs1, 3'b000, 5'd1, 7'b1100111};
                  end
                end else begin
                  // C.ADD → ADD rd, rd, rs2
                  instr_o = {7'b0000000, rs2, rd, 3'b000, rd, 7'b0110011};
                end
              end
            end
            3'b110: begin
              // C.SWSP → SW rs2, offset(x2)
              // offset = {c[8:7], c[12:9]} << 2  (8 bits, zero-extended)
              // SW: {imm[11:5], rs2, rs1, 010, imm[4:0], 0100011}
              logic [7:0] off;
              off = {c[8:7], c[12:9], 2'b00};
              instr_o = {4'b0, off[7:5], rs2, 5'd2, 3'b010, off[4:0], 7'b0100011};
            end
            default: instr_o = 32'b0;
          endcase
        end

        default: instr_o = instr_i; // should never reach here
      endcase
    end
  end
endmodule
