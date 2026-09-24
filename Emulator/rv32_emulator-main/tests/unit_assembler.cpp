// The assembler, end to end.
#include "asm/assembler.hpp"
#include "asm/diag_render.hpp"
#include "isa/disasm.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::as;

namespace {

AssembledProgram build(const std::string& text) {
    return assemble_text(text, "test.s");
}

/// Assemble and require success, reporting the first diagnostic if not.
AssembledProgram build_ok(const std::string& text) {
    AssembledProgram program = build(text);
    if (!program.ok()) {
        const SourceFile source("test.s", text);
        std::printf("%s", render_all(program.diagnostics, source).c_str());
    }
    return program;
}

std::string disasm(const AssembledProgram& program, std::size_t index) {
    if (index >= program.imem_words.size()) return "<out of range>";
    return isa::disassemble_word(program.imem_words[index], static_cast<Addr>(index * 4));
}

/// True if any diagnostic carries this code.
bool has_code(const AssembledProgram& program, const std::string& code) {
    for (const Diagnostic& diagnostic : program.diagnostics.items()) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

}  // namespace

RV_TEST(assembler, encodes_basic_instructions) {
    const AssembledProgram program = build_ok(
        "        addi x1, x0, 5\n"
        "        add  x3, x1, x2\n"
        "        sw   x3, 0(x4)\n"
        "        ebreak\n");
    RV_CHECK(program.ok());
    RV_CHECK_EQ(program.imem_words.size(), std::size_t{4});
    RV_CHECK_HEX(program.imem_words[0], 0x00500093u);
    RV_CHECK_HEX(program.imem_words[1], 0x002081b3u);
    RV_CHECK_HEX(program.imem_words[3], 0x00100073u);
}

RV_TEST(assembler, abi_and_numeric_register_names_are_interchangeable) {
    const AssembledProgram abi = build_ok("addi a0, sp, 4\n");
    const AssembledProgram numeric = build_ok("addi x10, x2, 4\n");
    RV_CHECK(abi.ok());
    RV_CHECK_HEX(abi.imem_words[0], numeric.imem_words[0]);
}

RV_TEST(assembler, labels_resolve_forwards_and_backwards) {
    const AssembledProgram program = build_ok(
        "start:\n"
        "        beq  a0, a1, forward\n"   // 0x00 -> 0x08
        "        nop\n"                    // 0x04
        "forward:\n"
        "        beq  a0, a1, start\n"     // 0x08 -> 0x00
        "        ebreak\n");
    RV_CHECK(program.ok());
    RV_CHECK_STR(disasm(program, 0), "beq a0, a1, 0x8");
    RV_CHECK_STR(disasm(program, 2), "beq a0, a1, 0x0");
}

RV_TEST(assembler, li_is_sized_from_its_literal) {
    // Small values fit in one addi; larger ones need lui + addi. The size is
    // settled in pass 1, which is why it must depend only on the literal.
    const AssembledProgram small = build_ok("li a0, 5\nebreak\n");
    RV_CHECK_EQ(small.imem_words.size(), std::size_t{2});
    RV_CHECK_STR(disasm(small, 0), "addi a0, zero, 5");

    const AssembledProgram large = build_ok("li a0, 0x12345\nebreak\n");
    RV_CHECK_EQ(large.imem_words.size(), std::size_t{3});
    RV_CHECK_STR(disasm(large, 0), "lui a0, 0x12");
    RV_CHECK_STR(disasm(large, 1), "addi a0, a0, 837");

    // Boundary: 2047 fits, 2048 does not.
    RV_CHECK_EQ(build_ok("li a0, 2047\n").imem_words.size(), std::size_t{1});
    RV_CHECK_EQ(build_ok("li a0, 2048\n").imem_words.size(), std::size_t{2});
    RV_CHECK_EQ(build_ok("li a0, -2048\n").imem_words.size(), std::size_t{1});
    RV_CHECK_EQ(build_ok("li a0, -2049\n").imem_words.size(), std::size_t{2});
}

RV_TEST(assembler, li_round_trips_awkward_values) {
    // The +0x800 bias in the hi/lo split exists for exactly these: any value
    // whose bit 11 is set comes out 0x1000 too small without it.
    const std::vector<i64> values = {0x7ff,   0x800,      0xfff,      0x1000,   0x12345,
                                     -1,      -0x800,     -0x801,     0x7fffffff,
                                     static_cast<i64>(static_cast<i32>(0x80000000))};
    for (const i64 value : values) {
        const AssembledProgram program = build_ok("li a0, " + std::to_string(value) + "\nebreak\n");
        RV_CHECK(program.ok());

        // Execute it and check a0 really holds the value.
        core::Hart hart;
        hart.imem().load_words(program.imem_words);
        hart.reset(0);
        while (!hart.halted()) hart.step();
        RV_CHECK_HEX(hart.cpu().x[10], static_cast<u32>(value));
    }
}

RV_TEST(assembler, pseudo_instructions_expand_as_documented) {
    const AssembledProgram program = build_ok(
        "        nop\n"
        "        mv   a0, a1\n"
        "        not  a0, a1\n"
        "        neg  a0, a1\n"
        "        seqz a0, a1\n"
        "        snez a0, a1\n"
        "        ret\n");
    RV_CHECK(program.ok());
    RV_CHECK_STR(disasm(program, 0), "addi zero, zero, 0");
    RV_CHECK_STR(disasm(program, 1), "addi a0, a1, 0");
    RV_CHECK_STR(disasm(program, 2), "xori a0, a1, -1");
    RV_CHECK_STR(disasm(program, 3), "sub a0, zero, a1");
    RV_CHECK_STR(disasm(program, 4), "sltiu a0, a1, 1");
    RV_CHECK_STR(disasm(program, 5), "sltu a0, zero, a1");
    RV_CHECK_STR(disasm(program, 6), "jalr zero, 0(ra)");
}

RV_TEST(assembler, zero_branches_swap_operands_where_needed) {
    const AssembledProgram program = build_ok(
        "here:\n"
        "        beqz a0, here\n"
        "        bnez a0, here\n"
        "        blez a0, here\n"   // bge zero, a0
        "        bgtz a0, here\n"   // blt zero, a0
        "        bgt  a0, a1, here\n"  // blt a1, a0
        "        ble  a0, a1, here\n"  // bge a1, a0
        );
    RV_CHECK(program.ok());
    RV_CHECK_STR(disasm(program, 0), "beq a0, zero, 0x0");
    RV_CHECK_STR(disasm(program, 1), "bne a0, zero, 0x0");
    RV_CHECK_STR(disasm(program, 2), "bge zero, a0, 0x0");
    RV_CHECK_STR(disasm(program, 3), "blt zero, a0, 0x0");
    RV_CHECK_STR(disasm(program, 4), "blt a1, a0, 0x0");
    RV_CHECK_STR(disasm(program, 5), "bge a1, a0, 0x0");
}

RV_TEST(assembler, jal_has_a_one_operand_and_a_two_operand_form) {
    // `jal label` links into ra; `jal rd, label` is the real instruction.
    const AssembledProgram program = build_ok(
        "target:\n"
        "        jal  target\n"
        "        jal  a0, target\n"
        "        j    target\n");
    RV_CHECK(program.ok());
    RV_CHECK_STR(disasm(program, 0), "jal ra, 0x0");
    RV_CHECK_STR(disasm(program, 1), "jal a0, 0x0");
    RV_CHECK_STR(disasm(program, 2), "jal zero, 0x0");
}

RV_TEST(assembler, call_is_pc_relative_and_two_words) {
    const AssembledProgram program = build_ok(
        "_start:\n"
        "        call target\n"
        "        ebreak\n"
        "target:\n"
        "        ret\n");
    RV_CHECK(program.ok());
    RV_CHECK_EQ(program.imem_words.size(), std::size_t{4});
    // auipc computes from its own address, so the pair lands on target.
    RV_CHECK_STR(disasm(program, 0), "auipc ra, 0x0");
    RV_CHECK_STR(disasm(program, 1), "jalr ra, 12(ra)");
}

RV_TEST(assembler, csr_shorthands_and_names) {
    const AssembledProgram program = build_ok(
        "        csrr  t0, mstatus\n"
        "        csrw  mstatus, t0\n"
        "        csrrs t1, mtvec, zero\n"
        "        csrwi mscratch, 7\n");
    RV_CHECK(program.ok());
    RV_CHECK_STR(disasm(program, 0), "csrrs t0, mstatus, zero");
    RV_CHECK_STR(disasm(program, 1), "csrrw zero, mstatus, t0");
    RV_CHECK_STR(disasm(program, 2), "csrrs t1, mtvec, zero");
    RV_CHECK_STR(disasm(program, 3), "csrrwi zero, mscratch, 7");
}

RV_TEST(assembler, expressions_fold_with_correct_precedence) {
    const AssembledProgram program = build_ok(
        "        .equ BASE, 0x100\n"
        "        li a0, BASE + 2 * 4\n"      // 0x108: '*' binds tighter than '+'
        "        li a1, (BASE + 2) * 4\n"    // 0x408: parentheses override that
        "        li a2, 1 << 8 | 3\n"        // 0x103: shift binds tighter than '|'
        "        li a3, ~0 & 0xff\n"         // 0xff
        "        ebreak\n");
    RV_CHECK(program.ok());
    // All four values fit in a 12-bit immediate, so each li is a single addi.
    RV_CHECK_STR(disasm(program, 0), "addi a0, zero, 264");
    RV_CHECK_STR(disasm(program, 1), "addi a1, zero, 1032");
    RV_CHECK_STR(disasm(program, 2), "addi a2, zero, 259");
    RV_CHECK_STR(disasm(program, 3), "addi a3, zero, 255");
}

RV_TEST(assembler, data_directives_emit_a_little_endian_image) {
    const AssembledProgram program = build_ok(
        "        .data\n"
        "        .word 0x12345678\n"
        "        .half 0xabcd\n"
        "        .byte 1, 2\n"
        "        .asciz \"hi\"\n");
    RV_CHECK(program.ok());
    const std::vector<u8> expected = {0x78, 0x56, 0x34, 0x12, 0xcd, 0xab,
                                      0x01, 0x02, 'h',  'i',  0x00};
    RV_CHECK_EQ(program.dmem_bytes.size(), expected.size());
    for (std::size_t i = 0; i < std::min(program.dmem_bytes.size(), expected.size()); ++i) {
        RV_CHECK_HEX(program.dmem_bytes[i], expected[i]);
    }
}

RV_TEST(assembler, align_and_space) {
    const AssembledProgram program = build_ok(
        "        .data\n"
        "        .byte 1\n"
        "        .align 2\n"
        "        .word 0xffffffff\n"
        "        .space 3\n"
        "        .byte 9\n");
    RV_CHECK(program.ok());
    RV_CHECK_EQ(program.dmem_bytes.size(), std::size_t{12});
    RV_CHECK_HEX(program.dmem_bytes[0], 1u);
    RV_CHECK_HEX(program.dmem_bytes[1], 0u);  // alignment padding
    RV_CHECK_HEX(program.dmem_bytes[4], 0xffu);
    RV_CHECK_HEX(program.dmem_bytes[11], 9u);
}

RV_TEST(assembler, text_and_data_are_separate_cursors) {
    // Both spaces start at zero and advance independently. `msg` and `_start`
    // can therefore have the same numeric value and be different locations.
    const AssembledProgram program = build_ok(
        "        .data\n"
        "msg:    .asciz \"x\"\n"
        "        .text\n"
        "_start: ebreak\n");
    RV_CHECK(program.ok());
    const Symbol* msg = program.symbols.find("msg");
    const Symbol* start = program.symbols.find("_start");
    RV_CHECK(msg != nullptr);
    RV_CHECK(start != nullptr);
    RV_CHECK_HEX(msg->value, 0u);
    RV_CHECK_HEX(start->value, 0u);
    RV_CHECK_EQ(static_cast<int>(msg->space), static_cast<int>(Space::Data));
    RV_CHECK_EQ(static_cast<int>(start->space), static_cast<int>(Space::Text));
}

RV_TEST(assembler, la_loads_an_absolute_address_not_a_pc_relative_one) {
    // auipc would compute an address in instruction memory, which is the wrong
    // space entirely. See docs/memory-map.md.
    const AssembledProgram program = build_ok(
        "        .data\n"
        "        .space 0x2000\n"
        "msg:    .asciz \"x\"\n"
        "        .text\n"
        "_start: la a0, msg\n");
    RV_CHECK(program.ok());
    RV_CHECK_STR(disasm(program, 0), "lui a0, 0x2");
    RV_CHECK_STR(disasm(program, 1), "addi a0, a0, 0");
}

RV_TEST(assembler, entry_point_comes_from_start) {
    const AssembledProgram program = build_ok(
        "        nop\n"
        "        nop\n"
        "_start: ebreak\n");
    RV_CHECK_HEX(program.entry, 8u);
}

// ---- diagnostics -----------------------------------------------------------

RV_TEST(assembler, jumping_to_a_data_symbol_is_an_error) {
    // The single most valuable diagnostic in this assembler: without it the
    // program assembles cleanly and jumps into nowhere.
    const AssembledProgram program = build(
        "        .data\n"
        "msg:    .asciz \"x\"\n"
        "        .text\n"
        "        call msg\n");
    RV_CHECK(!program.ok());
    RV_CHECK(has_code(program, "E0310"));

    // And it points at both the use and the definition.
    const Diagnostic* first = program.diagnostics.first_error();
    RV_CHECK(first != nullptr);
    RV_CHECK_EQ(first->labels.size(), std::size_t{1});
    RV_CHECK_EQ(first->labels[0].span.line, 2u);
}

RV_TEST(assembler, branching_to_a_data_symbol_is_an_error) {
    const AssembledProgram program = build(
        "        .data\n"
        "buf:    .space 4\n"
        "        .text\n"
        "        beq a0, a1, buf\n");
    RV_CHECK(has_code(program, "E0310"));
}

RV_TEST(assembler, out_of_range_immediates_report_the_legal_range) {
    const AssembledProgram program = build("addi a0, a1, 5000\n");
    RV_CHECK(has_code(program, "E0102"));
    const Diagnostic* first = program.diagnostics.first_error();
    RV_CHECK_NE(first->hint.find("-2048..2047"), std::string::npos);
    RV_CHECK_NE(first->hint.find("li t0, 5000"), std::string::npos);
}

RV_TEST(assembler, a_branch_too_far_away_is_reported) {
    std::string text = "here:\n";
    for (int i = 0; i < 1200; ++i) text += "        nop\n";  // ~4800 bytes
    text += "        beq a0, a1, here\n";
    const AssembledProgram program = build(text);
    RV_CHECK(!program.ok());
    RV_CHECK(has_code(program, "E0102"));
}

RV_TEST(assembler, unknown_mnemonics_suggest_a_replacement) {
    const AssembledProgram program = build("addii a0, a1, 5\n");
    RV_CHECK(has_code(program, "E0020"));
    RV_CHECK_NE(program.diagnostics.first_error()->hint.find("addi"), std::string::npos);
}

RV_TEST(assembler, undefined_labels_suggest_a_defined_one) {
    const AssembledProgram program = build(
        "loop:   nop\n"
        "        beq a0, a1, lop\n");
    RV_CHECK(has_code(program, "E0201"));
    RV_CHECK_NE(program.diagnostics.first_error()->hint.find("loop"), std::string::npos);
}

RV_TEST(assembler, an_out_of_range_register_number_says_so) {
    const AssembledProgram program = build("li a0, x33\n");
    RV_CHECK(has_code(program, "E0203"));
    RV_CHECK_NE(program.diagnostics.first_error()->primary_label.find("x0 to x31"),
                std::string::npos);
}

RV_TEST(assembler, duplicate_definitions_point_at_both) {
    const AssembledProgram program = build(
        "loop:   nop\n"
        "loop:   nop\n");
    RV_CHECK(has_code(program, "E0202"));
    const Diagnostic* first = program.diagnostics.first_error();
    RV_CHECK_EQ(first->primary.line, 2u);
    RV_CHECK_EQ(first->labels.size(), std::size_t{1});
    RV_CHECK_EQ(first->labels[0].span.line, 1u);
}

RV_TEST(assembler, every_mistake_in_a_file_is_reported) {
    // One typo must not stop the assembler from finding the rest: a beginner
    // fixing errors one build at a time is a miserable loop.
    const AssembledProgram program = build(
        "        .data\n"
        "msg:    .asciz \"x\"\n"
        "        .text\n"
        "        addii a0, a1, 5\n"      // unknown mnemonic
        "        addi  a0, a1, 5000\n"   // out of range
        "        beq   a0, a1, lop\n"    // undefined symbol
        "        call  msg\n"            // wrong address space
        "loop:   ebreak\n");
    RV_CHECK_EQ(program.diagnostics.error_count(), std::size_t{4});
}

RV_TEST(assembler, a_failed_line_still_reserves_its_word) {
    // Otherwise one typo shifts every later address and turns a single error
    // into a cascade of bogus range errors.
    const AssembledProgram good = build_ok(
        "        nop\n"
        "        nop\n"
        "here:   ebreak\n");
    const AssembledProgram bad = build(
        "        addii a0, a1, 5\n"
        "        nop\n"
        "here:   ebreak\n");
    RV_CHECK_EQ(bad.symbols.find("here")->value, good.symbols.find("here")->value);
}

RV_TEST(assembler, mixing_address_spaces_in_arithmetic_is_reported) {
    const AssembledProgram program = build(
        "        .data\n"
        "d:      .word 0\n"
        "        .text\n"
        "t:      li a0, t - d\n");
    RV_CHECK(has_code(program, "E0301"));
}

RV_TEST(assembler, a_difference_within_one_space_is_a_plain_number) {
    const AssembledProgram program = build_ok(
        "        .data\n"
        "a:      .word 0\n"
        "b:      .word 0\n"
        "        .text\n"
        "        li a0, b - a\n"
        "        ebreak\n");
    RV_CHECK(program.ok());
    RV_CHECK_STR(disasm(program, 0), "addi a0, zero, 4");
}

RV_TEST(assembler, li_of_an_address_suggests_la) {
    const AssembledProgram program = build(
        "        .data\n"
        "msg:    .word 0\n"
        "        .text\n"
        "        li a0, msg\n");
    RV_CHECK(has_code(program, "E0311"));
    RV_CHECK_NE(program.diagnostics.first_error()->hint.find("'la'"), std::string::npos);
}

RV_TEST(assembler, data_in_text_warns_that_it_cannot_be_loaded) {
    const AssembledProgram program = build(
        "        .text\n"
        "        .word 42\n"
        "        ebreak\n");
    RV_CHECK(program.ok());  // legal, just probably not what was meant
    RV_CHECK(has_code(program, "W0502"));
}

RV_TEST(assembler, strict_word_mem_flags_sub_word_access) {
    AssembleOptions options;
    options.strict_word_mem = true;
    const AssembledProgram program =
        assemble_text("sb a0, 0(a1)\nlbu a2, 0(a1)\nsw a0, 0(a1)\n", "test.s", options);
    RV_CHECK(program.ok());
    RV_CHECK_EQ(program.diagnostics.warning_count(), std::size_t{2});  // sw is fine
}

RV_TEST(assembler, a_program_larger_than_imem_is_rejected) {
    AssembleOptions options;
    options.imem_size = 64;
    std::string text;
    for (int i = 0; i < 32; ++i) text += "nop\n";
    const AssembledProgram program = assemble_text(text, "test.s", options);
    RV_CHECK(has_code(program, "E0601"));
}

// ---- source map ------------------------------------------------------------

RV_TEST(assembler, source_map_records_one_entry_per_emitted_word) {
    const AssembledProgram program = build_ok(
        "        li a0, 5\n"        // line 1: one word
        "        li a1, 0x12345\n"  // line 2: two words
        "        ebreak\n");        // line 3: one word
    RV_CHECK(program.ok());

    RV_CHECK_EQ(program.map.addrs_for_line(1).size(), std::size_t{1});
    RV_CHECK_EQ(program.map.addrs_for_line(2).size(), std::size_t{2});
    RV_CHECK_EQ(program.map.addrs_for_line(3).size(), std::size_t{1});

    // slot/count is what lets the debugger show [1/2] and [2/2] while keeping
    // the execution arrow on line 2.
    const auto first = program.map.at(4);
    const auto second = program.map.at(8);
    RV_CHECK(first.has_value());
    RV_CHECK_EQ(first->line, 2u);
    RV_CHECK_EQ(first->slot, 0);
    RV_CHECK_EQ(first->count, 2);
    RV_CHECK_EQ(second->line, 2u);
    RV_CHECK_EQ(second->slot, 1);

    // Stepping by line must land only on slot 0.
    RV_CHECK(program.map.is_line_start(4));
    RV_CHECK(!program.map.is_line_start(8));
}

RV_TEST(assembler, source_map_skips_blank_lines_and_comments) {
    const AssembledProgram program = build_ok(
        "# a comment\n"
        "\n"
        "        nop\n"
        "\n"
        "        ebreak\n");
    RV_CHECK(program.map.addrs_for_line(1).empty());
    RV_CHECK_EQ(*program.map.next_line_with_code(1), 3u);
    RV_CHECK_EQ(*program.map.next_line_with_code(4), 5u);
    RV_CHECK_EQ(*program.map.first_addr_at_or_after(2), 0u);
}

RV_TEST(assembler, source_map_names_the_pseudo_a_word_came_from) {
    const AssembledProgram program = build_ok("li a0, 0x12345\n");
    const auto entry = program.map.at(0);
    RV_CHECK(entry.has_value());
    RV_CHECK_EQ(static_cast<int>(entry->pseudo), static_cast<int>(PseudoId::LI));
}

// ---- the C extension -------------------------------------------------------

RV_TEST(assembler, compressed_instructions_take_two_bytes) {
    // Four compressed instructions pack into two words. If sizing still
    // reserved four bytes each, this would be four words and every label after
    // them would be wrong.
    const AssembledProgram program = build_ok(
        "        c.li a0, 1\n"
        "        c.li a1, 2\n"
        "        c.add a0, a1\n"
        "        c.mv a2, a0\n");
    RV_CHECK_EQ(program.imem_words.size(), std::size_t{2});
    RV_CHECK_HEX(program.imem_words[0], 0x4589'4505u);
    RV_CHECK_HEX(program.imem_words[1], 0x862a'952eu);
}

RV_TEST(assembler, a_32_bit_instruction_may_straddle_a_word_boundary) {
    // One compressed instruction leaves the cursor at 2 mod 4, so the addi
    // that follows spans two words. That is legal now, and the assembler must
    // neither pad nor complain.
    const AssembledProgram program = build_ok(
        "        c.li a0, 1\n"
        "        addi a1, zero, 0x123\n"
        "        c.li a2, 3\n");
    // 2 + 4 + 2 = 8 bytes.
    RV_CHECK_EQ(program.imem_words.size(), std::size_t{2});
    // addi a1, zero, 0x123 encodes as 0x12300593: its low half is in the first
    // word and its high half in the second.
    RV_CHECK_HEX(program.imem_words[0] & 0xffffu, 0x4505u);
    RV_CHECK_HEX(program.imem_words[0] >> 16, 0x0593u);
    RV_CHECK_HEX(program.imem_words[1] & 0xffffu, 0x1230u);
}

RV_TEST(assembler, labels_after_compressed_code_land_where_they_should) {
    // The two passes have to agree about how many bytes each line takes. If
    // they did not, this branch offset would be silently wrong rather than
    // being reported -- which is why the sizing is a property of the mnemonic.
    const AssembledProgram program = build_ok(
        "        c.li a0, 1\n"
        "        c.li a1, 2\n"
        "        beq  a0, a1, target\n"
        "        c.li a2, 3\n"
        "target: ebreak\n");
    const Symbol* target = program.symbols.find("target");
    RV_CHECK(target != nullptr);
    // 2 + 2 + 4 + 2 = 10.
    RV_CHECK_HEX(target->value, 0x0au);
}

RV_TEST(assembler, a_compressed_form_rejects_a_register_it_cannot_reach) {
    const AssembledProgram program = build("        c.lw t0, 8(a1)\n");
    RV_CHECK(!program.ok());
    RV_CHECK(has_code(program, "E0107"));
}

RV_TEST(assembler, a_compressed_form_rejects_an_out_of_range_immediate) {
    RV_CHECK(has_code(build("        c.addi a0, 99\n"), "E0102"));
    RV_CHECK(has_code(build("        c.lw a0, 6(a1)\n"), "E0102"));       // not a multiple of 4
    RV_CHECK(has_code(build("        c.addi4spn a0, 0\n"), "E0102"));     // reserved
    RV_CHECK(has_code(build("        c.addi16sp 8\n"), "E0102"));         // not a multiple of 16
}

RV_TEST(assembler, the_sp_forms_insist_on_sp) {
    RV_CHECK(has_code(build("        c.swsp a0, 4(a1)\n"), "E0109"));
    RV_CHECK(has_code(build("        c.lwsp a0, 4(a1)\n"), "E0109"));
    RV_CHECK(build_ok("        c.swsp a0, 4(sp)\n").ok());
}

RV_TEST(assembler, the_forms_that_exclude_x0_say_so) {
    RV_CHECK(has_code(build("        c.jr zero\n"), "E0108"));
    RV_CHECK(has_code(build("        c.mv zero, a0\n"), "E0108"));
}

RV_TEST(assembler, compressed_code_round_trips_through_the_disassembler) {
    // What the assembler emits, the disassembler must name the same way -- and
    // it must name it as the compressed form, not as what it expands to.
    const AssembledProgram program = build_ok(
        "        c.li a0, 5\n"
        "        c.addi a0, -1\n"
        "        c.jr ra\n");
    RV_CHECK_STR(isa::disassemble_word(program.imem_words[0] & 0xffffu, 0), "c.li a0, 5");
    RV_CHECK_STR(isa::disassemble_word(program.imem_words[0] >> 16, 2), "c.addi a0, -1");
    RV_CHECK_STR(isa::disassemble_word(program.imem_words[1] & 0xffffu, 4), "c.jr ra");
}
