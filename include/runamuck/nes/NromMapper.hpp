#pragma once

#include "runamuck/nes/Mapper.hpp"

#include <cstddef>
#include <cstdint>

namespace runamuck {

class NromMapper final : public Mapper {
public:
    NromMapper(std::size_t prgRomSize, NametableMirroring mirroring);

    MapperMapping mapCpuRead(std::uint16_t address) override;
    MapperMapping mapCpuWrite(std::uint16_t address, std::uint8_t value) override;
    MapperMapping mapPpuRead(std::uint16_t address) override;
    MapperMapping mapPpuWrite(std::uint16_t address, std::uint8_t value) override;

    NametableMirroring mirroring() const noexcept override;
    void reset() override;

private:
    std::size_t prgRomSize_;
    NametableMirroring mirroring_;
};

} // namespace runamuck