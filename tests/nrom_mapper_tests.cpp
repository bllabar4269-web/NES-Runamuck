#include "runamuck/nes/NromMapper.hpp"
#include "runamuck/nes/Mmc1Mapper.hpp"
#include "runamuck/nes/Mmc2Mapper.hpp"

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
    using runamuck::Mmc1Mapper;
    using runamuck::Mmc2Mapper;

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

    Mmc1Mapper mmc1(0x20000, 0x20000, NametableMirroring::horizontal);
    requireMapping(mmc1.mapCpuRead(0x8000), MemoryRegion::prgRom, 0,
                   "MMC1 starts at PRG bank zero");

    const auto writeBits = [&mmc1](std::uint16_t address,
                                   std::uint8_t value) {
        for (std::uint8_t bit = 0; bit < 5; ++bit) {
            mmc1.mapCpuWrite(address, (value >> bit) & 1);
        }
    };

    requireMapping(mmc1.mapPpuRead(0x0000), MemoryRegion::chr, 0,
                   "MMC1 8 KB CHR mode maps the first pattern table");
    requireMapping(mmc1.mapPpuRead(0x1000), MemoryRegion::chr, 0x1000,
                   "MMC1 8 KB CHR mode maps the second pattern table");
    writeBits(0xa000, 2);
    requireMapping(mmc1.mapPpuRead(0x0000), MemoryRegion::chr, 0x2000,
                   "MMC1 8 KB CHR mode selects an even bank pair");
    requireMapping(mmc1.mapPpuRead(0x1000), MemoryRegion::chr, 0x3000,
                   "MMC1 8 KB CHR mode maps the paired odd bank");

    writeBits(0x8000, 0);
    require(mmc1.mirroring() == NametableMirroring::singleScreenLower,
            "MMC1 control register selects lower single-screen mirroring");

    writeBits(0x8000, 0x1c);
    writeBits(0xa000, 5);
    writeBits(0xc000, 7);
    requireMapping(mmc1.mapPpuRead(0x0000), MemoryRegion::chr, 0x5000,
                   "MMC1 4 KB CHR mode selects its lower bank");
    requireMapping(mmc1.mapPpuRead(0x1000), MemoryRegion::chr, 0x7000,
                   "MMC1 4 KB CHR mode selects its upper bank");

    writeBits(0xe000, 2);
    writeBits(0x8000, 0);
    requireMapping(mmc1.mapCpuRead(0x8000), MemoryRegion::prgRom, 0x8000,
                   "MMC1 32 KB PRG mode selects even PRG bank");
    requireMapping(mmc1.mapCpuRead(0xc000), MemoryRegion::prgRom, 0xc000,
                   "MMC1 32 KB PRG mode maps the following bank");

    Mmc2Mapper mmc2(0x10000, 0x20000, NametableMirroring::vertical);
    requireMapping(mmc2.mapCpuRead(0x8000), MemoryRegion::prgRom, 0,
                   "MMC2 starts with PRG bank zero");
    requireMapping(mmc2.mapCpuRead(0xa000), MemoryRegion::prgRom, 0xa000,
                   "MMC2 maps its first fixed PRG bank");
    requireMapping(mmc2.mapCpuRead(0xc000), MemoryRegion::prgRom, 0xc000,
                   "MMC2 maps its second fixed PRG bank");
    requireMapping(mmc2.mapCpuRead(0xe000), MemoryRegion::prgRom, 0xe000,
                   "MMC2 maps its final fixed PRG bank");
    mmc2.mapCpuWrite(0xa000, 3);
    requireMapping(mmc2.mapCpuRead(0x8000), MemoryRegion::prgRom, 0x6000,
                   "MMC2 selects an 8 KB switchable PRG bank");

    mmc2.mapCpuWrite(0xb000, 2);
    mmc2.mapCpuWrite(0xc000, 4);
    mmc2.mapCpuWrite(0xd000, 6);
    mmc2.mapCpuWrite(0xe000, 8);
    requireMapping(mmc2.mapPpuRead(0x0000), MemoryRegion::chr, 0x4000,
                   "MMC2 initially selects the lower FE CHR bank");
    requireMapping(mmc2.mapPpuRead(0x0fd8), MemoryRegion::chr, 0x4fd8,
                   "MMC2 uses the current CHR bank before updating its latch");
    requireMapping(mmc2.mapPpuRead(0x0000), MemoryRegion::chr, 0x2000,
                   "MMC2 lower latch selects its FD CHR bank");
    requireMapping(mmc2.mapPpuRead(0x0fe8), MemoryRegion::chr, 0x2fe8,
                   "MMC2 lower FE trigger uses the current FD bank");
    requireMapping(mmc2.mapPpuRead(0x1000), MemoryRegion::chr, 0x8000,
                   "MMC2 upper latch starts on its FE CHR bank");
    requireMapping(mmc2.mapPpuRead(0x1fd8), MemoryRegion::chr, 0x8fd8,
                   "MMC2 upper FD trigger uses the current FE bank");
    requireMapping(mmc2.mapPpuRead(0x1000), MemoryRegion::chr, 0x6000,
                   "MMC2 upper latch selects its FD CHR bank");

    mmc2.mapCpuWrite(0xf000, 1);
    require(mmc2.mirroring() == NametableMirroring::horizontal,
            "MMC2 selects horizontal mirroring");
    mmc2.mapCpuWrite(0xf000, 0);
    require(mmc2.mirroring() == NametableMirroring::vertical,
            "MMC2 selects vertical mirroring");

    std::cout << "All NROM mapper tests passed.\n";
    return 0;
}