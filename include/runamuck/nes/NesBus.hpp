#pragma once

#include "runamuck/cpu/Bus.hpp"
#include "runamuck/nes/Apu.hpp"
#include "runamuck/nes/Controller.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace runamuck {

class Cartridge;
class Ppu;

class NesBus final : public Bus {
public:
    void setCartridge(Cartridge cartridge);
    void setCartridge(std::shared_ptr<Cartridge> cartridge);
    void insertCartridge(Cartridge cartridge);
    void removeCartridge() noexcept;
    bool hasCartridge() const noexcept;
    void setPpu(std::shared_ptr<Ppu> ppu);
    void removePpu() noexcept;
    void setApu(std::shared_ptr<Apu> apu);
    void removeApu() noexcept;
    void setControllerButton(
        std::uint8_t port,
        ControllerButton button,
        bool pressed);

    std::uint8_t read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

private:
    std::array<std::uint8_t, 0x0800> ram_{};
    std::shared_ptr<Cartridge> cartridge_;
    std::shared_ptr<Ppu> ppu_;
    std::shared_ptr<Apu> apu_;
    std::array<Controller, 2> controllers_{};
    std::uint8_t openBus_ = 0;
};

} // namespace runamuck