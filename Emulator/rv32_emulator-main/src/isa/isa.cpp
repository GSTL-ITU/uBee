#include "isa.hpp"

#include <cassert>
#include <string>
#include <unordered_map>

namespace rv::isa {
namespace {

const std::unordered_map<std::string_view, InstrId>& mnemonic_index() {
    static const std::unordered_map<std::string_view, InstrId> index = [] {
        std::unordered_map<std::string_view, InstrId> map;
        map.reserve(kInstrCount * 2);
        for (std::size_t i = 0; i < kInstrCount; ++i) {
            map.emplace(kInstrTable[i].mnemonic, static_cast<InstrId>(i));
        }
        return map;
    }();
    return index;
}

}  // namespace

const InstrDesc& describe(InstrId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kInstrCount && "describe() called with kInvalidInstr");
    return kInstrTable[index];
}

std::string_view mnemonic_of(InstrId id) {
    if (static_cast<std::size_t>(id) >= kInstrCount) return "<invalid>";
    return kInstrTable[static_cast<std::size_t>(id)].mnemonic;
}

InstrId lookup_mnemonic(std::string_view name) {
    const auto& index = mnemonic_index();
    const auto it = index.find(name);
    return it == index.end() ? kInvalidInstr : it->second;
}

std::string_view format_name(InstrFormat format) {
    switch (format) {
        case InstrFormat::R: return "R-type";
        case InstrFormat::I: return "I-type";
        case InstrFormat::S: return "S-type";
        case InstrFormat::B: return "B-type";
        case InstrFormat::U: return "U-type";
        case InstrFormat::J: return "J-type";
        case InstrFormat::CR: return "CR-type";
        case InstrFormat::CI: return "CI-type";
        case InstrFormat::CSS: return "CSS-type";
        case InstrFormat::CIW: return "CIW-type";
        case InstrFormat::CL: return "CL-type";
        case InstrFormat::CS: return "CS-type";
        case InstrFormat::CA: return "CA-type";
        case InstrFormat::CB: return "CB-type";
        case InstrFormat::CJ: return "CJ-type";
    }
    return "?";
}

std::string_view group_name(InstrGroup group) {
    switch (group) {
        case InstrGroup::ArithR: return "arithmetic";
        case InstrGroup::ArithI: return "arithmetic (immediate)";
        case InstrGroup::Shift: return "shift";
        case InstrGroup::Load: return "load";
        case InstrGroup::Store: return "store";
        case InstrGroup::Branch: return "branch";
        case InstrGroup::Jump: return "jump";
        case InstrGroup::Upper: return "upper immediate";
        case InstrGroup::MulDiv: return "multiply/divide";
        case InstrGroup::Csr: return "CSR";
        case InstrGroup::System: return "system";
        case InstrGroup::Fence: return "fence";
    }
    return "?";
}

}  // namespace rv::isa
