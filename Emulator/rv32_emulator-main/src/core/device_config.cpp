#include "core/device_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/devices.hpp"
#include "core/memfile.hpp"

namespace rv::core {
namespace {

/// Whether a memory size is one a machine can actually have.
std::string check_memory_size(const std::string& what, u32 size) {
    if (size % 4 != 0) {
        return what + " must be a multiple of 4 bytes -- both spaces are exported a word per line";
    }
    if (size < kMinMemorySize || size > kMaxMemorySize) {
        return what + " must be between " + std::to_string(kMinMemorySize) + " and " +
               std::to_string(kMaxMemorySize) + " bytes";
    }
    return {};
}

/// "0xffff0040" -- how a person names an address, and how the memory map
/// documents one.
std::string hex_address(Addr addr) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08x", addr);
    return text;
}

std::string trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

/// Strip a `#` comment, respecting quotes so a path containing one survives.
std::string strip_comment(std::string_view line) {
    bool in_string = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"') in_string = !in_string;
        if (line[index] == '#' && !in_string) return std::string(line.substr(0, index));
    }
    return std::string(line);
}

struct Value {
    std::string text;
    bool quoted = false;
};

/// Parse an integer in decimal, 0x hex or 0b binary, with `_` separators.
bool parse_number(std::string_view text, u32& out) {
    std::string cleaned;
    for (const char character : text) {
        if (character != '_') cleaned.push_back(character);
    }
    if (cleaned.empty()) return false;

    int base = 10;
    std::size_t start = 0;
    if (cleaned.size() > 2 && cleaned[0] == '0') {
        if (cleaned[1] == 'x' || cleaned[1] == 'X') {
            base = 16;
            start = 2;
        } else if (cleaned[1] == 'b' || cleaned[1] == 'B') {
            base = 2;
            start = 2;
        }
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(cleaned.c_str() + start, &end, base);
    if (end == cleaned.c_str() + start || *end != '\0' || value > 0xffff'ffffull) return false;
    out = static_cast<u32>(value);
    return true;
}

std::string join_path(const std::string& directory, const std::string& file) {
    if (directory.empty() || file.empty()) return file;
    // Anchored paths are left alone. Testing for a leading '/' would be the
    // POSIX half of that question only: on Windows a path is also anchored by
    // a drive ("C:\lab\dmem.mem") or by a bare root ("\dmem.mem"), and
    // gluing either onto the board file's directory produces nonsense.
    const std::filesystem::path relative(file);
    if (relative.has_root_name() || relative.has_root_directory()) return file;
    return (std::filesystem::path(directory) / relative).string();
}

}  // namespace

// ---------------------------------------------------------------------------
// Catalogue
// ---------------------------------------------------------------------------

const std::vector<u32>& common_sizes(SizeUnit unit) {
    static const std::vector<u32> kNone;
    static const std::vector<u32> kBits = {1, 2, 4, 8, 16, 32};
    static const std::vector<u32> kBytes = {64, 128, 256, 512, 1024, 2048, 4096};
    static const std::vector<u32> kWords = {1, 2, 4, 8, 16, 32};
    // Stops at kMaxFunctionWidth: past that the result no longer fits in the
    // two halves of a register it is read back through.
    static const std::vector<u32> kOperandBits = {4, 8, 16};
    switch (unit) {
        case SizeUnit::None: return kNone;
        case SizeUnit::Bits: return kBits;
        case SizeUnit::Bytes: return kBytes;
        case SizeUnit::Words: return kWords;
        case SizeUnit::OperandBits: return kOperandBits;
    }
    return kNone;
}

u32 default_size(SizeUnit unit) {
    switch (unit) {
        case SizeUnit::None: return 0;
        // A whole register, and a block big enough to be worth having.
        case SizeUnit::Bits: return kMaxWidth;
        case SizeUnit::Bytes: return 256;
        case SizeUnit::Words: return 4;
        // 8 x 8: the multiplier IP the coursework is built around.
        case SizeUnit::OperandBits: return static_cast<u32>(kDefaultFunctionWidth);
    }
    return 0;
}

std::string memory_size_label(u32 bytes) {
    // Below a kilobyte, say bytes. "0 KB" is not a size.
    const std::string amount = bytes >= 1024 ? std::to_string(bytes / 1024) + " KB"
                                             : std::to_string(bytes) + " B";
    return amount + " \u2014 " + std::to_string(bytes / 4) + " \u00d7 32-bit";
}

std::string_view unit_name(SizeUnit unit) {
    switch (unit) {
        case SizeUnit::None: return "";
        case SizeUnit::Bits: return "bits";
        case SizeUnit::Bytes: return "bytes";
        case SizeUnit::Words: return "registers";
        case SizeUnit::OperandBits: return "bits per operand";
    }
    return "";
}

const std::vector<DeviceType>& device_catalogue() {
    static const std::vector<DeviceType> types = {
        {"uart", "character output", SizeUnit::None, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<UartDevice>();
         }},
        {"leds", "output register, as many bits as the board has LEDs", SizeUnit::Bits, false,
         [](u32 width, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<LedDevice>(width == 0 ? kMaxWidth : static_cast<int>(width));
         }},
        {"switches", "input register, one bit per switch", SizeUnit::Bits, false,
         [](u32 width, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<SwitchDevice>(width == 0 ? kMaxWidth
                                                             : static_cast<int>(width));
         }},
        {"button", "momentary inputs, can raise IRQ", SizeUnit::Bits, false,
         [](u32 count, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<ButtonDevice>(count == 0 ? 1 : static_cast<int>(count));
         }},
        {"value", "numeric input register", SizeUnit::None, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<ValueDevice>();
         }},
        {"display", "7-segment digits", SizeUnit::None, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<DisplayDevice>();
         }},
        {"mtime", "instruction count / mtime", SizeUnit::None, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<CycleDevice>();
         }},
        {"mtimecmp", "timer compare", SizeUnit::None, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<TimerDevice>();
         }},
        {"irq", "software/external IRQ requests", SizeUnit::None, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<IrqDevice>();
         }},
        {"ram", "memory block, words settable by hand", SizeUnit::Bytes, false,
         [](u32 size, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<RamDevice>(size == 0 ? 256u : size, false);
         }},
        {"rom", "read-only memory block", SizeUnit::Bytes, false,
         [](u32 size, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<RamDevice>(size == 0 ? 256u : size, true);
         }},
        {"function", "a result computed from its operands, after a latency",
         SizeUnit::OperandBits, true,
         [](u32 width, const std::string& name) -> std::unique_ptr<Device> {
             // `size` is the operand width in bits, the way an IP core is
             // ordered: an 8 x 8 multiplier is what the IP homework configures.
             return std::make_unique<FunctionDevice>(
                 FunctionOp::Mul,
                 width == 0 ? kDefaultFunctionWidth : static_cast<int>(width),
                 kDefaultFunctionLatency, name.empty() ? "function" : name);
         }},
        {"custom", "your own registers, settable by hand, can raise IRQ", SizeUnit::Words, true,
         [](u32 size, const std::string& name) -> std::unique_ptr<Device> {
             // `size` is words here rather than bytes: a custom peripheral is
             // counted in registers, which is how its author thinks of it.
             return std::make_unique<CustomDevice>(size == 0 ? 4u : size,
                                                   name.empty() ? "custom" : name);
         }},
    };
    return types;
}

