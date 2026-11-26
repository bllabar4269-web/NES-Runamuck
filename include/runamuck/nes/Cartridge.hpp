#pragma once

#include "runamuck/nes/Mapper.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <vector>

namespace runamuck {

class Cartridge {
public:
    static Cartridge load(const std::filesystem::path& path);

    std::uint16_t mapperId() const noexcept;
    std::uint8_t submapperId() const noexcept;
    const std::vector<std::uint8_t>& prgRom() const noexcept;
    const std::vector<std::uint8_t>& chrData() const noexcept;
    std::vector<std::uint8_t>& chrData() noexcept;
    bool hasChrRam() const noexcept;
    bool hasMapper() const noexcept;
    NametableMirroring mirroring() const noexcept;

    std::optional<std::uint8_t> cpuRead(std::uint16_t address);
    void cpuWrite(std::uint16_t address, std::uint8_t value);
    std::optional<std::uint8_t> ppuRead(std::uint16_t address);
    std::optional<std::uint8_t> ppuPeek(std::uint16_t address);
    std::optional<std::size_t> ppuPeekChrBank(std::uint16_t address);
    void ppuWrite(std::uint16_t address, std::uint8_t value);
    void setMmc1TraceStream(std::ostream* stream) noexcept;


private:
    std::uint16_t mapperId_ = 0;
    std::uint8_t submapperId_ = 0;
    std::vector<std::uint8_t> prgRom_;
    std::vector<std::uint8_t> chrData_;
    bool hasChrRam_ = false;
    std::vector<std::uint8_t> prgRam_;
    NametableMirroring mirroring_ = NametableMirroring::horizontal;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace runamuck
