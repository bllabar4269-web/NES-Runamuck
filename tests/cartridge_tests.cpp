#include "runamuck/nes/Cartridge.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr std::size_t headerSize = 16;
constexpr std::size_t prgBankSize = 16 * 1024;
constexpr std::size_t chrBankSize = 8 * 1024;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class TemporaryRom {
public:
    explicit TemporaryRom(const std::vector<std::uint8_t>& bytes)
        : path_(std::filesystem::temp_directory_path() /
                ("runamuck-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".nes")) {
        std::ofstream file(path_, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Could not create temporary ROM file.");
        }

        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));

        if (!file) {
            throw std::runtime_error("Could not write temporary ROM file.");
        }
    }

    ~TemporaryRom() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::vector<std::uint8_t> makeRom(
    std::uint8_t prgBanks,
    std::uint8_t chrBanks,
    std::uint8_t mapper) {
    const std::size_t prgSize = static_cast<std::size_t>(prgBanks) * prgBankSize;
    const std::size_t chrSize = static_cast<std::size_t>(chrBanks) * chrBankSize;
    std::vector<std::uint8_t> bytes(headerSize + prgSize + chrSize);

    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1a;
    bytes[4] = prgBanks;
    bytes[5] = chrBanks;
    bytes[6] = static_cast<std::uint8_t>((mapper & 0x0f) << 4);
    bytes[7] = static_cast<std::uint8_t>(mapper & 0xf0);

    if (prgSize > 0) {
        bytes[headerSize] = 0xa5;
    }
    if (chrSize > 0) {
        bytes[headerSize + prgSize] = 0x5a;
    }

    return bytes;
}

void testLoadsMapperAndRomData() {
    TemporaryRom rom(makeRom(1, 1, 0x23));
    const auto cartridge = runamuck::Cartridge::load(rom.path());

    require(cartridge.mapperId() == std::uint16_t{0x23}, "mapper ID combines header flags");
    require(cartridge.submapperId() == std::uint8_t{0}, "iNES submapper defaults to zero");
    require(cartridge.prgRom().size() == prgBankSize, "loads one PRG bank");
    require(cartridge.prgRom()[0] == 0xa5, "loads PRG data");
    require(cartridge.chrData().size() == chrBankSize, "loads one CHR bank");
    require(cartridge.chrData()[0] == 0x5a, "loads CHR data");
    require(!cartridge.hasChrRam(), "CHR ROM is not reported as CHR RAM");
}

void testLoadsMmc2Mapper() {
    TemporaryRom rom(makeRom(8, 16, 9));
    auto cartridge = runamuck::Cartridge::load(rom.path());

    require(cartridge.mapperId() == std::uint16_t{9}, "loads the MMC2 mapper ID");
    require(cartridge.hasMapper(), "creates an MMC2 mapper");
    require(cartridge.cpuRead(0x8000) == std::uint8_t{0xa5},
            "reads the initial MMC2 PRG bank");
}

void testAllocatesChrRam() {
    TemporaryRom rom(makeRom(1, 0, 2));
    const auto cartridge = runamuck::Cartridge::load(rom.path());

    require(cartridge.hasChrRam(), "zero CHR banks means CHR RAM");
    require(cartridge.chrData().size() == chrBankSize, "allocates 8 KB of CHR RAM");
    require(cartridge.chrData()[0] == 0, "CHR RAM starts zero-initialized");
}

void testNromCpuAndChrRamAccess() {
    TemporaryRom rom(makeRom(1, 0, 0));
    auto cartridge = runamuck::Cartridge::load(rom.path());

    require(cartridge.hasMapper(), "creates an NROM mapper");
    require(cartridge.cpuRead(0x8000) == std::uint8_t{0xa5}, "reads PRG ROM");
    require(cartridge.cpuRead(0xc000) == std::uint8_t{0xa5}, "mirrors 16 KB PRG ROM");
    require(!cartridge.cpuRead(0x5fff), "returns no value for unmapped CPU addresses");

    cartridge.cpuWrite(0x6000, 0x3c);
    require(cartridge.cpuRead(0x6000) == std::uint8_t{0x3c}, "writes and reads PRG RAM");

    cartridge.ppuWrite(0x0000, 0x42);
    require(cartridge.ppuRead(0x0000) == std::uint8_t{0x42}, "writes and reads CHR RAM");
    require(!cartridge.ppuRead(0x2000), "returns no value outside the CHR address range");
}

void testChrRomIsReadOnly() {
    TemporaryRom rom(makeRom(1, 1, 0));
    auto cartridge = runamuck::Cartridge::load(rom.path());

    cartridge.ppuWrite(0x0000, 0x33);
    require(cartridge.ppuRead(0x0000) == std::uint8_t{0x5a}, "does not write to CHR ROM");
}

void testRejectsInvalidSignature() {
    auto bytes = makeRom(1, 0, 0);
    bytes[0] = 'X';
    TemporaryRom rom(bytes);

    bool rejected = false;
    try {
        (void)runamuck::Cartridge::load(rom.path());
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(rejected, "rejects an invalid iNES signature");
}

} // namespace

int main() {
    testLoadsMapperAndRomData();
    testLoadsMmc2Mapper();
    testAllocatesChrRam();
    testRejectsInvalidSignature();
    testNromCpuAndChrRamAccess();
    testChrRomIsReadOnly();
    std::cout << "All cartridge tests passed.\n";
    return 0;
}