const DeviceType* find_device_type(std::string_view name) {
    for (const DeviceType& type : device_catalogue()) {
        if (type.name == name) return &type;
    }
    return nullptr;
}

std::unique_ptr<Device> make_device(std::string_view type, u32 size, const std::string& name) {
    const DeviceType* found = find_device_type(type);
    if (found == nullptr) return nullptr;
    std::unique_ptr<Device> device = found->create(size, name);
    // Every type may be named, not only the one that has to be: a machine with
    // two UARTs needs to say which is which, and that is the moment the type
    // name stops identifying anything.
    if (device != nullptr && !name.empty()) device->set_label(name);
    return device;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

DeviceConfig parse_device_config(const std::string& text, const std::string& source_name) {
    DeviceConfig config;
    std::istringstream stream(text);
    std::string raw;
    int line_number = 0;
    bool in_device = false;
    bool in_machine = false;

    const auto fail = [&](const std::string& message) {
        config.error = source_name + ":" + std::to_string(line_number) + ": " + message;
    };

    while (std::getline(stream, raw)) {
        ++line_number;
        const std::string line = trim(strip_comment(raw));
        if (line.empty()) continue;

        if (line == "[[device]]") {
            config.devices.emplace_back();
            in_device = true;
            in_machine = false;
            continue;
        }
        if (line == "[machine]") {
            in_machine = true;
            in_device = false;
            continue;
        }
        if (!line.empty() && line[0] == '[') {
            fail("unknown table '" + line + "'; only [machine] and [[device]] are understood");
            return config;
        }
        if (!in_device && !in_machine) {
            fail("'" + line + "' appears before any [machine] or [[device]]");
            return config;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            fail("expected 'key = value', found '" + line + "'");
            return config;
        }
        const std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));

        bool quoted = false;
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
            quoted = true;
        }

        const auto number = [&](u32& target) {
            if (!parse_number(value, target)) {
                fail("'" + value + "' is not a number");
                return false;
            }
            return true;
        };

        if (in_machine) {
            if (key == "imem" || key == "dmem") {
                u32 size = 0;
                if (!number(size)) return config;
                const std::string checked = check_memory_size(key, size);
                if (!checked.empty()) {
                    fail(checked);
                    return config;
                }
                (key == "imem" ? config.imem_size : config.dmem_size) = size;
                continue;
            }
            if (key == "imem_base" || key == "dmem_base" || key == "reset") {
                u32 addr = 0;
                if (!number(addr)) return config;
                if (key == "imem_base") {
                    config.imem_base = addr;
                    config.has_imem_base = true;
                } else if (key == "dmem_base") {
                    config.dmem_base = addr;
                    config.has_dmem_base = true;
                } else {
                    config.reset = addr;
                    config.has_reset = true;
                }
                continue;
            }
            fail("unknown key '" + key +
                 "' in [machine]; expected imem, dmem, imem_base, dmem_base, or reset");
            return config;
        }

        DeviceSpec& spec = config.devices.back();

        if (key == "type") {
            if (!quoted) {
                fail("type must be quoted, as type = \"" + value + "\"");
                return config;
            }
            if (find_device_type(value) == nullptr) {
                std::string known;
                for (const DeviceType& type : device_catalogue()) {
                    if (!known.empty()) known += ", ";
                    known += type.name;
                }
                fail("unknown device type '" + value + "'; known types are " + known);
                return config;
            }
            spec.type = value;
        } else if (key == "slot") {
            u32 slot = 0;
            if (!number(slot)) return config;
            if (slot >= kMmioSlots) {
                fail("slot " + std::to_string(slot) + " is out of range (0.." +
                     std::to_string(kMmioSlots - 1) + ")");
                return config;
            }
            spec.slot = slot;
        } else if (key == "address") {
            u32 addr = 0;
            if (!number(addr)) return config;
            // Any absolute address is allowed: the default teaching window is
            // 0xFFFF0000, but a uBee SoC board places peripherals at
            // 0x4000_xxxx / 0x6000_0000.
            if ((addr % kSlotSize) != 0) {
                fail("address " + hex_address(addr) + " must be " + std::to_string(kSlotSize) +
                     "-byte aligned");
                return config;
            }
            spec.address = addr;
            spec.has_address = true;
            if (addr >= kMmioBase && addr - kMmioBase < kMmioSize) {
                spec.slot = slot_of_address(addr);
            }
        } else if (key == "name") {
            spec.name = value;
        } else if (key == "size") {
            if (!number(spec.size)) return config;
        } else if (key == "value") {
            if (!number(spec.value)) return config;
            spec.has_value = true;
        } else if (key == "load") {
            spec.load = value;
        } else if (key == "readonly" || key == "read_only") {
            spec.read_only = value == "true" || value == "1";
        } else if (key == "digits") {
            u32 digits = 4;
            if (!number(digits)) return config;
            spec.digits = static_cast<int>(digits);
        } else if (key == "operation") {
            FunctionOp op = FunctionOp::Mul;
            if (!parse_function_op(value, op)) {
                std::string known;
                for (const std::string_view name : function_op_names()) {
                    if (!known.empty()) known += ", ";
                    known += name;
                }
                fail("unknown operation '" + value + "'; known operations are " + known);
                return config;
            }
            spec.operation = value;
        } else if (key == "latency") {
            if (!number(spec.latency)) return config;
            spec.has_latency = true;
        } else {
            fail("unknown key '" + key +
                 "'; understood keys are type, slot, size, value, load, readonly, digits, "
                 "operation, latency");
            return config;
        }
    }

    for (std::size_t index = 0; index < config.devices.size(); ++index) {
        if (config.devices[index].type.empty()) {
            config.error = source_name + ": device " + std::to_string(index + 1) + " has no type";
            return config;
        }
    }
    return config;
}

