#pragma once

#include "runamuck/cpu/Bus.hpp"

#include <array>
#include <cstdint>

namespace runamuck {

class NesBus final : public Bus {
public:
    std::uint8_t read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

private:
    std::array<std::uint8_t, 0x0800> ram_{};
    std::uint8_t openBus_ = 0;
};

} // namespace runamuck
