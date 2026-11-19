#include "runamuck/nes/NesBus.hpp"

namespace runamuck {

std::uint8_t NesBus::read(std::uint16_t address) {
    if (address < 0x2000) {
        openBus_ = ram_[address & 0x07ff];
    }

    return openBus_;
}

void NesBus::write(std::uint16_t address, std::uint8_t value) {
    openBus_ = value;

    if (address < 0x2000) {
        ram_[address & 0x07ff] = value;
    }
}

} // namespace runamuck