std::string directory_of(const std::string& path) {
#ifdef _WIN32
    // Windows accepts both separators and mixes them freely: a board file
    // chosen from the GUI's dialog arrives with backslashes, one typed on the
    // command line usually with forward slashes.
    const std::size_t slash = path.find_last_of("/\\");
#else
    const std::size_t slash = path.find_last_of('/');
#endif
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

DeviceConfig read_device_config(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        DeviceConfig config;
        config.error = "cannot open '" + path + "'";
        return config;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return parse_device_config(contents.str(), path);
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

std::string write_device_config(const DeviceConfig& config) {
    std::ostringstream out;
    out << "# rv32 machine configuration.\n"
           "# Devices may sit in the legacy window at 0xffff0000 or at absolute\n"
           "# SoC addresses (see docs/memory-map.md and examples/ubee.toml).\n";

    if (config.imem_size != 0 || config.dmem_size != 0 || config.has_imem_base ||
        config.has_dmem_base || config.has_reset) {
        out << "\n[machine]\n";
        if (config.imem_size != 0) out << "imem = " << config.imem_size << "\n";
        if (config.dmem_size != 0) out << "dmem = " << config.dmem_size << "\n";
        if (config.has_imem_base) out << "imem_base = " << hex_address(config.imem_base) << "\n";
        if (config.has_dmem_base) out << "dmem_base = " << hex_address(config.dmem_base) << "\n";
        if (config.has_reset) out << "reset = " << hex_address(config.reset) << "\n";
    }

    for (const DeviceSpec& spec : config.devices) {
        out << "\n[[device]]\n";
        out << "type = \"" << spec.type << "\"\n";
        // Written as an address rather than a slot number: an address is what
        // the program uses and what the panel shows, so a file that says one
        // can be read against the code without a conversion in between.
        const Addr addr = spec.has_address ? spec.address : slot_address(spec.slot);
        out << "address = " << hex_address(addr) << "\n";
        if (!spec.name.empty()) out << "name = \"" << spec.name << "\"\n";
        if (!spec.operation.empty()) out << "operation = \"" << spec.operation << "\"\n";
        if (spec.has_latency) out << "latency = " << spec.latency << "\n";
        if (spec.size != 0) out << "size = " << spec.size << "\n";
        if (spec.read_only) out << "readonly = true\n";
        if (!spec.load.empty()) out << "load = \"" << spec.load << "\"\n";
        if (spec.has_value) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "0x%08x", spec.value);
            out << "value = " << buffer << "\n";
        }
    }
    return out.str();
}

