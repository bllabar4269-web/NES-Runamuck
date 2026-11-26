#pragma once

#include <cstddef>
#include <cstdint>

namespace runamuck {

enum class MemoryRegion : std::uint8_t {
    none,
    prgRom,
    prgRam,
    chr
};

struct MapperMapping {
    MemoryRegion region = MemoryRegion::none;
    std::size_t offset = 0;
};

enum class NametableMirroring : std::uint8_t {
    horizontal,
    vertical,
    singleScreenLower,
    singleScreenUpper,
    fourScreen
};

class Mapper {
public:
    virtual ~Mapper() = default;

    virtual MapperMapping mapCpuRead(std::uint16_t address) = 0;
    virtual MapperMapping mapCpuWrite(std::uint16_t address, std::uint8_t value) = 0;
    virtual MapperMapping mapPpuRead(std::uint16_t address) = 0;
    virtual MapperMapping mapPpuPeek(std::uint16_t address) {
        return mapPpuRead(address);
    }
    virtual MapperMapping mapPpuWrite(std::uint16_t address, std::uint8_t value) = 0;

    virtual NametableMirroring mirroring() const noexcept = 0;
    virtual void reset() = 0;

    virtual void clockCpuCycle() {}
    virtual void clockPpuAddress(std::uint16_t) {}
    virtual bool irqPending() const noexcept { return false; }
};

} // namespace runamuck
