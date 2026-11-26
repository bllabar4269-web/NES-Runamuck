#pragma once

#include "runamuck/cpu/Bus.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>

namespace runamuck {

class Cpu6502 {
public:
    static constexpr std::uint8_t carryFlag = 0x01;
    static constexpr std::uint8_t zeroFlag = 0x02;
    static constexpr std::uint8_t interruptDisableFlag = 0x04;
    static constexpr std::uint8_t decimalFlag = 0x08;
    static constexpr std::uint8_t breakFlag = 0x10;
    static constexpr std::uint8_t unusedFlag = 0x20;
    static constexpr std::uint8_t overflowFlag = 0x40;
    static constexpr std::uint8_t negativeFlag = 0x80;

    explicit Cpu6502(Bus& bus);

    void reset();
    std::uint8_t step();
    void nmi();
    void irq();
    void setTraceStream(std::ostream* stream) noexcept { traceStream_ = stream; }

    std::uint16_t pc = 0;
    std::uint8_t sp = 0xfd;
    std::uint8_t a = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t status = unusedFlag | interruptDisableFlag;

private:
    Bus& bus_;
    bool nmiPending_ = false;
    bool irqPending_ = false;
    std::ostream* traceStream_ = nullptr;
    std::uint16_t currentInstructionAddress_ = 0;
    std::uint8_t currentOpcode_ = 0;
    mutable std::uint32_t statusReadCount_ = 0;
    std::array<std::uint8_t, 64> lastShadowOamY_{};
    std::array<bool, 64> hasShadowOamY_{};

    std::uint8_t read(std::uint16_t address) const;
    void write(std::uint16_t address, std::uint8_t value);
    std::uint8_t fetch();
    std::uint16_t fetchWord();

    void push(std::uint8_t value);
    std::uint8_t pop();
    void setZN(std::uint8_t value);

    void adc(std::uint8_t value);
    void sbc(std::uint8_t value);
    void andWith(std::uint8_t value);
    void xorWith(std::uint8_t value);
    void orWith(std::uint8_t value);
    void compare(std::uint8_t left, std::uint8_t value);
    std::uint8_t asl(std::uint8_t value);
    std::uint8_t lsr(std::uint8_t value);
    std::uint8_t rol(std::uint8_t value);
    std::uint8_t ror(std::uint8_t value);
};

} // namespace runamuck