DeviceConfig describe(const Hart& hart) {
    DeviceConfig config = describe(hart.bus());
    config.imem_size = hart.imem().size();
    config.dmem_size = hart.bus().dmem().size();
    config.imem_base = hart.imem().base();
    config.dmem_base = hart.bus().dmem().base();
    config.has_imem_base = config.imem_base != 0;
    config.has_dmem_base = config.dmem_base != 0;
    if (hart.reset_entry() != 0) {
        config.reset = hart.reset_entry();
        config.has_reset = true;
    }
    return config;
}

std::string apply_machine_config(Hart& hart, const DeviceConfig& config,
                                 const std::string& base_directory,
                                 std::vector<std::string>* warnings) {
    if (!config.ok()) return config.error;
    if (config.imem_size != 0 || config.dmem_size != 0) {
        hart.resize_memories(config.imem_size != 0 ? config.imem_size : hart.imem().size(),
                             config.dmem_size != 0 ? config.dmem_size : hart.bus().dmem().size());
    }
    if (config.has_imem_base || config.has_dmem_base) {
        hart.set_memory_bases(config.has_imem_base ? config.imem_base : hart.imem().base(),
                              config.has_dmem_base ? config.dmem_base : hart.bus().dmem().base());
    }
    if (config.has_reset) hart.set_reset_entry(config.reset);
    return apply_device_config(hart.bus(), config, base_directory, warnings);
}

