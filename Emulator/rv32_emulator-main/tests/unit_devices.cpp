// Configurable peripherals.
//
// The design claim under test: because every device implements a
// side-effect-free peek/poke pair, the debugger's existing one-word memory
// delta already undoes a write to a device, and the memory view can edit a
// device register like any other address. If that holds, adding a peripheral
// costs nothing anywhere else.
#include "core/device_config.hpp"
#include "dbg/history.hpp"
#include "machine_fixture.hpp"

using namespace fixture;
using namespace rv::core;
using rv::isa::InstrId;

namespace {

/// `sw value, offset(base)` where base is the MMIO window.
std::vector<rv::Word> store_program(rv::u32 offset, rv::u32 value) {
    return {
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        U(InstrId::LUI, 2, static_cast<rv::i32>(value & 0xffff'f000u)),
        I(InstrId::ADDI, 2, 2, static_cast<rv::i32>(rv::sign_extend(value & 0xfffu, 12))),
        S(InstrId::SW, 1, 2, static_cast<rv::i32>(offset)),
        EBREAK(),
    };
}

}  // namespace

// ---- the catalogue --------------------------------------------------------

RV_TEST(devices, the_catalogue_can_build_every_type_it_lists) {
    RV_CHECK(!device_catalogue().empty());
    for (const DeviceType& type : device_catalogue()) {
        std::unique_ptr<Device> device = make_device(type.name, 64);
        RV_CHECK(device != nullptr);
        if (device == nullptr) continue;
        // A device must name itself the way the catalogue does, or a saved
        // configuration would not round-trip.
        const bool ram_alias = type.name == "ram" || type.name == "rom";
        RV_CHECK(ram_alias || device->type_name() == type.name);
        RV_CHECK(!device->description().empty());
    }
}

RV_TEST(devices, an_unknown_type_is_refused_rather_than_guessed) {
    RV_CHECK(make_device("frobnicator") == nullptr);
    RV_CHECK(find_device_type("frobnicator") == nullptr);
}

// ---- attaching and detaching ----------------------------------------------

RV_TEST(devices, the_default_machine_has_the_documented_layout) {
    Machine m;
    const Bus& bus = m.hart().bus();
    RV_CHECK(bus.uart() != nullptr);
    RV_CHECK(bus.leds() != nullptr);
    RV_CHECK(bus.timer() != nullptr);
    RV_CHECK_EQ(bus.leds()->base_address(), 0xffff'0010u);
    RV_CHECK_EQ(bus.timer()->base_address(), 0xffff'0040u);
}

RV_TEST(devices, a_later_device_wins_the_slots_they_share) {
    // Refusing an overlap would be the tidier rule, but a machine being built
    // by hand spends most of its life half-built, and stopping the work to
    // complain is worse than showing what the overlap did.
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();

    Device* uart = bus.attach(1, std::make_unique<UartDevice>());
    RV_CHECK(uart != nullptr);
    Device* leds = bus.attach(1, std::make_unique<LedDevice>());
    RV_CHECK(leds != nullptr);

    // The later one answers at the address; the earlier one is still attached.
    RV_CHECK(bus.device_at_slot(1) == leds);
    RV_CHECK_EQ(bus.devices().size(), std::size_t{2});
    RV_CHECK_EQ(bus.reachable_slots(uart), std::size_t{0});
    RV_CHECK_EQ(bus.reachable_slots(leds), std::size_t{1});

    // And removing the one on top reveals what it was covering.
    RV_CHECK(bus.detach_device(leds));
    RV_CHECK(bus.device_at_slot(1) == uart);
    RV_CHECK_EQ(bus.reachable_slots(uart), std::size_t{1});
}

RV_TEST(devices, an_overlap_is_reported_before_it_is_made) {
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();
    bus.attach(0, std::make_unique<RamDevice>(64));  // four slots

    // What the interface asks so it can warn rather than refuse.
    RV_CHECK_EQ(bus.devices_overlapping(2, 1).size(), std::size_t{1});
    RV_CHECK(bus.devices_overlapping(4, 1).empty());
}

RV_TEST(devices, a_memory_block_takes_as_many_slots_as_it_needs) {
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();

    // 64 bytes at 16 bytes per slot is four slots.
    Device* ram = bus.attach(0, std::make_unique<RamDevice>(64));
    RV_CHECK(ram != nullptr);
    RV_CHECK_EQ(ram->slot_count(), std::size_t{4});
    RV_CHECK(bus.device_at_slot(3) == ram);
    RV_CHECK(bus.device_at_slot(4) == nullptr);

    // A device landing inside it takes over the slot it lands on, and only
    // that one -- the rest of the block still answers.
    Device* leds = bus.attach(2, std::make_unique<LedDevice>());
    RV_CHECK(bus.device_at_slot(2) == leds);
    RV_CHECK(bus.device_at_slot(1) == ram);
    RV_CHECK_EQ(bus.reachable_slots(ram), std::size_t{3});
}

RV_TEST(devices, an_address_with_nothing_attached_faults) {
    // Reading as zero would let a program touch a peripheral that is not there
    // and never find out.
    Machine m;
    m.hart().bus().detach_all();
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::LW, 2, 1, 0),
        EBREAK(),
    });
    m.step();
    const StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::LoadAccessFault));
}

