#include "runamuck/nes/Ppu.hpp"
#include "runamuck/nes/Cartridge.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class TemporaryRom {
public:
    TemporaryRom()
        : path_(std::filesystem::temp_directory_path() /
                ("runamuck-ppu-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".nes")) {
        std::vector<std::uint8_t> bytes(16 + 16 * 1024);
        bytes[0] = 'N';
        bytes[1] = 'E';
        bytes[2] = 'S';
        bytes[3] = 0x1a;
        bytes[4] = 1;
        bytes[6] = 1;
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

class TemporaryMmc2Rom {
public:
    TemporaryMmc2Rom()
        : path_(std::filesystem::temp_directory_path() /
                ("runamuck-mmc2-ppu-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".nes")) {
        constexpr std::size_t headerSize = 16;
        constexpr std::size_t prgSize = 0x8000;
        constexpr std::size_t chrSize = 0x2000;
        std::vector<std::uint8_t> bytes(headerSize + prgSize + chrSize);
        bytes[0] = 'N';
        bytes[1] = 'E';
        bytes[2] = 'S';
        bytes[3] = 0x1a;
        bytes[4] = 2;
        bytes[5] = 1;
        bytes[6] = 0x90;

        const std::size_t chrStart = headerSize + prgSize;
        for (std::size_t row = 0; row < 8; ++row) {
            bytes[chrStart + 0x0fd0 + row] = 0xff;
            bytes[chrStart + 0x0fd8 + row] = 0x00;
            bytes[chrStart + 0x1fd0 + row] = 0x00;
            bytes[chrStart + 0x1fd8 + row] = 0xff;

            // Tile 0 differs by CHR bank so sprite fetch order can be tested:
            // it is transparent in the FE bank and opaque in the FD bank.
            bytes[chrStart + 0x1000 + row] = 0x00;
            bytes[chrStart + 0x1000 + 8 + row] = 0xff;
        }

        std::ofstream file(path_, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Could not create temporary MMC2 ROM file.");
        }
        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            throw std::runtime_error("Could not write temporary MMC2 ROM file.");
        }
    }

    ~TemporaryMmc2Rom() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeVram(runamuck::Ppu& ppu, std::uint16_t address, std::uint8_t value) {
    ppu.writeRegister(0x2006, static_cast<std::uint8_t>(address >> 8));
    ppu.writeRegister(0x2006, static_cast<std::uint8_t>(address));
    ppu.writeRegister(0x2007, value);
}

} // namespace

int main() {
    runamuck::Ppu ppu;

    ppu.writeRegister(0x2006, 0x3f);
    ppu.writeRegister(0x2006, 0x00);
    ppu.writeRegister(0x2007, 0x12);

    ppu.writeRegister(0x2006, 0x3f);
    ppu.writeRegister(0x2006, 0x00);
    require(ppu.readRegister(0x2007) == 0x12,
            "PPU writes and reads palette memory");

    bool nmiTriggered = false;
    ppu.reset();
    ppu.setVblankCallback([&nmiTriggered]() { nmiTriggered = true; });
    ppu.writeRegister(0x2000, 0x80);
    ppu.clock(241 * 341 + 1);
    require(nmiTriggered, "PPU triggers VBlank NMI");
    require((ppu.readStatus() & 0x80) != 0,
            "PPU exposes VBlank status");
    require((ppu.readStatus() & 0x80) == 0,
            "reading PPU status clears VBlank");

    ppu.reset();
    ppu.clock(262 * 341);
    require(ppu.frameComplete(), "PPU completes a frame");
    ppu.acknowledgeFrame();
    require(!ppu.frameComplete(), "PPU frame completion can be acknowledged");

    TemporaryRom rom;
    auto cartridge = std::make_shared<runamuck::Cartridge>(
        runamuck::Cartridge::load(rom.path()));

    runamuck::Ppu scrollPpu;
    scrollPpu.setCartridge(cartridge);
    for (std::uint16_t row = 0; row < 8; ++row) {
        writeVram(scrollPpu, row, 0xff);
        writeVram(
            scrollPpu, static_cast<std::uint16_t>(0x0010 + row), 0x00);
        writeVram(
            scrollPpu, static_cast<std::uint16_t>(0x0018 + row), 0xff);
    }
    writeVram(scrollPpu, 0x0008, 0x00);
    writeVram(scrollPpu, 0x3f01, 0x01);
    writeVram(scrollPpu, 0x3f02, 0x16);
    writeVram(scrollPpu, 0x2000, 0x00);
    writeVram(scrollPpu, 0x2400, 0x01);
    scrollPpu.writeRegister(0x2000, 0x01);
    scrollPpu.writeRegister(0x2001, 0x0a);
    scrollPpu.clock(1);
    require(scrollPpu.framebuffer()[0] == 0xff982220,
            "renders the PPUCTRL-selected base nametable");

    scrollPpu.reset();
    scrollPpu.setCartridge(cartridge);
    writeVram(scrollPpu, 0x2000, 0x00);
    writeVram(scrollPpu, 0x2400, 0x01);
    scrollPpu.writeRegister(0x2005, 0xff);
    scrollPpu.writeRegister(0x2005, 0);
    scrollPpu.writeRegister(0x2001, 0x0a);
    scrollPpu.clock(1);
    require(scrollPpu.framebuffer()[1] == 0xff982220,
            "scrolls across the horizontal nametable boundary");

    scrollPpu.reset();
    scrollPpu.setCartridge(cartridge);
    writeVram(scrollPpu, 0x2400, 0x01);
    scrollPpu.writeRegister(0x2005, 0);
    scrollPpu.writeRegister(0x2005, 239);
    scrollPpu.writeRegister(0x2001, 0x0a);
    scrollPpu.clock(342);
    require(
        scrollPpu.framebuffer()[runamuck::Ppu::width] == 0xff001e74,
        "scrolls across the vertical nametable boundary");

    auto dotPpu = std::make_unique<runamuck::Ppu>();
    dotPpu->setCartridge(cartridge);
    dotPpu->setDotRendererEnabled(true);
    for (std::uint16_t row = 0; row < 8; ++row) {
        writeVram(*dotPpu, row, 0xff);
        writeVram(*dotPpu, static_cast<std::uint16_t>(0x0008 + row), 0x00);
    }
    writeVram(*dotPpu, 0x3f01, 0x01);
    dotPpu->writeRegister(0x2001, 0x0a);
    dotPpu->clock(262u * 341u + 342u);
    require(dotPpu->framebuffer()[runamuck::Ppu::width] == 0xff001e74,
            "renders background pixels through the dot fetch pipeline");

    TemporaryMmc2Rom mmc2Rom;
    auto mmc2Cartridge = std::make_shared<runamuck::Cartridge>(
        runamuck::Cartridge::load(mmc2Rom.path()));
    mmc2Cartridge->cpuWrite(0xb000, 1);
    mmc2Cartridge->cpuWrite(0xc000, 0);
    runamuck::Ppu mmc2Ppu;
    mmc2Ppu.setCartridge(mmc2Cartridge);
    writeVram(mmc2Ppu, 0x2000, 0xfd);
    writeVram(mmc2Ppu, 0x3f01, 0x01);
    writeVram(mmc2Ppu, 0x3f02, 0x16);
    mmc2Ppu.writeRegister(0x2001, 0x0a);
    mmc2Ppu.clock(1);
    for (int x = 0; x < 8; ++x) {
        require(mmc2Ppu.framebuffer()[x] == 0xff001e74,
                "renders an MMC2 tile row from one CHR bank selection");
    }

    auto mmc2SpriteCartridge = std::make_shared<runamuck::Cartridge>(
        runamuck::Cartridge::load(mmc2Rom.path()));
    mmc2SpriteCartridge->cpuWrite(0xb000, 1);
    mmc2SpriteCartridge->cpuWrite(0xc000, 0);
    auto mmc2SpritePpu = std::make_unique<runamuck::Ppu>();
    mmc2SpritePpu->setCartridge(mmc2SpriteCartridge);
    mmc2SpritePpu->writeRegister(0x2003, 0);
    mmc2SpritePpu->writeRegister(0x2004, 0);
    mmc2SpritePpu->writeRegister(0x2004, 0xfd);
    mmc2SpritePpu->writeRegister(0x2004, 0);
    mmc2SpritePpu->writeRegister(0x2004, 0);
    mmc2SpritePpu->writeRegister(0x2004, 0);
    mmc2SpritePpu->writeRegister(0x2004, 0x00);
    mmc2SpritePpu->writeRegister(0x2004, 0);
    mmc2SpritePpu->writeRegister(0x2004, 8);
    writeVram(*mmc2SpritePpu, 0x3f11, 0x01);
    writeVram(*mmc2SpritePpu, 0x3f12, 0x16);
    mmc2SpritePpu->writeRegister(0x2001, 0x14);
    mmc2SpritePpu->clock(342);
    require(mmc2SpritePpu->framebuffer()[runamuck::Ppu::width + 8] ==
                0xff982220,
            "fetches MMC2 sprite CHR banks in OAM order");

    runamuck::Ppu spritePpu;
    spritePpu.setCartridge(cartridge);
    for (std::uint16_t row = 0; row < 8; ++row) {
        writeVram(spritePpu, row, 0xff);
        writeVram(spritePpu, static_cast<std::uint16_t>(0x0008 + row), 0x00);
    }
    writeVram(spritePpu, 0x3f00, 0x0f);
    writeVram(spritePpu, 0x3f01, 0x01);
    writeVram(spritePpu, 0x3f11, 0x16);
    spritePpu.writeRegister(0x2003, 0);
    spritePpu.writeRegister(0x2004, 0);
    spritePpu.writeRegister(0x2004, 0);
    spritePpu.writeRegister(0x2004, 0);
    spritePpu.writeRegister(0x2004, 10);
    spritePpu.writeRegister(0x2001, 0x1e);
    spritePpu.clock(342);
    require(spritePpu.framebuffer()[runamuck::Ppu::width + 10] ==
                0xff982220,
            "renders a CHR sprite from OAM");
    spritePpu.clock(10);
    require((spritePpu.readStatus() & 0x40) != 0,
            "sets sprite-zero-hit when opaque sprite and background pixels overlap");
    spritePpu.clock(262 * 341 - 352);
    require(spritePpu.activeSpriteCount() == 1,
            "counts the visible sprite in OAM");
    require(spritePpu.lastFrameSpritePixelCount() > 0,
            "tracks rendered sprite pixels for a frame");

    std::cout << "ALL PPU Tests Passed.\n";
    return 0;
}