DeviceConfig describe(const Bus& bus) {
    DeviceConfig config;
    for (Device* device : bus.devices()) {
        DeviceSpec spec;
        spec.type = std::string(device->type_name());
        spec.slot = device->slot() < kMmioSlots ? device->slot() : 0;
        spec.address = device->base_address();
        spec.has_address = true;

        if (const auto* ram = dynamic_cast<const RamDevice*>(device)) {
            spec.size = ram->size();
            spec.read_only = ram->read_only();
        }
        spec.name = device->label();
        if (const auto* custom = dynamic_cast<const CustomDevice*>(device)) {
            spec.size = custom->word_count();
        }
        if (const auto* function = dynamic_cast<const FunctionDevice*>(device)) {
            // All three, always: what it computes, how wide, and how long it
            // takes are the device, and a file that left any of them out would
            // describe a different peripheral.
            spec.operation = std::string(function_op_name(function->operation()));
            spec.size = static_cast<u32>(function->width());
            spec.latency = function->latency();
            spec.has_latency = true;
        }
        // A width is only worth writing when it is not the whole register --
        // a file full of `size = 32` says nothing.
        if (const auto* leds = dynamic_cast<const LedDevice*>(device);
            leds != nullptr && leds->width() != kMaxWidth) {
            spec.size = static_cast<u32>(leds->width());
        }
        if (const auto* switches = dynamic_cast<const SwitchDevice*>(device);
            switches != nullptr && switches->width() != kMaxWidth) {
            spec.size = static_cast<u32>(switches->width());
        }
        if (const auto* buttons = dynamic_cast<const ButtonDevice*>(device);
            buttons != nullptr && buttons->width() != 1) {
            spec.size = static_cast<u32>(buttons->width());
        }
        // Inputs are the part of a device a person set, so they are what is
        // worth writing back out.
        const std::vector<InputControl> inputs = device->inputs();
        if (!inputs.empty() && inputs.front().value != 0) {
            spec.value = inputs.front().value;
            spec.has_value = true;
        }
        config.devices.push_back(std::move(spec));
    }
    std::sort(config.devices.begin(), config.devices.end(),
              [](const DeviceSpec& a, const DeviceSpec& b) {
                  const Addr aa = a.has_address ? a.address : slot_address(a.slot);
                  const Addr bb = b.has_address ? b.address : slot_address(b.slot);
                  return aa < bb;
              });
    return config;
}

std::size_t first_free_slot(const Bus& bus, std::size_t span) {
    const std::size_t width = std::max<std::size_t>(1, span);
    for (std::size_t slot = 0; slot + width <= kMmioSlots; ++slot) {
        bool free_here = true;
        for (std::size_t index = slot; index < slot + width; ++index) {
            if (bus.device_at_slot(index) != nullptr) free_here = false;
        }
        if (free_here) return slot;
    }
    return 0;  // a full window: the caller gets an overlap and a warning
}

std::string apply_device_config(Bus& bus, const DeviceConfig& config,
                                const std::string& base_directory,
                                std::vector<std::string>* warnings) {
    if (!config.ok()) return config.error;

    // Replace rather than add: a machine described in a file gets exactly what
    // the file says, so a configuration is reproducible.
    bus.detach_all();

    for (const DeviceSpec& spec : config.devices) {
        std::unique_ptr<Device> device;
        if (spec.type == "display") {
            device = std::make_unique<DisplayDevice>(spec.digits);
            device->set_label(spec.name);
        } else if (spec.type == "function") {
            FunctionOp op = FunctionOp::Mul;
            if (!spec.operation.empty() && !parse_function_op(spec.operation, op)) {
                return "unknown operation '" + spec.operation + "'";
            }
            device = std::make_unique<FunctionDevice>(
                op, spec.size == 0 ? kDefaultFunctionWidth : static_cast<int>(spec.size),
                spec.has_latency ? spec.latency : kDefaultFunctionLatency, spec.name);
            device->set_label(spec.name);
        } else if (spec.type == "ram" || spec.type == "rom") {
            device = std::make_unique<RamDevice>(spec.size == 0 ? 256u : spec.size,
                                                 spec.type == "rom" || spec.read_only);
            device->set_label(spec.name);
        } else {
            device = make_device(spec.type, spec.size, spec.name);
        }
        if (device == nullptr) return "unknown device type '" + spec.type + "'";

        // An overlap is worth saying out loud but not worth refusing over: the
        // machine still builds, with the later device winning the addresses
        // they share.
        const Addr base = spec.has_address ? spec.address : slot_address(spec.slot);
        if (warnings != nullptr) {
            for (const Device* other :
                 bus.devices_overlapping_address(base, device->span_bytes())) {
                warnings->push_back("'" + spec.type + "' at " + hex_address(base) +
                                    " overlaps '" + std::string(other->type_name()) + "' at " +
                                    hex_address(other->base_address()) +
                                    "; the later one wins the addresses they share");
            }
        }

        auto* ram = dynamic_cast<RamDevice*>(device.get());
        Device* attached = bus.attach_at(base, std::move(device));
        if (attached == nullptr) {
            return "'" + spec.type + "' at " + hex_address(base) + " could not be attached";
        }

        if (ram != nullptr && !spec.load.empty()) {
            std::vector<Word> words;
            const MemFileResult result =
                read_mem_file(join_path(base_directory, spec.load), words);
            if (!result.ok) return result.error;
            ram->load_words(words);
        }
        if (spec.has_value && !attached->inputs().empty()) {
            attached->set_input(0, spec.value);
        } else if (spec.has_value) {
            // A value on an output device is its initial contents.
            attached->poke(0, spec.value);
        }
    }
    return {};
}

}  // namespace rv::core
