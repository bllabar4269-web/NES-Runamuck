#include "runamuck/nes/Mmc1Mapper.hpp"

#include <stdexcept>
#include <ostream>

namespace runamuck {

Mmc1Mapper::Mmc1Mapper(
    std::size_t prgRomSize,
    std::size_t chrRomSize,
    NametableMirroring mirroring)
    : prgRomSize_(prgRomSize),
      chrRomSize_(chrRomSize),
      mirroring_(mirroring) {
    if (prgRomSize_ < 0x8000 || (prgRomSize_ % 0x4000) != 0) {
        throw std::invalid_argument("MMC1 PRG ROM must contain 16 KB banks.");
    }
}

void Mmc1Mapper::reset() {
    control_ = 0x0c;
    chrBank0_ = 0;
    chrBank1_ = 0;
    prgBank_ = 0;
    shiftRegister_ = 0;
    shiftCount_ = 0;
    mirroring_ = NametableMirroring::horizontal;
}

void Mmc1Mapper::writeRegister(std::uint16_t address, std::uint8_t value) {
    const std::uint8_t oldShiftRegister = shiftRegister_;
    const std::uint8_t oldShiftCount = shiftCount_;
    if (value & 0x80) {
        shiftRegister_ = 0;
        shiftCount_ = 0;
        control_ |= 0x0c;
        if (traceStream_) {
            *traceStream_ << "MMC1 reset write addr=$" << std::hex << address
                          << " data=$" << static_cast<unsigned>(value)
                          << " control=$" << static_cast<unsigned>(control_)
                          << std::dec << '\n';
        }
        return;
    }

    shiftRegister_ |= (value & 1) << shiftCount_;
    ++shiftCount_;
    if (traceStream_) {
        *traceStream_ << "MMC1 serial addr=$" << std::hex << address
                      << " data=$" << static_cast<unsigned>(value)
                      << " shift=" << static_cast<unsigned>(oldShiftCount)
                      << "->" << static_cast<unsigned>(shiftCount_)
                      << " value=$" << static_cast<unsigned>(oldShiftRegister)
                      << "->" << static_cast<unsigned>(shiftRegister_)
                      << std::dec << '\n';
    }
    if (shiftCount_ != 5) {
        return;
    }

    const std::uint8_t registerValue = shiftRegister_;
    shiftRegister_ = 0;
    shiftCount_ = 0;

    if (address >= 0x8000 && address < 0xa000) {
        control_ = registerValue;
        switch (control_ & 0x03) {
        case 0:
            mirroring_ = NametableMirroring::singleScreenLower;
            break;
        case 1:
            mirroring_ = NametableMirroring::singleScreenUpper;
            break;
        case 2:
            mirroring_ = NametableMirroring::vertical;
            break;
        default:
            mirroring_ = NametableMirroring::horizontal;
            break;
        }
    } else if (address >= 0xa000 && address < 0xc000) {
        chrBank0_ = registerValue;
    } else if (address >= 0xc000 && address < 0xe000) {
        chrBank1_ = registerValue;
    } else {
        prgBank_ = registerValue & 0x0f;
    }

    if (traceStream_) {
        *traceStream_ << "MMC1 commit reg=$" << std::hex << (address & 0xe000u)
                      << " value=$" << static_cast<unsigned>(registerValue)
                      << " control=$" << static_cast<unsigned>(control_)
                      << " chr0=$" << static_cast<unsigned>(chrBank0_)
                      << " chr1=$" << static_cast<unsigned>(chrBank1_)
                      << " prg=$" << static_cast<unsigned>(prgBank_)
                      << std::dec << '\n';
    }
}

MapperMapping Mmc1Mapper::mapCpuRead(std::uint16_t address) {
    if (address >= 0x6000 && address < 0x8000) {
        return {MemoryRegion::prgRam,
                static_cast<std::size_t>(address - 0x6000)};
    }
    if (address < 0x8000) {
        return {};
    }

    const std::size_t bankCount = prgRomSize_ / 0x4000;
    const std::size_t lastBank = bankCount - 1;
    const std::uint8_t prgMode = (control_ >> 2) & 3;
    std::size_t bank = 0;

    if (prgMode == 0 || prgMode == 1) {
        bank = (prgBank_ & 0x0e) % bankCount;
        if (address >= 0xc000) {
            bank += 1;
        }
    } else if (prgMode == 2) {
        bank = address < 0xc000 ? 0 : prgBank_ % bankCount;
    } else {
        bank = address < 0xc000 ? prgBank_ % bankCount : lastBank;
    }

    const std::size_t offset =
        bank * 0x4000 + (address & 0x3fff);
    return {MemoryRegion::prgRom, offset};
}

MapperMapping Mmc1Mapper::mapCpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address >= 0x6000 && address < 0x8000) {
        return {MemoryRegion::prgRam,
                static_cast<std::size_t>(address - 0x6000)};
    }
    if (address >= 0x8000) {
        writeRegister(address, value);
    }
    return {};
}

MapperMapping Mmc1Mapper::mapPpuRead(std::uint16_t address) {
    if (address >= 0x2000) {
        return {};
    }
    if (chrRomSize_ == 0) {
        return {MemoryRegion::chr, address};
    }

    if ((control_ & 0x10) == 0) {
        const std::size_t bank =
            (chrBank0_ & 0x1e) % (chrRomSize_ / 0x1000);
        return {MemoryRegion::chr,
                bank * 0x1000 + address};
    }

    if (address < 0x1000) {
        const std::size_t bank = chrBank0_ % (chrRomSize_ / 0x1000);
        return {MemoryRegion::chr,
                bank * 0x1000 + (address & 0x0fff)};
    }

    const std::size_t bank = chrBank1_ % (chrRomSize_ / 0x1000);
    return {MemoryRegion::chr,
            bank * 0x1000 + (address & 0x0fff)};
}

MapperMapping Mmc1Mapper::mapPpuWrite(std::uint16_t address, std::uint8_t) {
    if (chrRomSize_ == 0 && address < 0x2000) {
        return {MemoryRegion::chr, address};
    }
    return mapPpuRead(address);
}

NametableMirroring Mmc1Mapper::mirroring() const noexcept {
    return mirroring_;
}

} // namespace runamuck
