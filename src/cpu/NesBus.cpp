#include "runamuck/nes/NesBus.hpp"
#include "runamuck/nes/Cartridge.hpp"
#include "runamuck/nes/Ppu.hpp"

#include <stdexcept>
#include <utility>

namespace runamuck {

void NesBus::setCartridge(Cartridge cartridge) {
    cartridge_ = std::make_shared<Cartridge>(std::move(cartridge));
    if (ppu_) {
        ppu_->setCartridge(cartridge_);
    }
}

void NesBus::setCartridge(std::shared_ptr<Cartridge> cartridge) {
    cartridge_ = std::move(cartridge);
    if (ppu_) {
        ppu_->setCartridge(cartridge_);
    }
}

void NesBus::insertCartridge(Cartridge cartridge) {
    setCartridge(std::move(cartridge));
}

void NesBus::removeCartridge() noexcept {
    cartridge_.reset();
}

bool NesBus::hasCartridge() const noexcept {
    return static_cast<bool>(cartridge_);
}

void NesBus::setPpu(std::shared_ptr<Ppu> ppu) {
    ppu_ = std::move(ppu);
    if (ppu_) {
        ppu_->setCartridge(cartridge_);
        ppu_->setDmaCallback([this](std::uint16_t address) {
            return read(address);
        });
    }
}

void NesBus::removePpu() noexcept {
    ppu_.reset();
}

void NesBus::setApu(std::shared_ptr<Apu> apu) {
    apu_ = std::move(apu);
}

void NesBus::removeApu() noexcept {
    apu_.reset();
}

void NesBus::setControllerButton(
    std::uint8_t port,
    ControllerButton button,
    bool pressed) {
    if (port >= controllers_.size()) {
        throw std::out_of_range("NES controller port must be 0 or 1.");
    }
    controllers_[port].setButton(button, pressed);
}

std::uint8_t NesBus::read(std::uint16_t address) {
    if (address < 0x2000) {
        openBus_ = ram_[address & 0x07ff];
    } else if (address >= 0x2000 && address < 0x4000) {
        openBus_ = ppu_ ? ppu_->readRegister(address) : openBus_;
    } else if (address == 0x4015) {
        openBus_ = apu_ ? apu_->readStatus() : openBus_;
    } else if (address == 0x4016 || address == 0x4017) {
        openBus_ = controllers_[address - 0x4016].read();
    } else if (cartridge_) {
        if (const auto value = cartridge_->cpuRead(address)) {
            openBus_ = *value;
        }
    }

    return openBus_;
}

void NesBus::write(std::uint16_t address, std::uint8_t value) {
    openBus_ = value;

    if (address < 0x2000) {
        ram_[address & 0x07ff] = value;
    } else if (address >= 0x2000 && address < 0x4000) {
        if (ppu_) {
            ppu_->writeRegister(address, value);
        }
    } else if (address == 0x4014) {
        if (ppu_) {
            ppu_->writeRegister(address, value);
        }
    } else if (address == 0x4016) {
        for (auto& controller : controllers_) {
            controller.writeStrobe(value);
        }
    } else if ((address >= 0x4000 && address <= 0x4013) ||
               address == 0x4015 ||
               address == 0x4017) {
        if (apu_) {
            apu_->writeRegister(address, value);
        }
    } else if (cartridge_) {
        cartridge_->cpuWrite(address, value);
    }
}

} // namespace runamuck