// ---- memory blocks ---------------------------------------------------------

RV_TEST(devices, a_ram_block_stores_and_loads_like_memory) {
    Machine m;
    Bus& bus = m.hart().bus();
    bus.attach(8, std::make_unique<RamDevice>(64));

    m.load(store_program(0x80, 0x1234'5000));  // slot 8 is at +0x80
    m.run();
    RV_CHECK_HEX(bus.peek_word(0xffff'0080u), 0x1234'5000u);
}

RV_TEST(devices, a_rom_ignores_stores_but_can_still_be_edited_by_hand) {
    Machine m;
    Bus& bus = m.hart().bus();
    auto* rom = static_cast<RamDevice*>(bus.attach(8, std::make_unique<RamDevice>(64, true)));
    RV_CHECK(rom != nullptr);

    m.load(store_program(0x80, 0x1234'5000));
    m.run();
    RV_CHECK_HEX(bus.peek_word(0xffff'0080u), 0u);  // the program's write did nothing

    // But the debugger's path is not the program's path: being unable to edit a
    // ROM from a memory view would be an obstacle, not a safeguard.
    bus.poke_word(0xffff'0080u, 0xdead'beefu);
    RV_CHECK_HEX(bus.peek_word(0xffff'0080u), 0xdead'beefu);
}

RV_TEST(devices, a_preloaded_block_survives_a_reset) {
    // The point of "give me some ready-made memory": a lookup table has to
    // still be there after the machine restarts.
    Machine m;
    auto* rom = static_cast<RamDevice*>(
        m.hart().bus().attach(8, std::make_unique<RamDevice>(32, true)));
    rom->load_words({0x1111'1111u, 0x2222'2222u});

    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x1111'1111u);
    m.hart().reset(0);
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x1111'1111u);
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0084u), 0x2222'2222u);
}

RV_TEST(devices, a_program_can_read_a_preloaded_table) {
    Machine m;
    auto* rom = static_cast<RamDevice*>(
        m.hart().bus().attach(8, std::make_unique<RamDevice>(32, true)));
    rom->load_words({0x0000'00aau, 0x0000'00bbu});

    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::LW, 2, 1, 0x80),
        I(InstrId::LW, 3, 1, 0x84),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0xaau);
    RV_CHECK_HEX(m.reg(3), 0xbbu);
}

// ---- input devices ---------------------------------------------------------

RV_TEST(devices, a_value_device_hands_the_program_what_the_user_typed) {
    Machine m;
    auto* value = static_cast<ValueDevice*>(
        m.hart().bus().attach(8, std::make_unique<ValueDevice>()));
    value->set_input(0, 1234);

    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::LW, 2, 1, 0x80),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 1234u);
}

RV_TEST(devices, a_button_raises_the_external_interrupt_line) {
    Machine m;
    auto* button = static_cast<ButtonDevice*>(
        m.hart().bus().attach(8, std::make_unique<ButtonDevice>()));

    RV_CHECK_HEX(m.hart().bus().pending_interrupts(0), 0u);
    button->set_input(0, 1);  // press
    RV_CHECK_HEX(m.hart().bus().pending_interrupts(0), kIrqExternal);

    // And it can be told not to, for a button that is only polled.
    button->set_input(1, 0);
    RV_CHECK_HEX(m.hart().bus().pending_interrupts(0), 0u);
}

RV_TEST(devices, a_device_describes_its_own_controls) {
    // This is what lets the interface render a device it has never heard of.
    SwitchDevice switches;
    const std::vector<InputControl> controls = switches.inputs();
    RV_CHECK_EQ(controls.size(), std::size_t{1});
    RV_CHECK_EQ(static_cast<int>(controls[0].kind), static_cast<int>(InputKind::Toggle));
    RV_CHECK_EQ(controls[0].bit_count, 32);

    ButtonDevice button;
    RV_CHECK_EQ(button.inputs().size(), std::size_t{2});
    RV_CHECK_EQ(static_cast<int>(button.inputs()[0].kind), static_cast<int>(InputKind::Button));

    LedDevice leds;
    RV_CHECK(leds.inputs().empty());  // output only
}

RV_TEST(devices, a_display_renders_its_digits) {
    DisplayDevice display(4);
    RV_CHECK_STR(display.rendered(), "----");  // blank at reset
    display.poke(0, 0x01020304);
    RV_CHECK_STR(display.rendered(), "1234");
}

// ---- the debugger's view ---------------------------------------------------

RV_TEST(devices, the_memory_view_can_read_and_edit_a_device_register) {
    // peek/poke is what makes a peripheral appear in the memory view rather
    // than as a hole, and what makes it editable there.
    Machine m;
    Bus& bus = m.hart().bus();

    bus.poke_word(0xffff'0010u, 0x0000'00ffu);  // the LED register
    RV_CHECK_HEX(bus.leds()->value(), 0xffu);
    RV_CHECK_HEX(bus.peek_word(0xffff'0010u), 0xffu);
}

RV_TEST(devices, peeking_a_device_has_no_side_effects) {
    // A memory view redraws constantly. If peeking consumed anything, merely
    // looking at the machine would change it.
    Machine m;
    const Bus& bus = m.hart().bus();
    const std::string before = bus.uart_output();
    for (int i = 0; i < 100; ++i) {
        (void)bus.peek_word(0xffff'0000u);
        (void)bus.peek_word(0xffff'0004u);
    }
    RV_CHECK_STR(bus.uart_output(), before);
}

RV_TEST(devices, reverse_stepping_undoes_a_write_to_a_device) {
    // The claim the whole design rests on: no per-device history is needed,
    // because the ordinary one-word delta plus peek/poke covers it.
    Machine m;
    m.load(store_program(0x10, 0x0000'0000u));  // the LED register
    m.hart().bus().leds()->set_raw(0xaaaa'aaaau);

    rv::dbg::StepHistory history(64);
    const auto step_recording = [&] {
        const StepOutcome outcome = m.hart().step();
        history.record(outcome, m.hart());
    };

    step_recording();
    step_recording();
    step_recording();
    RV_CHECK_HEX(m.hart().bus().leds()->value(), 0xaaaa'aaaau);

    step_recording();  // the store
    RV_CHECK_HEX(m.hart().bus().leds()->value(), 0u);

    history.undo(m.hart());
    RV_CHECK_HEX(m.hart().bus().leds()->value(), 0xaaaa'aaaau);
}

RV_TEST(devices, reverse_stepping_undoes_a_write_to_a_ram_block) {
    Machine m;
    m.hart().bus().attach(8, std::make_unique<RamDevice>(64));
    m.load(store_program(0x80, 0x1234'5000u));
    m.hart().bus().poke_word(0xffff'0080u, 0x9999'9999u);

    rv::dbg::StepHistory history(64);
    for (int i = 0; i < 3; ++i) history.record(m.hart().step(), m.hart());
    history.record(m.hart().step(), m.hart());  // the store
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x1234'5000u);

    history.undo(m.hart());
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x9999'9999u);
}

// ---- configuration ---------------------------------------------------------

RV_TEST(devices, a_configuration_file_describes_a_machine) {
    const DeviceConfig config = parse_device_config(
        "# a comment\n"
        "[[device]]\n"
        "type = \"uart\"\n"
        "slot = 0\n"
        "\n"
        "[[device]]\n"
        "type = \"switches\"\n"
        "slot = 2\n"
        "value = 0b1010\n"
        "\n"
        "[[device]]\n"
        "type = \"rom\"\n"
        "slot = 8\n"
        "size = 64\n",
        "test.toml");

    RV_CHECK(config.ok());
    RV_CHECK_EQ(config.devices.size(), std::size_t{3});
    RV_CHECK_STR(config.devices[1].type, "switches");
    RV_CHECK_HEX(config.devices[1].value, 0b1010u);
    RV_CHECK_EQ(config.devices[2].size, 64u);
}

RV_TEST(devices, applying_a_configuration_replaces_what_was_there) {
    // A machine described in a file gets exactly what the file says, so a
    // configuration is reproducible rather than additive.
    Machine m;
    RV_CHECK(m.hart().bus().uart() != nullptr);

    const DeviceConfig config = parse_device_config(
        "[[device]]\n"
        "type = \"leds\"\n"
        "slot = 0\n",
        "test.toml");
    RV_CHECK_STR(apply_device_config(m.hart().bus(), config), "");

    RV_CHECK(m.hart().bus().uart() == nullptr);
    RV_CHECK(m.hart().bus().leds() != nullptr);
    RV_CHECK_EQ(m.hart().bus().leds()->base_address(), 0xffff'0000u);
}

RV_TEST(devices, a_configuration_round_trips_through_text) {
    Machine m;
    m.hart().bus().switches()->set_value(0x5555);
    const DeviceConfig described = describe(m.hart().bus());
    const DeviceConfig reparsed = parse_device_config(write_device_config(described), "out.toml");

    RV_CHECK(reparsed.ok());
    RV_CHECK_EQ(reparsed.devices.size(), described.devices.size());
    for (std::size_t index = 0; index < reparsed.devices.size(); ++index) {
        RV_CHECK_STR(reparsed.devices[index].type, described.devices[index].type);
        RV_CHECK_EQ(reparsed.devices[index].slot, described.devices[index].slot);
    }
}

RV_TEST(devices, configuration_errors_name_the_line_and_the_problem) {
    const auto fails_with = [](const char* text, const char* needle) {
        const DeviceConfig config = parse_device_config(text, "t.toml");
        return !config.ok() && config.error.find(needle) != std::string::npos;
    };

    RV_CHECK(fails_with("[[device]]\ntype = \"frobnicator\"\n", "unknown device type"));
    // And it says what the known types are, rather than leaving you guessing.
    RV_CHECK(fails_with("[[device]]\ntype = \"frobnicator\"\n", "uart"));
    RV_CHECK(fails_with("[[device]]\ntype = \"uart\"\nslot = 99\n", "out of range"));
    RV_CHECK(fails_with("[[device]]\ntype = \"uart\"\nwibble = 1\n", "unknown key"));
    RV_CHECK(fails_with("[[device]]\ntype = uart\n", "must be quoted"));
    RV_CHECK(fails_with("type = \"uart\"\n", "before any [machine] or [[device]]"));
    RV_CHECK(fails_with("[[device]]\nslot = 0\n", "has no type"));
    // The line number is part of the message.
    RV_CHECK(fails_with("[[device]]\ntype = \"uart\"\nwibble = 1\n", "t.toml:3"));
}

RV_TEST(devices, an_overlapping_file_warns_and_still_builds_the_machine) {
    Machine m;
    const DeviceConfig config = parse_device_config(
        "[[device]]\n"
        "type = \"ram\"\n"
        "slot = 0\n"
        "size = 64\n"
        "\n"
        "[[device]]\n"
        "type = \"leds\"\n"
        "slot = 2\n",  // inside the RAM block's four slots
        "test.toml");
    RV_CHECK(config.ok());

    std::vector<std::string> warnings;
    const std::string error = apply_device_config(m.hart().bus(), config, {}, &warnings);
    RV_CHECK_STR(error, "");
    RV_CHECK_EQ(warnings.size(), std::size_t{1});
    RV_CHECK_NE(warnings.front().find("overlaps"), std::string::npos);
    // Built anyway, with the later one on top.
    RV_CHECK_EQ(m.hart().bus().devices().size(), std::size_t{2});
    RV_CHECK_STR(std::string(m.hart().bus().device_at_slot(2)->type_name()), "leds");
}

// Where a board file is imagined to live, and a load= anchored at the root.
// Spelled per platform because "anchored" is: on Windows a leading separator
// and a drive letter both do it, on POSIX only the separator.
#ifdef _WIN32
constexpr const char* kBoardDir = "C:\\boards";
constexpr const char* kRootedLoad = "\\images\\dmem.mem";
#else
constexpr const char* kBoardDir = "/boards";
constexpr const char* kRootedLoad = "/images/dmem.mem";
#endif

RV_TEST(devices, an_anchored_load_path_is_not_resolved_against_the_board_file) {
    // A load= that fails to open reports the path it tried, so this can ask
    // where the lookup went without putting a .mem file on disk.
    const auto looked_in = [](const char* load) {
        Machine m;
        const DeviceConfig config = parse_device_config(std::string("[[device]]\n"
                                                                    "type = \"ram\"\n"
                                                                    "slot = 0\n"
                                                                    "size = 64\n"
                                                                    "load = \"") +
                                                            load + "\"\n",
                                                        "board.toml");
        RV_CHECK(config.ok());
        return apply_device_config(m.hart().bus(), config, kBoardDir);
    };

    // Relative: resolved against the directory the board file came from.
    const std::string relative = looked_in("dmem.mem");
    RV_CHECK_NE(relative.find("dmem.mem"), std::string::npos);
    RV_CHECK_NE(relative.find(kBoardDir), std::string::npos);

    // Anchored at the root: taken as it stands. Gluing it to the board
    // directory would look somewhere that is not what the file asked for.
    const std::string rooted = looked_in(kRootedLoad);
    RV_CHECK_NE(rooted.find(kRootedLoad), std::string::npos);
    RV_CHECK_EQ(rooted.find(kBoardDir), std::string::npos);

#ifdef _WIN32
    // A drive letter anchors a path too, and does it without a leading
    // separator -- the case a '/' test cannot see.
    const std::string drive = looked_in("C:\\lab\\dmem.mem");
    RV_CHECK_NE(drive.find("C:\\lab\\dmem.mem"), std::string::npos);
    RV_CHECK_EQ(drive.find(kBoardDir), std::string::npos);
#endif
}

// ---------------------------------------------------------------------------
// Blocks as inputs, custom peripherals, and memory sizes
// ---------------------------------------------------------------------------

RV_TEST(devices, a_memory_block_offers_its_first_words_as_fields) {
    // A block is as much an input as a bank of switches is: an exercise that
    // reads a table wants the table set by hand.
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();
    Device* ram = bus.attach(0, std::make_unique<RamDevice>(64));

    const std::vector<InputControl> inputs = ram->inputs();
    RV_CHECK_EQ(inputs.size(), std::size_t{16});
    RV_CHECK_STR(inputs.front().name, "word[0]");

    ram->set_input(2, 0xdeadbeef);
    RV_CHECK_HEX(ram->peek(8), 0xdeadbeefu);
    RV_CHECK_HEX(bus.peek_word(ram->base_address() + 8), 0xdeadbeefu);

    // And what was typed survives a reset, since it was not the program that
    // put it there.
    ram->reset();
    RV_CHECK_HEX(ram->peek(8), 0xdeadbeefu);
}

RV_TEST(devices, a_large_block_offers_a_workable_number_of_fields) {
    RamDevice big(4096);
    RV_CHECK_EQ(big.inputs().size(), RamDevice::kInputWords);
}

RV_TEST(devices, a_rom_is_still_settable_by_hand) {
    // Same reason the memory view can edit one: this is the person's path.
    RamDevice rom(64, /*read_only=*/true);
    rom.set_input(0, 0x1234);
    RV_CHECK_HEX(rom.peek(0), 0x1234u);
}

RV_TEST(devices, a_custom_peripheral_is_named_readable_and_writable) {
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();
    auto* sensor = static_cast<CustomDevice*>(
        bus.attach(0, std::make_unique<CustomDevice>(2, "sensor")));
    RV_CHECK(sensor != nullptr);
    // The name lives on the device rather than in its summary, since the list
    // shows it in the column that used to hold only the type.
    RV_CHECK_STR(sensor->name(), "sensor");
    RV_CHECK_STR(sensor->display_name(), "sensor");

    // The person sets a register; the program reads it.
    sensor->set_input(1, 42);
    RV_CHECK_EQ(bus.load(sensor->base_address() + 4, 4, false).value, 42u);

    // The program writes one; it stays written.
    bus.store(sensor->base_address(), 4, 7);
    RV_CHECK_EQ(sensor->peek(0), 7u);
}

// ---- a peripheral that computes -------------------------------------------

RV_TEST(devices, a_function_device_computes_rather_than_remembers) {
    // The gap `custom` cannot close. A register file hands back what it was
    // given; an IP core's output is a function of its input, and this is the
    // difference between the two.
    FunctionDevice mul(FunctionOp::Mul, 8, 7, "multiplier");
    mul.set_now(100);
    mul.write(FunctionDevice::kOffsetA, 4, 31);
    mul.write(FunctionDevice::kOffsetB, 4, 31);

    // The write cycle itself reads idle, then seven cycles of busy, then done:
    // the count the homework's controller keeps, in this machine's clock.
    RV_CHECK_EQ(mul.status(), FunctionDevice::kIdle);
    for (u32 elapsed = 1; elapsed <= 7; ++elapsed) {
        mul.set_now(100 + elapsed);
        RV_CHECK_EQ(mul.status(), FunctionDevice::kBusy);
    }
    mul.set_now(108);
    RV_CHECK_EQ(mul.status(), FunctionDevice::kDone);

    // 31 * 31 = 961 = 0x03c1, read back as two 8-bit halves.
    RV_CHECK_HEX(mul.peek(FunctionDevice::kResultLow), 0xc1u);
    RV_CHECK_HEX(mul.peek(FunctionDevice::kResultHigh), 0x03u);
    RV_CHECK_HEX(mul.peek(FunctionDevice::kOffsetA), 31u);
}

RV_TEST(devices, any_write_to_a_function_device_restarts_it) {
    // The rule the homework's guideline warns about, reproduced rather than
    // smoothed over: a program that writes while polling never sees a result,
    // and finding that out on the emulator is the point.
    FunctionDevice mul(FunctionOp::Mul, 8, 7, "multiplier");
    mul.set_now(0);
    mul.write(FunctionDevice::kOffsetA, 4, 6);
    mul.write(FunctionDevice::kOffsetB, 4, 7);
    mul.set_now(8);
    RV_CHECK_EQ(mul.status(), FunctionDevice::kDone);

    // A store to the status register loads nothing -- and still restarts it.
    mul.write(FunctionDevice::kStatus, 4, 0);
    RV_CHECK_EQ(mul.status(), FunctionDevice::kIdle);
    mul.set_now(9);
    RV_CHECK_EQ(mul.status(), FunctionDevice::kBusy);
    RV_CHECK_HEX(mul.peek(FunctionDevice::kOffsetA), 6u);  // nothing was loaded
}

RV_TEST(devices, a_function_device_latches_its_result) {
    // While one operation is in flight the result registers still hold the
    // last one. The RTL has to latch because its product is combinational;
    // reproducing it is what makes a program that reads too early wrong here
    // in the same way it is wrong on the board.
    FunctionDevice mul(FunctionOp::Mul, 8, 7, "multiplier");
    mul.set_now(0);
    mul.write(FunctionDevice::kOffsetA, 4, 3);
    mul.write(FunctionDevice::kOffsetB, 4, 5);
    mul.set_now(8);
    RV_CHECK_HEX(mul.result(), 15u);

    mul.set_now(9);
    mul.write(FunctionDevice::kOffsetB, 4, 9);  // 3 * 9 = 27, once it finishes
    mul.set_now(12);
    RV_CHECK_EQ(mul.status(), FunctionDevice::kBusy);
    RV_CHECK_HEX(mul.result(), 15u);  // still the one before
    mul.set_now(17);
    RV_CHECK_HEX(mul.result(), 27u);
}

RV_TEST(devices, peeking_a_function_device_does_not_start_it) {
    // A memory view redraws constantly, and the Devices panel calls
    // state_summary on every refresh. Either restarting the peripheral would
    // mean a program's timing depended on whether anyone was looking.
    FunctionDevice mul(FunctionOp::Mul, 8, 7, "multiplier");
    mul.set_now(0);
    mul.write(FunctionDevice::kOffsetA, 4, 2);
    mul.write(FunctionDevice::kOffsetB, 4, 2);
    for (int i = 0; i < 50; ++i) {
        (void)mul.peek(FunctionDevice::kStatus);
        (void)mul.state_summary();
    }
    mul.set_now(8);
    RV_CHECK_EQ(mul.status(), FunctionDevice::kDone);
    RV_CHECK_HEX(mul.result(), 4u);

    // Nor does setting a register by hand from the memory view. That is the
    // person's path, not the program's: only a store starts the peripheral.
    mul.poke(FunctionDevice::kOffsetA, 5);
    RV_CHECK_EQ(mul.status(), FunctionDevice::kDone);
    RV_CHECK_HEX(mul.result(), 10u);
}

RV_TEST(devices, a_function_device_is_as_wide_as_it_was_ordered) {
    // Operands are masked to the width, and the result comes back as two
    // halves of that width -- an 8 x 8 IP cannot be handed 300.
    FunctionDevice narrow(FunctionOp::Mul, 4, 1, "nibbles");
    narrow.set_now(0);
    narrow.write(FunctionDevice::kOffsetA, 4, 0xff);  // masked to 0xf
    narrow.write(FunctionDevice::kOffsetB, 4, 0xf);
    narrow.set_now(2);
    RV_CHECK_HEX(narrow.result(), 225u);                              // 15 * 15
    RV_CHECK_HEX(narrow.peek(FunctionDevice::kResultLow), 0x1u);      // 0xe1 low nibble
    RV_CHECK_HEX(narrow.peek(FunctionDevice::kResultHigh), 0xeu);

    // The widest it can be ordered still fits its result in one register.
    FunctionDevice wide(FunctionOp::Mul, kMaxFunctionWidth, 1, "wide");
    wide.set_now(0);
    wide.write(FunctionDevice::kOffsetA, 4, 0xffff);
    wide.write(FunctionDevice::kOffsetB, 4, 0xffff);
    wide.set_now(2);
    RV_CHECK_HEX(wide.result(), 0xfffe'0001u);
}

RV_TEST(devices, reverse_stepping_undoes_a_write_to_a_function_device) {
    // A write moves three things no register holds: which operand is loaded,
    // when the operation started, and the result the one before it left
    // behind. The claim is that the ordinary one-word delta plus the aux field
    // still covers it, with no per-device history.
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();
    auto* mul = dynamic_cast<FunctionDevice*>(
        bus.attach(0, std::make_unique<FunctionDevice>(FunctionOp::Mul, 8, 1, "multiplier")));
    RV_CHECK(mul != nullptr);
    if (mul == nullptr) return;

    // Set up an operation by hand and let the clock carry it to done, so there
    // is a latched result for the store to displace. After loading, not
    // before: loading resets the machine, and a reset clears a peripheral.
    m.load(store_program(FunctionDevice::kOffsetA, 9));
    mul->poke(FunctionDevice::kOffsetA, 3);
    mul->poke(FunctionDevice::kOffsetB, 4);

    rv::dbg::StepHistory history(64);
    for (int i = 0; i < 3; ++i) history.record(m.hart().step(), m.hart());
    RV_CHECK_EQ(mul->status(), FunctionDevice::kDone);
    RV_CHECK_HEX(mul->result(), 12u);

    history.record(m.hart().step(), m.hart());  // the store: A <- 9
    RV_CHECK_HEX(mul->peek(FunctionDevice::kOffsetA), 9u);
    // Busy rather than idle: idle lasts exactly the cycle the write lands on,
    // so between two instructions the clock has already moved past it. That is
    // the RTL's behaviour too -- nothing can read the status in the cycle it
    // wrote, which is why a poll loop always sees busy at least once.
    RV_CHECK_EQ(mul->status(), FunctionDevice::kBusy);
    RV_CHECK_HEX(mul->result(), 12u);  // the finished one, latched on the way past

    history.undo(m.hart());
    RV_CHECK_HEX(mul->peek(FunctionDevice::kOffsetA), 3u);
    RV_CHECK_EQ(mul->status(), FunctionDevice::kDone);
    RV_CHECK_HEX(mul->result(), 12u);
}

RV_TEST(devices, a_function_device_round_trips_through_a_file) {
    const DeviceConfig config = parse_device_config(
        "[[device]]\n"
        "type = \"function\"\n"
        "name = \"multiplier\"\n"
        "address = 0xffff0060\n"
        "operation = \"mul\"\n"
        "latency = 7\n"
        "size = 8\n",
        "test.toml");
    RV_CHECK_STR(config.error, "");

    Machine m;
    RV_CHECK_STR(apply_device_config(m.hart().bus(), config), "");
    auto* mul = dynamic_cast<FunctionDevice*>(m.hart().bus().device_for_address(0xffff'0060u));
    RV_CHECK(mul != nullptr);
    if (mul == nullptr) return;
    RV_CHECK_STR(mul->label(), "multiplier");
    RV_CHECK_EQ(mul->width(), 8);
    RV_CHECK_EQ(mul->latency(), 7u);
    // Five registers one word apart do not fit in a sixteen-byte slot, so the
    // status register lands in the second one.
    RV_CHECK_EQ(mul->slot_count(), std::size_t{2});
    RV_CHECK_EQ(m.hart().bus().device_for_address(0xffff'0070u), mul);

    // What it computes, how wide and how long it takes all survive a save.
    const DeviceConfig again = parse_device_config(write_device_config(describe(m.hart().bus())),
                                                   "again.toml");
    RV_CHECK_STR(again.error, "");
    bool found = false;
    for (const DeviceSpec& spec : again.devices) {
        if (spec.type != "function") continue;
        found = true;
        RV_CHECK_STR(spec.operation, "mul");
        RV_CHECK_EQ(spec.latency, 7u);
        RV_CHECK_EQ(spec.size, 8u);
        RV_CHECK_STR(spec.name, "multiplier");
    }
    RV_CHECK(found);
}

RV_TEST(devices, an_unknown_operation_names_what_is_known) {
    const DeviceConfig config = parse_device_config(
        "[[device]]\n"
        "type = \"function\"\n"
        "operation = \"integrate\"\n",
        "test.toml");
    RV_CHECK(!config.ok());
    RV_CHECK(config.error.find("test.toml:3") != std::string::npos);
    RV_CHECK(config.error.find("mul") != std::string::npos);
}

RV_TEST(devices, a_custom_peripheral_can_raise_an_interrupt) {
    // The point of it being custom: a student can build the source, not only
    // handle one somebody else provided.
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();
    auto* device = static_cast<CustomDevice*>(
        bus.attach(0, std::make_unique<CustomDevice>(1, "knock")));

    RV_CHECK_HEX(bus.pending_interrupts(0), 0u);
    device->set_input(1, 1);  // the line, which follows the registers
    RV_CHECK_HEX(bus.pending_interrupts(0), kIrqExternal);
}

RV_TEST(devices, memory_sizes_come_from_the_machine_table) {
    const DeviceConfig config = parse_device_config(
        "[machine]\n"
        "imem = 4096\n"
        "dmem = 2048\n"
        "\n"
        "[[device]]\n"
        "type = \"uart\"\n"
        "address = 0xffff0000\n",
        "test.toml");
    RV_CHECK(config.ok());
    RV_CHECK_EQ(config.imem_size, 4096u);
    RV_CHECK_EQ(config.dmem_size, 2048u);
    RV_CHECK_EQ(config.devices.front().slot, std::size_t{0});

    Machine m;
    RV_CHECK_STR(apply_machine_config(m.hart(), config), "");
    RV_CHECK_EQ(m.hart().imem().size(), 4096u);
    RV_CHECK_EQ(m.hart().bus().dmem().size(), 2048u);
}

RV_TEST(devices, an_address_names_a_slot_and_a_bad_size_is_refused) {
    RV_CHECK_EQ(slot_of_address(kMmioBase), std::size_t{0});
    RV_CHECK_EQ(slot_of_address(kMmioBase + 0x40), std::size_t{4});
    // Inside a slot means that slot -- a person naming an address inside a
    // device means that device.
    RV_CHECK_EQ(slot_of_address(kMmioBase + 0x4c), std::size_t{4});

    const DeviceConfig odd = parse_device_config("[machine]\nimem = 1000\n", "t.toml");
    RV_CHECK(odd.ok());  // 1000 is a multiple of 4
    const DeviceConfig tiny = parse_device_config("[machine]\nimem = 8\n", "t.toml");
    RV_CHECK(!tiny.ok());
    RV_CHECK_NE(tiny.error.find("between"), std::string::npos);

    const DeviceConfig ragged = parse_device_config("[machine]\nimem = 4098\n", "t.toml");
    RV_CHECK(!ragged.ok());
    RV_CHECK_NE(ragged.error.find("multiple of 4"), std::string::npos);
}

RV_TEST(devices, growing_a_memory_keeps_what_was_in_it) {
    Machine m;
    m.hart().bus().dmem().write_word_raw(0x10, 0xcafebabe);
    m.hart().resize_memories(kDefaultImemSize, 32 * 1024);
    RV_CHECK_HEX(m.hart().bus().dmem().read_word_raw(0x10), 0xcafebabeu);
    RV_CHECK_EQ(m.hart().bus().dmem().size(), 32u * 1024);
}

RV_TEST(devices, a_machine_round_trips_through_a_file) {
    Machine m;
    m.hart().resize_memories(8192, 4096);
    m.hart().bus().detach_all();
    m.hart().bus().attach(0, std::make_unique<CustomDevice>(3, "sensor"));

    const std::string text = write_device_config(describe(m.hart()));
    RV_CHECK_NE(text.find("[machine]"), std::string::npos);
    RV_CHECK_NE(text.find("imem = 8192"), std::string::npos);
    RV_CHECK_NE(text.find("address = 0xffff0000"), std::string::npos);
    RV_CHECK_NE(text.find("name = \"sensor\""), std::string::npos);

    const DeviceConfig back = parse_device_config(text, "round.toml");
    RV_CHECK(back.ok());
    Machine other;
    RV_CHECK_STR(apply_machine_config(other.hart(), back), "");
    RV_CHECK_EQ(other.hart().imem().size(), 8192u);
    auto* custom = other.hart().bus().find<CustomDevice>();
    RV_CHECK(custom != nullptr);
    RV_CHECK_STR(custom->name(), "sensor");
    RV_CHECK_EQ(custom->word_count(), 3u);
}

// ---------------------------------------------------------------------------
// How wide a device is
// ---------------------------------------------------------------------------

RV_TEST(devices, a_bank_is_as_wide_as_the_board_it_stands_for) {
    // A program that writes bit 20 of an eight-LED bank is writing to nothing.
    // Reading back what was written would be the emulator agreeing with a
    // program the board will not.
    LedDevice eight(8);
    RV_CHECK_EQ(eight.width(), 8);
    eight.poke(0, 0xffffffff);
    RV_CHECK_HEX(eight.peek(0), 0xffu);
    RV_CHECK_EQ(eight.state_summary().size(), std::string("00000000ff  ").size() - 2 + 8);

    SwitchDevice sixteen(16);
    sixteen.set_input(0, 0xffffffff);
    RV_CHECK_HEX(sixteen.value(), 0xffffu);
    RV_CHECK_EQ(sixteen.inputs().front().bit_count, 16);
}

RV_TEST(devices, a_row_of_buttons_is_one_device) {
    // Four direction buttons are four bits of one peripheral, not four
    // peripherals taking four addresses.
    ButtonDevice pad(4);
    RV_CHECK_EQ(pad.slot_count(), std::size_t{1});
    // Four buttons plus the interrupt switch.
    RV_CHECK_EQ(pad.inputs().size(), std::size_t{5});

    pad.set_input(2, 1);
    RV_CHECK_HEX(pad.peek(0), 0b0100u);
    RV_CHECK_HEX(pad.interrupt_lines(0), kIrqExternal);

    pad.set_input(2, 0);
    RV_CHECK_HEX(pad.interrupt_lines(0), 0u);

    // The last control is still the interrupt switch, whatever the count.
    pad.set_input(4, 0);
    pad.set_input(0, 1);
    RV_CHECK_HEX(pad.interrupt_lines(0), 0u);
}

RV_TEST(devices, a_width_survives_a_round_trip_through_a_file) {
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();
    bus.attach(0, std::make_unique<LedDevice>(8));
    bus.attach(1, std::make_unique<SwitchDevice>(16));
    bus.attach(2, std::make_unique<ButtonDevice>(4));

    const std::string text = write_device_config(describe(m.hart()));
    Machine other;
    const DeviceConfig back = parse_device_config(text, "widths.toml");
    RV_CHECK(back.ok());
    RV_CHECK_STR(apply_machine_config(other.hart(), back), "");

    RV_CHECK_EQ(other.hart().bus().find<LedDevice>()->width(), 8);
    RV_CHECK_EQ(other.hart().bus().find<SwitchDevice>()->width(), 16);
    RV_CHECK_EQ(other.hart().bus().find<ButtonDevice>()->width(), 4);
}

RV_TEST(devices, a_full_width_bank_says_nothing_about_its_width) {
    // The default is the whole register, and a file full of `size = 32` would
    // be noise.
    Machine m;
    m.hart().bus().detach_all();
    m.hart().bus().attach(0, std::make_unique<LedDevice>());
    RV_CHECK_EQ(write_device_config(describe(m.hart().bus())).find("size"), std::string::npos);
}

RV_TEST(devices, the_offered_sizes_are_powers_of_two) {
    // This is hardware: a width or a depth that is not a power of two wastes
    // what it is built from.
    for (const SizeUnit unit : {SizeUnit::Bits, SizeUnit::Bytes, SizeUnit::Words}) {
        const std::vector<u32>& sizes = common_sizes(unit);
        RV_CHECK(!sizes.empty());
        for (const u32 size : sizes) RV_CHECK_EQ(size & (size - 1), 0u);
    }
    RV_CHECK(common_sizes(SizeUnit::None).empty());
}

RV_TEST(devices, any_device_may_be_named_so_two_of_a_kind_can_be_told_apart) {
    // Once a machine holds two UARTs -- one to a terminal, one to a sensor --
    // the type name stops identifying anything.
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();
    Device* console = bus.attach(0, make_device("uart", 0, "console"));
    Device* plotter = bus.attach(1, make_device("uart", 0, "plotter"));
    RV_CHECK_STR(console->display_name(), "console");
    RV_CHECK_STR(plotter->display_name(), "plotter");

    // Unnamed falls back to the type rather than storing it, which would make
    // every device look deliberately named.
    Device* plain = bus.attach(2, make_device("leds"));
    RV_CHECK(plain->label().empty());
    RV_CHECK_STR(plain->display_name(), "leds");

    // And the names survive a round trip.
    const DeviceConfig back =
        parse_device_config(write_device_config(describe(bus)), "named.toml");
    RV_CHECK(back.ok());
    Machine other;
    RV_CHECK_STR(apply_device_config(other.hart().bus(), back), "");
    RV_CHECK_STR(other.hart().bus().device_at_slot(0)->display_name(), "console");
    RV_CHECK_STR(other.hart().bus().device_at_slot(1)->display_name(), "plotter");
    RV_CHECK(other.hart().bus().device_at_slot(2)->label().empty());
}
