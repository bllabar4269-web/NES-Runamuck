#include "runamuck/nes/Apu.hpp"

#include "runamuck/cpu/Cpu6502.hpp"
#include "runamuck/nes/Apu.hpp"
#include "runamuck/nes/Cartridge.hpp"
#include "runamuck/nes/NesBus.hpp"
#include "runamuck/nes/Ppu.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace runamuck {

namespace {

constexpr std::array<std::uint8_t, 32> kLengthTable = {
    10, 254, 20,  2, 40,  4, 80,  6,
    160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

constexpr std::array<std::array<std::uint8_t, 8>, 4> kDutyTable = {{
    {{0, 1, 0, 0, 0, 0, 0, 0}}, // 12.5%
    {{0, 1, 1, 0, 0, 0, 0, 0}}, // 25%
    {{0, 1, 1, 1, 1, 0, 0, 0}}, // 50%
    {{1, 0, 0, 1, 1, 1, 1, 1}}  // 25% negated
}};

constexpr std::array<std::uint8_t, 32> kTriangleTable = {
    15, 14, 13, 12, 11, 10,  9,  8,
     7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 12, 13, 14, 15
};

constexpr std::array<std::uint16_t, 16> kNoisePeriodTable = {
    4, 8, 16, 32, 64, 96, 128, 160,
    202, 254, 380, 508, 762, 1016, 2034, 4068
};

} // namespace

Apu::Apu() {
    reset();
}

void Apu::reset() {
    pulse1_ = PulseChannel{};
    pulse1_.isChannel2 = false;
    pulse2_ = PulseChannel{};
    pulse2_.isChannel2 = true;
    triangle_ = TriangleChannel{};
    noise_ = NoiseChannel{};
    noise_.shiftRegister = 1;

    fiveStepMode_ = false;
    irqInhibit_ = false;
    frameIrq_ = false;
    frameCycle_ = 0;
    evenCycle_ = false;
    sampleAccumulator_ = 0.0;
    highPassPrevIn_ = 0.0f;
    highPassPrevOut_ = 0.0f;
    lowPassPrevOut_ = 0.0f;
    sampleBuffer_.clear();
}

void Apu::Envelope::clock() {
    if (start) {
        start = false;
        decayLevel = 15;
        divider = volumePeriod;
        return;
    }

    if (divider > 0) {
        --divider;
    } else {
        divider = volumePeriod;
        if (decayLevel > 0) {
            --decayLevel;
        } else if (loop) {
            decayLevel = 15;
        }
    }
}

std::uint8_t Apu::Envelope::output() const noexcept {
    return constantVolume ? volumePeriod : decayLevel;
}

void Apu::PulseChannel::clockTimer() {
    if (timerCounter > 0) {
        --timerCounter;
    } else {
        timerCounter = timerPeriod;
        dutyStep = (dutyStep + 1) & 0x07u;
    }
}

std::uint16_t Apu::PulseChannel::sweepTargetPeriod() const noexcept {
    const std::uint16_t change = static_cast<std::uint16_t>(timerPeriod >> sweepShift);
    if (sweepNegate) {
        if (isChannel2) {
            return timerPeriod >= change ? static_cast<std::uint16_t>(timerPeriod - change) : 0;
        }
        return timerPeriod > change ? static_cast<std::uint16_t>(timerPeriod - change - 1) : 0;
    }
    return static_cast<std::uint16_t>(timerPeriod + change);
}

bool Apu::PulseChannel::isMuted() const noexcept {
    return timerPeriod < 8 || sweepTargetPeriod() > 0x07ffu;
}

void Apu::PulseChannel::clockLengthAndSweep() {
    if (!lengthHalt && lengthCounter > 0) {
        --lengthCounter;
    }

    const std::uint16_t target = sweepTargetPeriod();
    if (sweepDivider == 0 && sweepEnabled && sweepShift > 0 && !isMuted()) {
        timerPeriod = target;
    }

    if (sweepDivider == 0 || sweepReload) {
        sweepDivider = sweepPeriod;
        sweepReload = false;
    } else {
        --sweepDivider;
    }
}

std::uint8_t Apu::PulseChannel::output() const noexcept {
    if (!enabled || lengthCounter == 0 || isMuted() || kDutyTable[duty][dutyStep] == 0) {
        return 0;
    }
    return envelope.output();
}

void Apu::TriangleChannel::clockTimer() {
    if (timerCounter > 0) {
        --timerCounter;
    } else {
        timerCounter = timerPeriod;
        if (lengthCounter > 0 && linearCounter > 0) {
            sequenceStep = (sequenceStep + 1) & 0x1fu;
        }
    }
}

void Apu::TriangleChannel::clockLinearCounter() {
    if (linearReloadFlag) {
        linearCounter = linearReloadValue;
    } else if (linearCounter > 0) {
        --linearCounter;
    }

    if (!controlFlag) {
        linearReloadFlag = false;
    }
}

void Apu::TriangleChannel::clockLengthCounter() {
    if (!controlFlag && lengthCounter > 0) {
        --lengthCounter;
    }
}

std::uint8_t Apu::TriangleChannel::output() const noexcept {
    if (!enabled || lengthCounter == 0 || linearCounter == 0 || timerPeriod < 2) {
        return 0;
    }
    return kTriangleTable[sequenceStep];
}

void Apu::NoiseChannel::clockTimer() {
    if (timerCounter > 0) {
        --timerCounter;
    } else {
        timerCounter = timerPeriod;
        const std::uint8_t tapBit = mode ? 6 : 1;
        const std::uint16_t feedback = static_cast<std::uint16_t>(
            (shiftRegister & 0x01u) ^ ((shiftRegister >> tapBit) & 0x01u));
        shiftRegister = static_cast<std::uint16_t>((shiftRegister >> 1) | (feedback << 14));
    }
}

void Apu::NoiseChannel::clockLengthCounter() {
    if (!lengthHalt && lengthCounter > 0) {
        --lengthCounter;
    }
}

std::uint8_t Apu::NoiseChannel::output() const noexcept {
    if (!enabled || lengthCounter == 0 || (shiftRegister & 0x01u) != 0) {
        return 0;
    }
    return envelope.output();
}

void Apu::clockQuarterFrame() {
    pulse1_.envelope.clock();
    pulse2_.envelope.clock();
    triangle_.clockLinearCounter();
    noise_.envelope.clock();
}

void Apu::clockHalfFrame() {
    pulse1_.clockLengthAndSweep();
    pulse2_.clockLengthAndSweep();
    triangle_.clockLengthCounter();
    noise_.clockLengthCounter();
}

float Apu::mixOutput() const noexcept {
    const float p1 = static_cast<float>(pulse1_.output());
    const float p2 = static_cast<float>(pulse2_.output());
    const float tri = static_cast<float>(triangle_.output());
    const float noi = static_cast<float>(noise_.output());

    float pulseOut = 0.0f;
    if (p1 + p2 > 0.0f) {
        pulseOut = 95.88f / ((8128.0f / (p1 + p2)) + 100.0f);
    }

    float tndOut = 0.0f;
    const float tndDenom = (tri / 8227.0f) + (noi / 12241.0f);
    if (tndDenom > 0.0f) {
        tndOut = 159.79f / ((1.0f / tndDenom) + 100.0f);
    }

    return pulseOut + tndOut;
}

void Apu::clock() {
    // Triangle clocks at CPU rate; Pulse, Noise, and Frame Sequencer clock at APU rate (CPU / 2)
    triangle_.clockTimer();

    evenCycle_ = !evenCycle_;
    if (evenCycle_) {
        pulse1_.clockTimer();
        pulse2_.clockTimer();
        noise_.clockTimer();

        ++frameCycle_;
        if (!fiveStepMode_) {
            if (frameCycle_ == 3728) {
                clockQuarterFrame();
            } else if (frameCycle_ == 7456) {
                clockQuarterFrame();
                clockHalfFrame();
            } else if (frameCycle_ == 11185) {
                clockQuarterFrame();
            } else if (frameCycle_ >= 14914) {
                clockQuarterFrame();
                clockHalfFrame();
                if (!irqInhibit_) {
                    frameIrq_ = true;
                }
                frameCycle_ = 0;
            }
        } else {
            if (frameCycle_ == 3728) {
                clockQuarterFrame();
            } else if (frameCycle_ == 7456) {
                clockQuarterFrame();
                clockHalfFrame();
            } else if (frameCycle_ == 11185) {
                clockQuarterFrame();
            } else if (frameCycle_ >= 18640) {
                clockQuarterFrame();
                clockHalfFrame();
                frameCycle_ = 0;
            }
        }
    }

    sampleAccumulator_ += static_cast<double>(sampleRate);
    if (sampleAccumulator_ >= cpuClockHz) {
        sampleAccumulator_ -= cpuClockHz;

        const float raw = mixOutput();
        // DC-blocking high-pass + gentle low-pass filter to remove harsh aliasing
        const float highPassed = 0.996f * (highPassPrevOut_ + raw - highPassPrevIn_);
        highPassPrevIn_ = raw;
        highPassPrevOut_ = highPassed;

        const float filtered = lowPassPrevOut_ + 0.65f * (highPassed - lowPassPrevOut_);
        lowPassPrevOut_ = filtered;

        sampleBuffer_.push_back(std::clamp(filtered * 0.8f, -1.0f, 1.0f));
    }
}

std::uint8_t Apu::readStatus() {
    std::uint8_t status = 0;
    if (pulse1_.lengthCounter > 0) status |= 0x01u;
    if (pulse2_.lengthCounter > 0) status |= 0x02u;
    if (triangle_.lengthCounter > 0) status |= 0x04u;
    if (noise_.lengthCounter > 0) status |= 0x08u;
    if (frameIrq_) status |= 0x40u;

    frameIrq_ = false;
    return status;
}

void Apu::writeRegister(std::uint16_t address, std::uint8_t value) {
    switch (address) {
    // Pulse 1 ($4000-$4003)
    case 0x4000:
        pulse1_.duty = (value >> 6) & 0x03u;
        pulse1_.lengthHalt = (value & 0x20u) != 0;
        pulse1_.envelope.loop = pulse1_.lengthHalt;
        pulse1_.envelope.constantVolume = (value & 0x10u) != 0;
        pulse1_.envelope.volumePeriod = value & 0x0fu;
        break;
    case 0x4001:
        pulse1_.sweepEnabled = (value & 0x80u) != 0;
        pulse1_.sweepPeriod = (value >> 4) & 0x07u;
        pulse1_.sweepNegate = (value & 0x08u) != 0;
        pulse1_.sweepShift = value & 0x07u;
        pulse1_.sweepReload = true;
        break;
    case 0x4002:
        pulse1_.timerPeriod = static_cast<std::uint16_t>((pulse1_.timerPeriod & 0x0700u) | value);
        break;
    case 0x4003:
        pulse1_.timerPeriod = static_cast<std::uint16_t>(
            (pulse1_.timerPeriod & 0x00ffu) | ((static_cast<std::uint16_t>(value) & 0x07u) << 8));
        if (pulse1_.enabled) {
            pulse1_.lengthCounter = kLengthTable[(value >> 3) & 0x1fu];
        }
        pulse1_.dutyStep = 0;
        pulse1_.envelope.start = true;
        break;

    // Pulse 2 ($4004-$4007)
    case 0x4004:
        pulse2_.duty = (value >> 6) & 0x03u;
        pulse2_.lengthHalt = (value & 0x20u) != 0;
        pulse2_.envelope.loop = pulse2_.lengthHalt;
        pulse2_.envelope.constantVolume = (value & 0x10u) != 0;
        pulse2_.envelope.volumePeriod = value & 0x0fu;
        break;
    case 0x4005:
        pulse2_.sweepEnabled = (value & 0x80u) != 0;
        pulse2_.sweepPeriod = (value >> 4) & 0x07u;
        pulse2_.sweepNegate = (value & 0x08u) != 0;
        pulse2_.sweepShift = value & 0x07u;
        pulse2_.sweepReload = true;
        break;
    case 0x4006:
        pulse2_.timerPeriod = static_cast<std::uint16_t>((pulse2_.timerPeriod & 0x0700u) | value);
        break;
    case 0x4007:
        pulse2_.timerPeriod = static_cast<std::uint16_t>(
            (pulse2_.timerPeriod & 0x00ffu) | ((static_cast<std::uint16_t>(value) & 0x07u) << 8));
        if (pulse2_.enabled) {
            pulse2_.lengthCounter = kLengthTable[(value >> 3) & 0x1fu];
        }
        pulse2_.dutyStep = 0;
        pulse2_.envelope.start = true;
        break;
// Triangle ($4008-$400B)
    case 0x4008:
        triangle_.controlFlag = (value & 0x80u) != 0;
        triangle_.linearReloadValue = value & 0x7fu;
        break;
    case 0x400a:
        triangle_.timerPeriod = static_cast<std::uint16_t>((triangle_.timerPeriod & 0x0700u) | value);
        break;
    case 0x400b:
        triangle_.timerPeriod = static_cast<std::uint16_t>(
            (triangle_.timerPeriod & 0x00ffu) | ((static_cast<std::uint16_t>(value) & 0x07u) << 8));
        if (triangle_.enabled) {
            triangle_.lengthCounter = kLengthTable[(value >> 3) & 0x1fu];
        }
        triangle_.linearReloadFlag = true;
        break;

    // Noise ($400C-$400F)
    case 0x400c:
        noise_.lengthHalt = (value & 0x20u) != 0;
        noise_.envelope.loop = noise_.lengthHalt;
        noise_.envelope.constantVolume = (value & 0x10u) != 0;
        noise_.envelope.volumePeriod = value & 0x0fu;
        break;
    case 0x400e:
        noise_.mode = (value & 0x80u) != 0;
        noise_.timerPeriod = kNoisePeriodTable[value & 0x0fu];
        break;
    case 0x400f:
        if (noise_.enabled) {
            noise_.lengthCounter = kLengthTable[(value >> 3) & 0x1fu];
        }
        noise_.envelope.start = true;
        break;

    // Status & Channel Enable ($4015)
    case 0x4015:
        pulse1_.enabled = (value & 0x01u) != 0;
        if (!pulse1_.enabled) pulse1_.lengthCounter = 0;

        pulse2_.enabled = (value & 0x02u) != 0;
        if (!pulse2_.enabled) pulse2_.lengthCounter = 0;

        triangle_.enabled = (value & 0x04u) != 0;
        if (!triangle_.enabled) triangle_.lengthCounter = 0;

        noise_.enabled = (value & 0x08u) != 0;
        if (!noise_.enabled) noise_.lengthCounter = 0;
        break;

    // Frame Counter ($4017)
    case 0x4017:
        fiveStepMode_ = (value & 0x80u) != 0;
        irqInhibit_ = (value & 0x40u) != 0;
        if (irqInhibit_) {
            frameIrq_ = false;
        }
        frameCycle_ = 0;
        if (fiveStepMode_) {
            clockQuarterFrame();
            clockHalfFrame();
        }
        break;

    default:
        break;
    }
}

std::vector<float> Apu::consumeSamples() {
    std::vector<float> samples;
    samples.swap(sampleBuffer_);
    return samples;
}

} // namespace runamuck