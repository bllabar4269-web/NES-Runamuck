#include "runamuck/nes/PpuBus.hpp"
#include "runamuck/nes/Cartridge.hpp"

namespace runamuck {

void PpuBus::setCartridge(std::shared_ptr<Cartridge> cartridge) {
    cartridge_ = std::move(cartridge);
}

std::uint16_t PpuBus::mirrorAddress(std::uint16_t address) const {
    address &= 0x3fff;

    if (address < 0x2000) {
        return address;
    }
    if (address >= 0x3000 && address < 0x3f00) {
        address -= 0x1000;
    }
    if (address >= 0x3f00) {
        address &= 0x001f;
        if (address == 0x10 || address == 0x14 ||
            address == 0x18 || address == 0x1c) {
            address -= 0x10;
        }
        return 0x3f00 | address;
    }

    const std::uint16_t offset = address - 0x2000;
    const auto mirroring = cartridge_ ? cartridge_->mirroring()
                                      : NametableMirroring::horizontal;

    if (mirroring == NametableMirroring::vertical) {
        return 0x2000 + (offset & 0x07ff);
    }
    if (mirroring == NametableMirroring::horizontal) {
        const std::uint16_t table = (offset >> 10) & 0x3;
        const std::uint16_t within = offset & 0x03ff;
        if (table == 0 || table == 1) {
            return 0x2000 + within;
        }
        return 0x2000 + 0x400 + within;
    }
    if (mirroring == NametableMirroring::singleScreenLower) {
        return 0x2000 + (offset & 0x03ff);
    }
    if (mirroring == NametableMirroring::singleScreenUpper) {
        return 0x2400 + (offset & 0x03ff);
    }

    return address;
}

std::uint8_t PpuBus::read(std::uint16_t address) {
    address = mirrorAddress(address);

    if (address < 0x2000) {
        if (cartridge_) {
            if (const auto value = cartridge_->ppuRead(address)) {
                return *value;
            }
        }
        return 0;
    }
    if (address >= 0x2000 && address < 0x3f00) {
        const std::uint16_t offset = address - 0x2000;
        if (offset < nametableRam_.size()) {
            return nametableRam_[offset];
        }
        return 0;
    }
    if (address >= 0x3f00) {
        return paletteRam_[address - 0x3f00];
    }
    return 0;
}

std::uint8_t PpuBus::peek(std::uint16_t address) {
    address = mirrorAddress(address);

    if (address < 0x2000) {
        if (cartridge_) {
            if (const auto value = cartridge_->ppuPeek(address)) {
                return *value;
            }
        }
        return 0;
    }
    if (address < 0x3f00) {
        const std::uint16_t offset = address - 0x2000;
        return offset < nametableRam_.size() ? nametableRam_[offset] : 0;
    }
    return paletteRam_[address - 0x3f00];
}

std::optional<std::size_t> PpuBus::peekChrBank(std::uint16_t address) {
    address &= 0x1fffu;
    return cartridge_ ? cartridge_->ppuPeekChrBank(address) : std::nullopt;
}

void PpuBus::write(std::uint16_t address, std::uint8_t value) {
    address = mirrorAddress(address);

    if (address < 0x2000) {
        if (cartridge_) {
            cartridge_->ppuWrite(address, value);
        }
    } else if (address >= 0x2000 && address < 0x3f00) {
        const std::uint16_t offset = address - 0x2000;
        if (offset < nametableRam_.size()) {
            nametableRam_[offset] = value;
        }
    } else if (address >= 0x3f00) {
        paletteRam_[address - 0x3f00] = value;
    }
}

} // namespace runamuck
