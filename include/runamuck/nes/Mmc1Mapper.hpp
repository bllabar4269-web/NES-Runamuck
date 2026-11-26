#pragma once

#include "runamuck/nes/Mapper.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace runamuck {

class Mmc1Mapper final : public Mapper {
public:
    Mmc1Mapper(
        std::size_t prgRomSize,
        std::size_t chrRomSize,
        NametableMirroring mirroring);

    MapperMapping mapCpuRead(std::uint16_t address) override;
    MapperMapping mapCpuWrite(std::uint16_t address, std::uint8_t value) override;
    MapperMapping mapPpuRead(std::uint16_t address) override;
    MapperMapping mapPpuWrite(std::uint16_t address, std::uint8_t value) override;

    NametableMirroring mirroring() const noexcept override;
    void reset() override;
    void setTraceStream(std::ostream* stream) noexcept { traceStream_ = stream; }

private:
    std::size_t prgRomSize_;
    std::size_t chrRomSize_;
    NametableMirroring mirroring_;
    std::uint8_t control_ = 0x0c;
    std::uint8_t chrBank0_ = 0;
    std::uint8_t chrBank1_ = 0;
    std::uint8_t prgBank_ = 0;
    std::uint8_t shiftRegister_ = 0;
    std::uint8_t shiftCount_ = 0;
    std::ostream* traceStream_ = nullptr;

    void writeRegister(std::uint16_t address, std::uint8_t value);
};

} // namespace runamuck
