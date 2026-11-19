#include "runamuck/nes/NromMapper.hpp"

#include <stdexcept>

namespace runamuck {

NromMapper::NromMapper(
    std::size_t prgRomSize,
    NametableMirroring mirroring)
    : prgRomSize_(prgRomSize),
      mirroring_(mirroring) {
    if (prgRomSize_ != 0x4000 && prgRomSize_ != 0x8000) {
        throw std::invalid_argument("NROM PRG ROM must be 16 KB or 32 KB.");
    }
}

MapperMapping NromMapper::mapCpuRead(std::uint16_t address) {
    if (address >= 0x6000 && address < 0x8000) {
        return {
            MemoryRegion::prgRam,
            static_cast<std::size_t>(address - 0x6000)
        };
    }

    if (address >= 0x8000) {
        const auto offset = static_cast<std::size_t>(address - 0x8000);
        return {MemoryRegion::prgRom, offset % prgRomSize_};
    }

    return {};
}

MapperMapping NromMapper::mapCpuWrite(
    std::uint16_t address,
    std::uint8_t) {
    if (address >= 0x6000 && address < 0x8000) {
        return {
            MemoryRegion::prgRam,
            static_cast<std::size_t>(address - 0x6000)
        };
    }

    return {};
}

MapperMapping NromMapper::mapPpuRead(std::uint16_t address) {
    if (address < 0x2000) {
        return {MemoryRegion::chr, address};
    }

    return {};
}

MapperMapping NromMapper::mapPpuWrite(std::uint16_t address, std::uint8_t) {
    if (address < 0x2000) {
        return {MemoryRegion::chr, address};
    }

    return {};
}

NametableMirroring NromMapper::mirroring() const noexcept {
    return mirroring_;
}

void NromMapper::reset() {
}

} // namespace runamuck