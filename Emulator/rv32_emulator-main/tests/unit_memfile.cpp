// The $readmemh export format.
#include "core/memfile.hpp"

#include <algorithm>

#include "core/device_config.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::core;

RV_TEST(memfile, renders_one_lowercase_word_per_line) {
    const std::vector<Word> words = {0x00500093u, 0x00300113u, 0x002081b3u, 0xdeadbeefu};
    RV_CHECK_STR(render_mem_file(words),
                 "00500093\n"
                 "00300113\n"
                 "002081b3\n"
                 "deadbeef\n");
}

RV_TEST(memfile, pads_to_the_full_array_on_request) {
    MemFileOptions options;
    options.pad_to_full = true;
    options.full_size = 4;
    RV_CHECK_STR(render_mem_file({0x1u}, options),
                 "00000001\n"
                 "00000000\n"
                 "00000000\n"
                 "00000000\n");
}

RV_TEST(memfile, round_trips_through_the_parser) {
    const std::vector<Word> original = {0x00500093u, 0x00000000u, 0xffffffffu, 0x0000000fu};
    std::vector<Word> parsed;
    const MemFileResult result = parse_mem_file(render_mem_file(original), "test", parsed);
    RV_CHECK(result.ok);
    RV_CHECK_EQ(parsed.size(), original.size());
    for (std::size_t i = 0; i < original.size(); ++i) RV_CHECK_HEX(parsed[i], original[i]);
}

RV_TEST(memfile, parser_tolerates_comments_blank_lines_and_addresses) {
    const std::string text =
        "// a comment\n"
        "\n"
        "00000001   // trailing comment\n"
        "# hash comment\n"
        "@00000004\n"
        "000000ff\n";
    std::vector<Word> parsed;
    const MemFileResult result = parse_mem_file(text, "test", parsed);
    RV_CHECK(result.ok);
    RV_CHECK_EQ(parsed.size(), std::size_t{5});
    RV_CHECK_HEX(parsed[0], 0x1u);
    RV_CHECK_HEX(parsed[1], 0u);  // gap filled with zeros
    RV_CHECK_HEX(parsed[4], 0xffu);
}

RV_TEST(memfile, parser_reports_bad_input_with_a_line_number) {
    std::vector<Word> parsed;
    const MemFileResult result = parse_mem_file("00000001\nnot_hex\n", "prog.mem", parsed);
    RV_CHECK(!result.ok);
    RV_CHECK_NE(result.error.find("prog.mem:2"), std::string::npos);
}

RV_TEST(memfile, rejects_words_wider_than_32_bits) {
    std::vector<Word> parsed;
    RV_CHECK(!parse_mem_file("1000000000\n", "test", parsed).ok);
}

RV_TEST(memfile, packs_bytes_little_endian) {
    // .word 0x12345678 in a byte image appears as the single line 12345678.
    const std::vector<u8> bytes = {0x78, 0x56, 0x34, 0x12};
    const std::vector<Word> words = pack_bytes_to_words(bytes);
    RV_CHECK_EQ(words.size(), std::size_t{1});
    RV_CHECK_HEX(words[0], 0x12345678u);
}

RV_TEST(memfile, pads_a_partial_trailing_word) {
    const std::vector<u8> bytes = {0xaa, 0xbb, 0xcc};
    const std::vector<Word> words = pack_bytes_to_words(bytes);
    RV_CHECK_EQ(words.size(), std::size_t{1});
    RV_CHECK_HEX(words[0], 0x00ccbbaau);
}

RV_TEST(memfile, annotated_output_carries_the_disassembly) {
    MemFileOptions options;
    options.annotate = true;
    const std::string text = render_mem_file({0x00500093u}, options);
    RV_CHECK_NE(text.find("addi"), std::string::npos);
    RV_CHECK_NE(text.find("00500093"), std::string::npos);
}

RV_TEST(memfile, a_padded_image_is_exactly_as_deep_as_the_memory) {
    // What this is for: the file is loaded into an array of a fixed depth by
    // $readmemh. A file shorter than the array leaves its tail holding
    // whatever the tool put there, and that reads as the program going wrong
    // rather than as the file being short.
    const std::vector<Word> program = {0x00a00413, 0x00000293};

    core::MemFileOptions options;
    options.pad_to_full = true;
    options.full_size = 8192 / 4;  // an 8 KB machine

    const std::string text = core::render_mem_file(program, options);
    RV_CHECK_EQ(std::count(text.begin(), text.end(), '\n'), 2048);

    // The program is at the front and the rest is zero, which is what a memory
    // that has not been written holds.
    RV_CHECK_EQ(text.compare(0, 8, "00a00413"), 0);
    RV_CHECK_NE(text.rfind("00000000\n"), std::string::npos);
}

RV_TEST(memfile, the_depth_is_the_word_count_the_size_list_advertises) {
    // The label in the panel says "16 KB - 4096 x 32-bit"; that 4096 is the
    // depth to configure a block memory with, and it has to be the number of
    // lines this writes.
    for (const u32 bytes : core::kCommonMemorySizes) {
        core::MemFileOptions options;
        options.pad_to_full = true;
        options.full_size = bytes / 4;
        const std::string text = core::render_mem_file({}, options);
        RV_CHECK_EQ(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')),
                    std::size_t{bytes / 4});
    }
}
