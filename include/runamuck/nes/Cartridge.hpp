#pragma once

#include <cstdint>
#include <filesystem>
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

private:
    std::uint16_t mapperId_ = 0;
    std::uint8_t submapperId_ = 0;
    std::vector<std::uint8_t> prgRom_;
    std::vector<std::uint8_t> chrData_;
    bool hasChrRam_ = false;
};

} // namespace runamuck
