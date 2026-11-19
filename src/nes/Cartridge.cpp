#include "runamuck/nes/Cartridge.hpp"

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t headerSize = 16;
constexpr std::size_t prgBankSize = 16 * 1024;
constexpr std::size_t chrBankSize = 8 * 1024;

void readRomSection(
     std::ifstream& file,
     std::vector<std::uint8_t>& data,
     const char* sectionName) {
     if (data.empty()) {
        return;
     }
    
     const auto expected = static_cast<std::streamsize>(data.size());
     file.read(reinterpret_cast<char*>(data.data()), expected);

     if (file.gcount() != expected) {
        throw std::runtime_error(std::string("ROM is truncated in ") + sectionName + " data.");
     }   

}    

} // namespace

namespace runamuck { 

Cartridge Cartridge::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open ROM file: " + path.string());
    }

    std::array<std::uint8_t, headerSize> header{};
    const auto expectedHeaderSize = static_cast<std::streamsize>(header.size());
    file.read(reinterpret_cast<char*>(header.data()), expectedHeaderSize);

    if (file.gcount() != expectedHeaderSize) {
        throw std::runtime_error("ROM file is too small to contain an iNES header."); 
    }

    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1a) {
        throw std::runtime_error("ROM file has an invalid iNES signature.");
    }

    if ((header[7] & 0x0c) == 0x08) {
        throw std::runtime_error("NES 2.0 ROMs are not supported yet.");
    }

    if (header[4] == 0) {
        throw std::runtime_error("ROM must contain at least one PRG ROM bank.");
    }

    Cartridge cartridge;
    cartridge.mapperId_ = static_cast<std::uint16_t>(
        (header[6] >> 4) | (header[7] & 0xf0));

    if (header[6] & 0x04) {
        file.seekg(512, std::ios::cur);
        if (!file) {
            throw std::runtime_error("ROM file is truncated in its trainer data.");
        }
    }

    const std::size_t prgSize = static_cast<std::size_t>(header[4]) * prgBankSize;
    const std::size_t chrSize = static_cast<std::size_t>(header[5]) * chrBankSize;

    cartridge.prgRom_.resize(prgSize);
    readRomSection(file, cartridge.prgRom_, "PRG ROM");

    cartridge.hasChrRam_ = chrSize == 0;
    cartridge.chrData_.resize(cartridge.hasChrRam_ ? chrBankSize : chrSize);
    if (!cartridge.hasChrRam_) {
        readRomSection(file, cartridge.chrData_, "CHR ROM");
    }

    return cartridge;
}

std::uint16_t Cartridge::mapperId() const noexcept {
    return mapperId_;
}

std::uint8_t Cartridge::submapperId() const noexcept {
    return submapperId_;
}

const std::vector<std::uint8_t>& Cartridge::prgRom() const noexcept {
    return prgRom_;
}

const std::vector<std::uint8_t>& Cartridge::chrData() const noexcept {
    return chrData_;
}

std::vector<std::uint8_t>& Cartridge::chrData() noexcept {
    return chrData_;
}

bool Cartridge::hasChrRam() const noexcept {
    return hasChrRam_;
}
} //namespace runamuck