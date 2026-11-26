#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>

#include "runamuck/nes/PpuBus.hpp"

namespace runamuck {

class Cartridge;

class Ppu {
public:
    static constexpr int width = 256;
    static constexpr int height = 240;

    using Framebuffer = std::array<std::uint32_t, width * height>;
    using VblankCallback = std::function<void()>;
    using DmaCallback = std::function<std::uint8_t(std::uint16_t)>;

    Ppu();

    void setCartridge(std::shared_ptr<Cartridge> cartridge);
    void setDotRendererEnabled(bool enabled) noexcept { dotRendererEnabled_ = enabled; }
    void setTraceStream(std::ostream* stream) noexcept { traceStream_ = stream; }

    [[nodiscard]] std::uint8_t readRegister(std::uint16_t address);
    [[nodiscard]] std::uint8_t readStatus();
    void writeRegister(std::uint16_t address, std::uint8_t value);

    void clock(std::uint32_t cycles);
    void reset();

    void setVblankCallback(VblankCallback callback);
    void setDmaCallback(DmaCallback callback);

    [[nodiscard]] const Framebuffer& framebuffer() const noexcept { return framebuffer_; }
    [[nodiscard]] bool consumeFrameComplete() noexcept;
    [[nodiscard]] bool frameComplete() const noexcept { return frameComplete_; }
    void acknowledgeFrame() noexcept { frameComplete_ = false; }
    [[nodiscard]] std::uint8_t control() const noexcept { return control_; }
    [[nodiscard]] std::uint8_t mask() const noexcept { return mask_; }
    [[nodiscard]] std::uint8_t renderingMask() const noexcept { return mask_; }
    [[nodiscard]] std::uint8_t status() const noexcept { return status_; }
    [[nodiscard]] std::uint32_t lastFrameSpritePixelCount() const noexcept { return lastFrameSpritePixelCount_; }
    [[nodiscard]] std::size_t activeSpriteCount() const noexcept;
    [[nodiscard]] std::uint8_t debugPeekPpuMemory(std::uint16_t address);
    [[nodiscard]] std::optional<std::size_t> debugChrBank(std::uint16_t address);
    [[nodiscard]] std::uint8_t debugOamByte(std::size_t index) const noexcept {
        return oam_[index & 0xffu];
    }

    void writeOamByte(std::size_t index, std::uint8_t value) noexcept;

private:
    void incrementAddress();
    void renderScanline();
    void renderBackground(int scanline);
    void renderBackgroundScanline(int scanline);
    void renderBackgroundPixel(int screenX);
    void clockBackgroundPipeline();
    void loadBackgroundShifters();
    void incrementScrollX();
    void incrementScrollY();
    void renderSprites(int scanline);
    [[nodiscard]] std::uint32_t paletteColor(std::uint8_t paletteIndex);

    PpuBus bus_{};
    Framebuffer framebuffer_{};
    std::array<std::uint8_t, 256> oam_{};
    std::array<bool, width> backgroundOpaque_{};
    std::array<bool, width> spriteOpaque_{};
    std::array<bool, width> spriteBehindBackground_{};
    std::array<bool, width> spriteZeroOpaque_{};
    std::array<std::uint8_t, width> spritePaletteIndex_{};
    std::uint16_t backgroundPatternShiftLow_ = 0;
    std::uint16_t backgroundPatternShiftHigh_ = 0;
    std::uint16_t backgroundAttributeShiftLow_ = 0;
    std::uint16_t backgroundAttributeShiftHigh_ = 0;

    std::uint8_t nextTileId_ = 0;
    std::uint8_t nextTileAttribute_ = 0;
    std::uint8_t nextTileLow_ = 0;
    std::uint8_t nextTileHigh_ = 0;
    std::uint16_t cachedBackgroundAddress_ = 0xffff;
    std::uint8_t cachedBackgroundFineY_ = 0xff;
    std::uint8_t cachedBackgroundPatternBase_ = 0xff;
    std::uint8_t cachedBackgroundLow_ = 0;
    std::uint8_t cachedBackgroundHigh_ = 0;
    std::uint8_t cachedBackgroundPalette_ = 0;
    std::uint16_t backgroundStartV_ = 0;
    int backgroundAnchorX_ = 0;
    bool dotRendererEnabled_ = false;
    bool startupScrollPending_ = true;
    std::ostream* traceStream_ = nullptr;

    VblankCallback vblankCallback_{};
    DmaCallback dmaCallback_{};

    std::uint8_t control_ = 0;
    std::uint8_t mask_ = 0;
    std::uint8_t status_ = 0;
    std::uint8_t oamAddress_ = 0;

    bool writeToggle_ = false;
    std::uint8_t fineX_ = 0;
    std::uint8_t scrollX_ = 0;
    std::uint8_t scrollY_ = 0;
    std::uint16_t v_ = 0;
    std::uint16_t t_ = 0;
    std::uint8_t readBuffer_ = 0;

    int scanline_ = 0;
    std::uint32_t cycle_ = 0;
    std::uint32_t sprite0HitCycle_ = 0xffffffffu;
    bool frameComplete_ = false;
    bool suppressNmiThisFrame_ = false;
    std::uint32_t frameSpritePixelCount_ = 0;
    std::uint32_t lastFrameSpritePixelCount_ = 0;
    std::uint32_t scanlineSpriteBufferedPixelCount_ = 0;
    std::uint32_t scanlineSpriteRenderedPixelCount_ = 0;
    std::uint32_t frameSpriteBufferedPixelCount_ = 0;
    std::uint32_t frameSpriteScanlineCount_ = 0;
    std::size_t scanlineSelectedSpriteCount_ = 0;
    bool traceSpriteLinePending_ = false;
    bool traceSpriteLineLoggedThisFrame_ = false;
};

} // namespace runamuck
