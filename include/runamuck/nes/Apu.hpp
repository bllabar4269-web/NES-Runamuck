#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace runamuck {

    class Apu {
    public:
        static constexpr double cpuClockHz = 1789773.0;
        static constexpr int sampleRate = 44100;

        Apu();

        void reset();
        void clock();

        std::uint8_t readStatus();
        void writeRegister(std::uint16_t address, std::uint8_t value);

        [[nodiscard]] std::vector<float> consumeSamples();
        [[nodiscard]] bool irqPending() const noexcept { return frameIrq_; }

    private:
        struct Envelope {
            bool start = false;
            bool loop = false;
            bool constantVolume = false;
            std::uint8_t volumePeriod = 0;
            std::uint8_t divider = 0;
            std::uint8_t decayLevel = 0;

            void clock();
            [[nodiscard]] std::uint8_t output() const noexcept;
        };

        struct PulseChannel {
            bool isChannel2 = false;
            bool enabled = false;
            std::uint8_t duty = 0;
            std::uint8_t dutyStep = 0;
            std::uint16_t timerPeriod = 0;
            std::uint16_t timerCounter = 0;
            std::uint8_t lengthCounter = 0;
            bool lengthHalt = false;
            Envelope envelope{};

            bool sweepEnabled = false;
            std::uint8_t sweepPeriod = 0;
            bool sweepNegate = false;
            std::uint8_t sweepShift = 0;
            std::uint8_t sweepDivider = 0;
            bool sweepReload = false;

            void clockTimer();
            void clockLengthAndSweep();
            [[nodiscard]] std::uint16_t sweepTargetPeriod() const noexcept;
            [[nodiscard]] bool isMuted() const noexcept;
            [[nodiscard]] std::uint8_t output() const noexcept;
        };


        struct TriangleChannel {
            bool enabled = false;
            bool controlFlag = false;
            std::uint8_t linearReloadValue = 0;
            std::uint8_t linearCounter = 0;
            bool linearReloadFlag = false;
            std::uint16_t timerPeriod = 0;
            std::uint16_t timerCounter = 0;
            std::uint8_t sequenceStep = 0;
            std::uint8_t lengthCounter = 0;

            void clockTimer();
            void clockLinearCounter();
            void clockLengthCounter();
            [[nodiscard]] std::uint8_t output() const noexcept;
        };

        struct NoiseChannel {
            bool enabled = false;
            bool lengthHalt = false;
            bool mode = false;
            std::uint16_t timerPeriod = 4;
            std::uint16_t timerCounter = 0;
            std::uint16_t shiftRegister = 1;
            std::uint8_t lengthCounter = 0;
            Envelope envelope{};

            void clockTimer();
            void clockLengthCounter();
            [[nodiscard]] std::uint8_t output() const noexcept;
        };

        void clockQuarterFrame();
        void clockHalfFrame();
        [[nodiscard]] float mixOutput() const noexcept;

        PulseChannel pulse1_{};
        PulseChannel pulse2_{};
        TriangleChannel triangle_{};
        NoiseChannel noise_{};

        bool fiveStepMode_ = false;
        bool irqInhibit_ = false;
        bool frameIrq_ = false;
        std::uint32_t frameCycle_ = 0;
        bool evenCycle_ = false;

        double sampleAccumulator_ = 0.0;
        float highPassPrevIn_ = 0.0f;
        float highPassPrevOut_ = 0.0f;
        float lowPassPrevOut_ = 0.0f;
        std::vector<float> sampleBuffer_{};
};

} // namespace runamuck