#include "runamuck/nes/Cartridge.hpp"
#include "runamuck/nes/NromMapper.hpp"
#include "runamuck/nes/Mmc1Mapper.hpp"
#include "runamuck/nes/Mmc2Mapper.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <ostream>

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

    const bool nes2 = (header[7] & 0x0c) == 0x08;
    if (!nes2 && header[4] == 0) {
        throw std::runtime_error("ROM must contain at least one PRG ROM bank.");
    }

    Cartridge cartridge;
    cartridge.mapperId_ = static_cast<std::uint16_t>(
        (header[6] >> 4) | (header[7] & 0xf0));
    if (nes2) {
        cartridge.mapperId_ |= static_cast<std::uint16_t>(header[8] & 0x0f) << 8;
        cartridge.submapperId_ = header[8] >> 4;
    }

    if (header[6] &0x08) {
        cartridge.mirroring_ = NametableMirroring::fourScreen;
    } else if (header[6] & 0x01) {
        cartridge.mirroring_ = NametableMirroring::vertical;        
    } else {
        cartridge.mirroring_ = NametableMirroring::horizontal;
    }

    if (header[6] & 0x04) {
        file.seekg(512, std::ios::cur);
        if (!file) {
            throw std::runtime_error("ROM file is truncated in its trainer data.");
        }
    }

    std::size_t prgSize = static_cast<std::size_t>(header[4]) * prgBankSize;
    std::size_t chrSize = static_cast<std::size_t>(header[5]) * chrBankSize;
    std::size_t prgRamSize = 0;
    std::size_t chrRamSize = 0;

    if (nes2) {
        const std::uint8_t prgSizeMsb = header[9] & 0x0f;
        const std::uint8_t chrSizeMsb = header[9] >> 4;
        if (prgSizeMsb == 0x0f || chrSizeMsb == 0x0f) {
            throw std::runtime_error(
                "NES 2.0 exponent-form ROM sizes are not supported yet.");
        }

        const std::size_t prgBanks =
            (static_cast<std::size_t>(prgSizeMsb) << 8) | header[4];
        const std::size_t chrBanks =
            (static_cast<std::size_t>(chrSizeMsb) << 8) | header[5];
        prgSize = prgBanks * prgBankSize;
        chrSize = chrBanks * chrBankSize;

        const auto ramSizeFromShift = [](std::uint8_t shift) -> std::size_t {
            return shift == 0 ? 0 : (std::size_t{64} << shift);
        };
        prgRamSize = ramSizeFromShift(header[10] & 0x0f) +
                     ramSizeFromShift(header[10] >> 4);
        chrRamSize = ramSizeFromShift(header[11] & 0x0f) +
                     ramSizeFromShift(header[11] >> 4);
    } else {
        const std::size_t prgRamBanks = header[8] == 0 ? 1 : header[8];
        prgRamSize = prgRamBanks * 8 * 1024;
    }

    if (prgSize == 0) {
        throw std::runtime_error("ROM must contain at least one PRG ROM bank.");
    }
    if (chrSize == 0 && nes2 && chrRamSize != 0 && chrRamSize < chrBankSize) {
        throw std::runtime_error(
            "This PPU currently requires at least 8 KB of NES 2.0 CHR RAM.");
    }
    cartridge.prgRam_.resize(prgRamSize);

    cartridge.prgRom_.resize(prgSize);
    readRomSection(file, cartridge.prgRom_, "PRG ROM");

    cartridge.hasChrRam_ = chrSize == 0;
    cartridge.chrData_.resize(
        cartridge.hasChrRam_ ? std::max(chrBankSize, chrRamSize) : chrSize);
    if (!cartridge.hasChrRam_) {
        readRomSection(file, cartridge.chrData_, "CHR ROM");
    } 
    
    if (cartridge.mapperId_ == 0) {
        cartridge.mapper_ = std::make_unique<NromMapper>(
            prgSize,
            cartridge.mirroring_);
    }
    if (cartridge.mapperId_ == 1) {
        cartridge.mapper_ = std::make_unique<Mmc1Mapper>(
            prgSize,
            chrSize,
            cartridge.mirroring_);
    }
    if (cartridge.mapperId_ == 9) {
        cartridge.mapper_ = std::make_unique<Mmc2Mapper>(
            prgSize,
            chrSize,
            cartridge.mirroring_);
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

bool Cartridge::hasMapper() const noexcept {
    return static_cast<bool>(mapper_);
}

NametableMirroring Cartridge::mirroring() const noexcept {
    return mapper_ ? mapper_->mirroring() : mirroring_;
}

std::optional<std::uint8_t> Cartridge::cpuRead(std::uint16_t address) {
    if (!mapper_) {
        return std::nullopt;
    }

    const auto mapping = mapper_->mapCpuRead(address);
    if (mapping.region == MemoryRegion::prgRom &&
        mapping.offset < prgRom_.size()) {
        return prgRom_[mapping.offset];
        }
     if (mapping.region == MemoryRegion::prgRam &&
         mapping.offset < prgRam_.size()) {
        return prgRam_[mapping.offset];
    }

    return std::nullopt;
}

void Cartridge::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (!mapper_) {
        return;
    }

    const auto mapping = mapper_->mapCpuWrite(address, value);
    if (mapping.region == MemoryRegion::prgRam &&
        mapping.offset < prgRam_.size()) {
        prgRam_[mapping.offset] = value;
    }
}

std::optional<std::uint8_t> Cartridge::ppuRead(std::uint16_t address) {
    if (!mapper_) {
        return std::nullopt;
    }

    const auto mapping = mapper_->mapPpuRead(address);
    if (mapping.region == MemoryRegion::chr &&
        mapping.offset < chrData_.size()) {
        return chrData_[mapping.offset];
    }

    return std::nullopt;
}

std::optional<std::uint8_t> Cartridge::ppuPeek(std::uint16_t address) {
    if (!mapper_) {
        return std::nullopt;
    }

    const auto mapping = mapper_->mapPpuPeek(address);
    if (mapping.region == MemoryRegion::chr &&
        mapping.offset < chrData_.size()) {
        return chrData_[mapping.offset];
    }

    return std::nullopt;
}

std::optional<std::size_t> Cartridge::ppuPeekChrBank(std::uint16_t address) {
    if (!mapper_ || address >= 0x2000) {
        return std::nullopt;
    }

    const auto mapping = mapper_->mapPpuPeek(address);
    if (mapping.region != MemoryRegion::chr) {
        return std::nullopt;
    }
    return mapping.offset / 0x1000;
}

void Cartridge::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (!mapper_) {
        return;
    }

    const auto mapping = mapper_->mapPpuWrite(address, value);
    if (hasChrRam_ &&
        mapping.region == MemoryRegion::chr &&
        mapping.offset < chrData_.size()) {
        chrData_[mapping.offset] = value;
    }
}    

void Cartridge::setMmc1TraceStream(std::ostream* stream) noexcept {
    if (auto* mmc1 = dynamic_cast<Mmc1Mapper*>(mapper_.get())) {
        mmc1->setTraceStream(stream);
    }
}
      
} //namespace runamuck
