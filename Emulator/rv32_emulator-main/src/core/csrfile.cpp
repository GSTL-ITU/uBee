#include "core/csrfile.hpp"

namespace rv::core {

void CsrFile::reset() {
    for (std::size_t i = 0; i < kCsrCount; ++i) values_[i] = isa::kCsrTable[i].reset;
}

u32 CsrFile::read(CsrAddr addr) const {
    const std::size_t index = isa::csr_index(addr);
    return index == kCsrCount ? 0 : values_[index];
}

void CsrFile::write(CsrAddr addr, u32 value) {
    const std::size_t index = isa::csr_index(addr);
    if (index == kCsrCount) return;
    const u32 mask = isa::kCsrTable[index].write_mask;
    values_[index] = (values_[index] & ~mask) | (value & mask);
}

void CsrFile::raw_write(CsrAddr addr, u32 value) {
    const std::size_t index = isa::csr_index(addr);
    if (index != kCsrCount) values_[index] = value;
}

void CsrFile::set_pending_interrupts(u32 mip) {
    values_[static_cast<std::size_t>(CsrId::MIP)] = mip;
}

void CsrFile::set_counters(u64 cycle, u64 instret) {
    values_[static_cast<std::size_t>(CsrId::MCYCLE)] = static_cast<u32>(cycle);
    values_[static_cast<std::size_t>(CsrId::MCYCLEH)] = static_cast<u32>(cycle >> 32);
    values_[static_cast<std::size_t>(CsrId::MINSTRET)] = static_cast<u32>(instret);
    values_[static_cast<std::size_t>(CsrId::MINSTRETH)] = static_cast<u32>(instret >> 32);
    values_[static_cast<std::size_t>(CsrId::CYCLE)] = static_cast<u32>(cycle);
    values_[static_cast<std::size_t>(CsrId::CYCLEH)] = static_cast<u32>(cycle >> 32);
    // No independent wall clock: `time` mirrors `cycle`, which is the honest
    // thing to report for a machine with no real timer.
    values_[static_cast<std::size_t>(CsrId::TIME)] = static_cast<u32>(cycle);
    values_[static_cast<std::size_t>(CsrId::TIMEH)] = static_cast<u32>(cycle >> 32);
    values_[static_cast<std::size_t>(CsrId::INSTRET)] = static_cast<u32>(instret);
    values_[static_cast<std::size_t>(CsrId::INSTRETH)] = static_cast<u32>(instret >> 32);
}

u64 CsrFile::machine_cycle() const {
    return (static_cast<u64>(values_[static_cast<std::size_t>(CsrId::MCYCLEH)]) << 32) |
           values_[static_cast<std::size_t>(CsrId::MCYCLE)];
}

u64 CsrFile::machine_instret() const {
    return (static_cast<u64>(values_[static_cast<std::size_t>(CsrId::MINSTRETH)]) << 32) |
           values_[static_cast<std::size_t>(CsrId::MINSTRET)];
}

}  // namespace rv::core
