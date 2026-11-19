#include "runamuck/nes/NromMapper.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireMapping(
    runamuck::MapperMapping mapping,
    runamuck::MemoryRegion region,
    std::size_t offset,
    const char* message) {
    require(mapping.region == region && mapping.offset == offset, message);
}

} // namespace

int main() {
    using runamuck::MemoryRegion;
    using runamuck::NametableMirroring;
    using runamuck::NromMapper;

    NromMapper mapper16k(0x4000, NametableMirroring::vertical);
    requireMapping(mapper16k.mapCpuRead(0x8000), MemoryRegion::prgRom, 0,
                   "16 KB PRG starts at 0x8000");
    requireMapping(mapper16k.mapCpuRead(0xc000), MemoryRegion::prgRom, 0,
                   "16 KB PRG mirrors at 0xc000");
    requireMapping(mapper16k.mapCpuRead(0xffff), MemoryRegion::prgRom, 0x3fff,
                   "16 KB PRG maps its final byte");
    requireMapping(mapper16k.mapCpuRead(0x6000), MemoryRegion::prgRam, 0,
                   "PRG RAM starts at 0x6000");
    requireMapping(mapper16k.mapCpuWrite(0x7fff, 0x42), MemoryRegion::prgRam, 0x1fff,
                   "PRG RAM maps its final byte");
    requireMapping(mapper16k.mapCpuWrite(0x8000, 0x42), MemoryRegion::none, 0,
                   "writes to PRG ROM do not map to writable memory");

    requireMapping(mapper16k.mapPpuRead(0x1fff), MemoryRegion::chr, 0x1fff,
                   "CHR maps through 0x1fff");
    requireMapping(mapper16k.mapPpuRead(0x2000), MemoryRegion::none, 0,
                   "addresses above CHR are unmapped");
    require(mapper16k.mirroring() == NametableMirroring::vertical,
            "returns configured mirroring");

    NromMapper mapper32k(0x8000, NametableMirroring::horizontal);
    requireMapping(mapper32k.mapCpuRead(0xc000), MemoryRegion::prgRom, 0x4000,
                   "32 KB PRG maps its second bank at 0xc000");
    requireMapping(mapper32k.mapCpuRead(0xffff), MemoryRegion::prgRom, 0x7fff,
                   "32 KB PRG maps its final byte");

    bool rejectedInvalidSize = false;
    try {
        NromMapper invalidMapper(0x6000, NametableMirroring::horizontal);
    } catch (const std::invalid_argument&) {
        rejectedInvalidSize = true;
    }
    require(rejectedInvalidSize, "rejects unsupported PRG ROM sizes");

    std::cout << "All NROM mapper tests passed.\n";
    return 0;
}