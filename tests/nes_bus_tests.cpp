#include "runamuck/nes/NesBus.hpp"
#include "runamuck/nes/Cartridge.hpp"
#include "runamuck/nes/Ppu.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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
    explicit TemporaryRom(const std::vector<std::uint8_t>& bytes)
        : path_(std::filesystem::temp_directory_path() /
                ("runamuck-bus-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".nes")) {
        std::ofstream file(path_, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Could not create temporary ROM file.");
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
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

std::vector<std::uint8_t> makeRom() {
    std::vector<std::uint8_t> bytes(16 + (16 * 1024));
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1a;
    bytes[4] = 1;
    bytes[5] = 0;
    bytes[6] = 0;
    bytes[7] = 0;
    bytes[16] = 0xa5;
    return bytes;
}

} // namespace

int main() {
    runamuck::NesBus bus;

    bus.write(0x0000, 0x42);
    require(bus.read(0x0000) == 0x42, "RAM reads back its written value");
    require(bus.read(0x0800) == 0x42, "RAM mirrors at 0x800");
    require(bus.read(0x1000) == 0x42, "RAM mirrors at 0x1000");
    require(bus.read(0x1800) == 0x42, "RAM mirrors at 0x1800");

    bus.write(0x1fff, 0xa5);
    require(bus.read(0x07ff) == 0xa5, "last RAM byte mirrors to 0x07ff");

    TemporaryRom rom(makeRom());
    runamuck::NesBus cartridgeBus;
    cartridgeBus.setCartridge(runamuck::Cartridge::load(rom.path()));
    require(cartridgeBus.hasCartridge(), "inserts a cartridge");
    require(cartridgeBus.read(0x8000) == 0xa5, "CPU reads cartridge PRG ROM");
    require(cartridgeBus.read(0xc000) == 0xa5, "CPU reads mirrored PRG ROM");

    cartridgeBus.write(0x6000, 0x3c);
    require(cartridgeBus.read(0x6000) == 0x3c, "CPU writes cartridge PRG RAM");

    cartridgeBus.removeCartridge();
    require(!cartridgeBus.hasCartridge(), "removes a cartridge");

    runamuck::NesBus dmaBus;
    auto dmaPpu = std::make_shared<runamuck::Ppu>();
    dmaBus.setPpu(dmaPpu);
    for (std::uint16_t i = 0; i < 256; ++i) {
        dmaBus.write(
            static_cast<std::uint16_t>(0x0200 + i),
            static_cast<std::uint8_t>(i ^ 0x5a));
    }
    dmaPpu->writeRegister(0x2003, 0x80);
    dmaBus.write(0x4014, 0x02);
    for (std::uint16_t i = 0; i < 256; ++i) {
        const auto address = static_cast<std::uint8_t>(0x80 + i);
        dmaPpu->writeRegister(0x2003, address);
        require(
            dmaPpu->readRegister(0x2004) ==
                static_cast<std::uint8_t>(i ^ 0x5a),
            "OAM DMA copies a full page and wraps from OAMADDR");
    }

    runamuck::NesBus controllerBus;
    controllerBus.setControllerButton(
        0, runamuck::ControllerButton::a, true);
    controllerBus.setControllerButton(
        0, runamuck::ControllerButton::start, true);
    controllerBus.setControllerButton(
        1, runamuck::ControllerButton::b, true);
    controllerBus.write(0x4016, 1);
    require((controllerBus.read(0x4016) & 1) == 1,
            "controller strobe returns the current A button");
    controllerBus.setControllerButton(
        0, runamuck::ControllerButton::a, false);
    require((controllerBus.read(0x4016) & 1) == 0,
            "high controller strobe reflects live button changes");
    controllerBus.setControllerButton(
        0, runamuck::ControllerButton::a, true);
    controllerBus.write(0x4016, 0);
    require((controllerBus.read(0x4016) & 1) == 1,
            "controller serial read starts with A");
    require((controllerBus.read(0x4016) & 1) == 0,
            "controller serial read continues with B");
    require((controllerBus.read(0x4016) & 1) == 0,
            "controller serial read continues with Select");
    require((controllerBus.read(0x4016) & 1) == 1,
            "controller serial read reaches Start");
    require((controllerBus.read(0x4017) & 1) == 0,
            "second controller is read at 0x4017");
    require((controllerBus.read(0x4017) & 1) == 1,
            "second controller serial read includes its B button");

    std::cout << "ALL NesBus Tests Passed.\n";
    return 0;

}
