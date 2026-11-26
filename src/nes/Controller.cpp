#include "runamuck/nes/Controller.hpp"

namespace runamuck {

void Controller::setButton(ControllerButton button, bool pressed) noexcept {
    const auto mask = static_cast<std::uint8_t>(
        1u << static_cast<std::uint8_t>(button));
    if (pressed) {
        buttons_ |= mask;
    } else {
        buttons_ &= static_cast<std::uint8_t>(~mask);
    }
}

void Controller::latch() noexcept {
    shiftRegister_ = buttons_;
}

void Controller::writeStrobe(std::uint8_t value) noexcept {
    const bool newStrobe = (value & 1) != 0;
    if (newStrobe || strobe_) {
        latch();
    }
    strobe_ = newStrobe;
}

std::uint8_t Controller::read() noexcept {
    const std::uint8_t bit = strobe_
        ? static_cast<std::uint8_t>(buttons_ & 1)
        : static_cast<std::uint8_t>(shiftRegister_ & 1);
    if (!strobe_) {
        shiftRegister_ = static_cast<std::uint8_t>(
            (shiftRegister_ >> 1) | 0x80);
    }
    return static_cast<std::uint8_t>(0x40 | bit);
}

} // namespace runamuck
