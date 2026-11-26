#pragma once

#include "runamuck/nes/Mapper.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace runamuck {

class Cartridge;

class PpuBus {
public:
    void setCartridge(std::shared_ptr<Cartridge> cartridge);

    std::uint8_t read(std::uint16_t address);
    std::uint8_t peek(std::uint16_t address);
    std::optional<std::size_t> peekChrBank(std::uint16_t address);
    void write(std::uint16_t address, std::uint8_t value);

private:
    std::uint16_t mirrorAddress(std::uint16_t address) const;

    std::array<std::uint8_t, 0x1000> nametableRam_{};
    std::array<std::uint8_t, 0x20> paletteRam_{};
    std::shared_ptr<Cartridge> cartridge_;
};

} // namespace runamuck
