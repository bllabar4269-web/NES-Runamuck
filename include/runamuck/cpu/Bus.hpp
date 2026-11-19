#pragma once

#include <cstdint>

namespace runamuck {

class Bus {
public:
    virtual ~Bus() = default;
    virtual std::uint8_t read(std::uint16_t address) = 0;
    virtual void write(std::uint16_t address, std::uint8_t value) = 0;
};

} // namespace runamuck
