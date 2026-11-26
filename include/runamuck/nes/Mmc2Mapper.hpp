#pragma once

#include "runamuck/nes/Mapper.hpp"

#include <cstddef>
#include <cstdint>

namespace runamuck {

class Mmc2Mapper final : public Mapper {
public:
    Mmc2Mapper(
        std::size_t prgRomSize,
        std::size_t chrRomSize,
        NametableMirroring mirroring);

    MapperMapping mapCpuRead(std::uint16_t address) override;
    MapperMapping mapCpuWrite(std::uint16_t address, std::uint8_t value) override;
    MapperMapping mapPpuRead(std::uint16_t address) override;
    MapperMapping mapPpuPeek(std::uint16_t address) override;
    MapperMapping mapPpuWrite(std::uint16_t address, std::uint8_t value) override;

    NametableMirroring mirroring() const noexcept override;
    void reset() override;

private:
    std::size_t prgRomSize_;
    std::size_t chrRomSize_;
    NametableMirroring initialMirroring_;
    NametableMirroring mirroring_;
    std::uint8_t prgBank_ = 0;
    std::uint8_t chrBank0Fd_ = 0;
    std::uint8_t chrBank0Fe_ = 0;
    std::uint8_t chrBank1Fd_ = 0;
    std::uint8_t chrBank1Fe_ = 0;
    std::uint8_t chrLatch0_ = 0xfe;
    std::uint8_t chrLatch1_ = 0xfe;

    std::size_t selectedChrBank(std::uint16_t address) const noexcept;
    void updateChrLatch(std::uint16_t address) noexcept;
    void writeRegister(std::uint16_t address, std::uint8_t value) noexcept;
};

} // namespace runamuck
