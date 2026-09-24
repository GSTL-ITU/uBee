// Describing a machine's peripherals in a file.
//
// The catalogue maps a type name to a constructor, so both the configuration
// file and the "add device" list in the interface work from one list rather
// than each hard-coding what exists.
//
// The file format is a deliberate subset of TOML -- arrays of tables with
// scalar keys -- parsed here in about a hundred lines rather than taken as a
// dependency. What is supported is documented in docs/memory-map.md; anything
// else is reported with a line number rather than ignored.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/bus.hpp"
#include "core/device.hpp"
#include "core/hart.hpp"

namespace rv::core {

/// What a type's `size` counts, which decides what the interface asks for and
/// in what units. A device that is not sized at all is `None`.
enum class SizeUnit : u8 {
    None,
    /// Bits wired up: how many LEDs the board has, how many switches.
    Bits,
    /// Bytes of storage, for a block.
    Bytes,
    /// 32-bit registers, for a peripheral counted in registers.
    Words,
    /// Bits of one operand, for a peripheral that computes. Bits again, but a
    /// separate list: an IP core is ordered by how wide it multiplies, the
    /// widths on offer stop where the result stops fitting in a register, and
    /// the one to open on is the one the course hands out rather than the
    /// widest.
    OperandBits,
};

/// The sizes worth offering for each unit. Powers of two, for the same reason
/// the memory list is: this is hardware, and a width or a depth that is not one
/// wastes what it is built from.
const std::vector<u32>& common_sizes(SizeUnit unit);

/// The one to offer first: what a person means by a device of this kind when
/// they do not say. Named rather than an index into the list above, so that
/// adding a size does not silently move it.
u32 default_size(SizeUnit unit);

/// "16 KB - 4096 x 32-bit". Both halves matter: the bytes are what the machine
/// has, the depth is what a block memory is ordered by.
std::string memory_size_label(u32 bytes);
std::string_view unit_name(SizeUnit unit);

/// One entry in the catalogue of device types that can be created by name.
struct DeviceType {
    std::string_view name;
    std::string_view description;
    /// What `size` counts for this type, and whether it has one at all.
    SizeUnit unit = SizeUnit::None;
    /// True if the device carries a name of its own, which the interface asks
    /// for and the file records.
    bool takes_name = false;
    std::unique_ptr<Device> (*create)(u32 size, const std::string& name);
};

/// Every device type that can be named in a file or added from the interface.
const std::vector<DeviceType>& device_catalogue();
const DeviceType* find_device_type(std::string_view name);
std::unique_ptr<Device> make_device(std::string_view type, u32 size = 0,
                                    const std::string& name = {});

/// One device as a file describes it.
struct DeviceSpec {
    std::string type;
    std::size_t slot = 0;
    /// Absolute base when `has_address` is set. Otherwise the slot maps into
    /// the legacy `0xFFFF0000` window.
    Addr address = 0;
    bool has_address = false;
    /// Bytes, for ram and rom.
    u32 size = 0;
    /// Initial value for an input device, or the contents of a display.
    u32 value = 0;
    bool has_value = false;
    /// A .mem image to preload a ram or rom block from, relative to the
    /// configuration file.
    std::string load;
    bool read_only = false;
    /// Digits, for a display.
    int digits = 4;
    /// What a custom peripheral calls itself.
    std::string name;
    /// Which behaviour a function device has. Empty means the default.
    std::string operation;
    /// Cycles a function device reports busy for. `has_latency` distinguishes
    /// "the file asked for none" from "the file did not say", and only the
    /// second takes the default.
    u32 latency = 0;
    bool has_latency = false;
};

struct DeviceConfig {
    std::vector<DeviceSpec> devices;
    /// Bytes of instruction and data memory. Zero means "leave as it is",
    /// which is what a file that only describes peripherals says.
    u32 imem_size = 0;
    u32 dmem_size = 0;
    /// Absolute IMEM/DMEM bases. Absent keys leave the current bases alone
    /// (default machine keeps both at 0).
    Addr imem_base = 0;
    Addr dmem_base = 0;
    bool has_imem_base = false;
    bool has_dmem_base = false;
    /// Reset / entry PC when a program has no `_start`.
    Addr reset = 0;
    bool has_reset = false;
    std::string error;   // empty when the file parsed
    bool ok() const { return error.empty(); }
};

/// The memory sizes a machine may be given. Both spaces are word-addressed and
/// exported a word per line, so a size that is not a multiple of four would
/// describe a .mem file that cannot be written.
inline constexpr u32 kMinMemorySize = 256;
inline constexpr u32 kMaxMemorySize = 1u << 20;

/// The sizes offered as a list, rather than as a number to type. These are the
/// depths a block memory actually comes in: a BRAM is configured by how many
/// words deep it is, and a depth that is not a power of two wastes a whole
/// block. Anything else is still accepted from a file -- the list is what is
/// worth offering, not what is allowed.
inline constexpr u32 kCommonMemorySizes[] = {
    1u << 10, 1u << 11, 1u << 12, 1u << 13, 1u << 14, 1u << 15, 1u << 16,
};

/// The directory part of a path, which is what `load = "..."` entries are
/// resolved against -- a configuration should refer to its images relative to
/// itself, so a directory can be moved or shared whole.
std::string directory_of(const std::string& path);

/// Parse a configuration. `source_name` appears in error messages.
DeviceConfig parse_device_config(const std::string& text, const std::string& source_name);
DeviceConfig read_device_config(const std::string& path);

/// Render a configuration back out, so the interface can save what a person
/// assembled by hand.
std::string write_device_config(const DeviceConfig& config);

/// Describe the devices currently on a bus, for saving.
DeviceConfig describe(const Bus& bus);
/// The same, with the memory sizes -- a whole machine rather than its
/// peripherals.
DeviceConfig describe(const Hart& hart);

/// Replace everything on the bus with what the configuration says. Returns an
/// error message, or an empty string. `base_directory` is where `load = "..."`
/// paths are resolved from. Overlapping addresses are not errors -- they are
/// collected into `warnings`, if one is passed, and the machine is built
/// anyway with the later device winning the addresses they share.
std::string apply_device_config(Bus& bus, const DeviceConfig& config,
                                const std::string& base_directory = {},
                                std::vector<std::string>* warnings = nullptr);

/// The address a slot starts at, and the slot an address falls in -- the
/// interface talks in addresses because that is what a program uses, while the
/// bus is organised in slots.
constexpr std::size_t slot_of_address(Addr addr) {
    return addr < kMmioBase ? 0 : (addr - kMmioBase) / kSlotSize;
}

/// The first run of `span` free slots, so adding a device does not open with a
/// conflict the person then has to resolve.
std::size_t first_free_slot(const Bus& bus, std::size_t span);

/// Apply a whole machine: the memory sizes as well as the peripherals. Sizes
/// left at zero are kept as they are.
std::string apply_machine_config(Hart& hart, const DeviceConfig& config,
                                 const std::string& base_directory = {},
                                 std::vector<std::string>* warnings = nullptr);

}  // namespace rv::core
