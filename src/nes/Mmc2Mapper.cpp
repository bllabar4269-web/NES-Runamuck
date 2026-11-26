#include "runamuck/nes/Mmc2Mapper.hpp"

#include <stdexcept>

namespace runamuck {

Mmc2Mapper::Mmc2Mapper(
    std::size_t prgRomSize,
    std::size_t chrRomSize,
    NametableMirroring mirroring)
    : prgRomSize_(prgRomSize),
      chrRomSize_(chrRomSize),
      initialMirroring_(mirroring),
      mirroring_(mirroring) {
    if (prgRomSize_ < 0x8000 || prgRomSize_ > 0x20000 ||
        (prgRomSize_ % 0x2000) != 0) {
        throw std::invalid_argument(
            "MMC2 PRG ROM must contain 32-128 KB in 8 KB banks.");
    }
    if (chrRomSize_ < 0x2000 || chrRomSize_ > 0x20000 ||
        (chrRomSize_ % 0x1000) != 0) {
        throw std::invalid_argument(
            "MMC2 requires 8-128 KB of CHR ROM in 4 KB banks.");
    }
}

MapperMapping Mmc2Mapper::mapCpuRead(std::uint16_t address) {
    if (address >= 0x6000 && address < 0x8000) {
        return {MemoryRegion::prgRam,
                static_cast<std::size_t>(address - 0x6000)};
    }
    if (address < 0x8000) {
        return {};
    }

    const std::size_t bankCount = prgRomSize_ / 0x2000;
    const std::size_t bank = address < 0xa000
        ? prgBank_ % bankCount
        : bankCount - 3 + ((address - 0xa000) / 0x2000);
    return {
        MemoryRegion::prgRom,
        bank * 0x2000 + (address & 0x1fff)
    };
}

MapperMapping Mmc2Mapper::mapCpuWrite(
    std::uint16_t address,
    std::uint8_t value) {
    if (address >= 0x6000 && address < 0x8000) {
        return {MemoryRegion::prgRam,
                static_cast<std::size_t>(address - 0x6000)};
    }
    if (address >= 0x8000) {
        writeRegister(address, value);
    }
    return {};
}

MapperMapping Mmc2Mapper::mapPpuRead(std::uint16_t address) {
    if (address >= 0x2000) {
        return {};
    }

    const std::size_t bank = selectedChrBank(address);
    const MapperMapping mapping{
        MemoryRegion::chr,
        bank * 0x1000 + (address & 0x0fff)
    };
    updateChrLatch(address);
    return mapping;
}

MapperMapping Mmc2Mapper::mapPpuPeek(std::uint16_t address) {
    if (address >= 0x2000) {
        return {};
    }
    const std::size_t bank = selectedChrBank(address);
    return {
        MemoryRegion::chr,
        bank * 0x1000 + (address & 0x0fff)
    };
}

MapperMapping Mmc2Mapper::mapPpuWrite(
    std::uint16_t address,
    std::uint8_t) {
    if (address >= 0x2000) {
        return {};
    }
    return {
        MemoryRegion::chr,
        selectedChrBank(address) * 0x1000 + (address & 0x0fff)
    };
}

NametableMirroring Mmc2Mapper::mirroring() const noexcept {
    return mirroring_;
}

void Mmc2Mapper::reset() {
    prgBank_ = 0;
    chrBank0Fd_ = chrBank0Fe_ = 0;
    chrBank1Fd_ = chrBank1Fe_ = 0;
    chrLatch0_ = chrLatch1_ = 0xfe;
    mirroring_ = initialMirroring_;
}

std::size_t Mmc2Mapper::selectedChrBank(
    std::uint16_t address) const noexcept {
    if (address < 0x1000) {
        return (chrLatch0_ == 0xfd ? chrBank0Fd_ : chrBank0Fe_) %
               (chrRomSize_ / 0x1000);
    }
    return (chrLatch1_ == 0xfd ? chrBank1Fd_ : chrBank1Fe_) %
           (chrRomSize_ / 0x1000);
}

void Mmc2Mapper::updateChrLatch(std::uint16_t address) noexcept {
    if (address >= 0x0fd8 && address <= 0x0fdf) {
        chrLatch0_ = 0xfd;
    } else if (address >= 0x0fe8 && address <= 0x0fef) {
        chrLatch0_ = 0xfe;
    } else if (address >= 0x1fd8 && address <= 0x1fdf) {
        chrLatch1_ = 0xfd;
    } else if (address >= 0x1fe8 && address <= 0x1fef) {
        chrLatch1_ = 0xfe;
    }
}

void Mmc2Mapper::writeRegister(
    std::uint16_t address,
    std::uint8_t value) noexcept {
    switch (address & 0xf000) {
    case 0xa000:
        prgBank_ = value & 0x0f;
        break;
    case 0xb000:
        chrBank0Fd_ = value & 0x1f;
        break;
    case 0xc000:
        chrBank0Fe_ = value & 0x1f;
        break;
    case 0xd000:
        chrBank1Fd_ = value & 0x1f;
        break;
    case 0xe000:
        chrBank1Fe_ = value & 0x1f;
        break;
    case 0xf000:
        mirroring_ = (value & 1)
            ? NametableMirroring::horizontal
            : NametableMirroring::vertical;
        break;
    default:
        break;
    }
}

} // namespace runamuck
