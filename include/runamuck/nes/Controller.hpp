#pragma once

#include <cstdint>

namespace runamuck {

enum class ControllerButton : std::uint8_t {
    a,
    b,
    select,
    start,
    up,
    down,
    left,
    right
};

class Controller {
public:
    void setButton(ControllerButton button, bool pressed) noexcept;
    void writeStrobe(std::uint8_t value) noexcept;
    std::uint8_t read() noexcept;

private:
    std::uint8_t buttons_ = 0;
    std::uint8_t shiftRegister_ = 0;
    bool strobe_ = false;

    void latch() noexcept;
};

} // namespace runamuck
