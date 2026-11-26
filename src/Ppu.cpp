#include "runamuck/nes/Ppu.hpp"

#include "runamuck/nes/Cartridge.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <ostream>
#include <utility>

namespace runamuck {

namespace {

constexpr std::array<std::uint32_t, 64> nesPalette = {
    0xff545454, 0xff001e74, 0xff081090, 0xff301088,
    0xff440064, 0xff5c0030, 0xff540400, 0xff3c1800,
    0xff202a00, 0xff083a00, 0xff004000, 0xff003c00,
    0xff00323c, 0xff000000, 0xff000000, 0xff000000,
    0xff989898, 0xff084cc4, 0xff3032ec, 0xff5c1ee4,
    0xff8814b0, 0xffa01464, 0xff982220, 0xff783c00,
    0xff5e6600, 0xff2c7c00, 0xff008400, 0xff007c38,
    0xff006e8c, 0xff000000, 0xff000000, 0xff000000,
    0xffececec, 0xff4c9cff, 0xff7878ff, 0xffb46cfc,
    0xffe45cf4, 0xfff45ca4, 0xfff46c7c, 0xffea9040,
    0xffc0c000, 0xff94d400, 0xff74e400, 0xff64dc88,
    0xff64ccc0, 0xff808080, 0xff000000, 0xff000000,
    0xfffcfcfc, 0xffa0dcff, 0xffbcbcff, 0xffe0b0ff,
    0xfff0acf4, 0xfff4b0d0, 0xfff4b0b0, 0xfff4c8a0,
    0xffe8e8a0, 0xffd8f48c, 0xffb4f4b0, 0xffb4f4d0,
    0xffb4f4e0, 0xffbcbcbc, 0xff000000, 0xff000000
};

} // namespace

Ppu::Ppu() {
    reset();
}

void Ppu::setCartridge(std::shared_ptr<Cartridge> cartridge) {
    bus_.setCartridge(std::move(cartridge));
}

void Ppu::setVblankCallback(VblankCallback callback) {
    vblankCallback_ = std::move(callback);
}

void Ppu::setDmaCallback(DmaCallback callback) {
    dmaCallback_ = std::move(callback);
}

bool Ppu::consumeFrameComplete() noexcept {
    const bool complete = frameComplete_;
    frameComplete_ = false;
    return complete;
}

std::uint8_t Ppu::readStatus() {
    const std::uint8_t result = status_;
    status_ &= static_cast<std::uint8_t>(~0x80u);
    writeToggle_ = false;
    if (scanline_ == 241 && cycle_ == 0) {
        suppressNmiThisFrame_ = true;
    }
    return result;
}

std::uint8_t Ppu::readRegister(std::uint16_t address) {
    switch (address & 0x0007u) {
    case 2:
        return readStatus();
    case 4:
        return oam_[oamAddress_];
    case 7: {
        const std::uint16_t ppuAddress = static_cast<std::uint16_t>(v_ & 0x3fffu);
        const bool paletteRead = (ppuAddress & 0x3f00u) == 0x3f00u;
        const std::uint8_t result = paletteRead ? bus_.read(ppuAddress) : readBuffer_;
        readBuffer_ = bus_.read(static_cast<std::uint16_t>(ppuAddress - (paletteRead ? 0x1000 : 0)));
        incrementAddress();
        return result;
    }
    default:
        return 0;
    }
}

void Ppu::writeRegister(std::uint16_t address, std::uint8_t value) {
    const std::uint16_t registerAddress = static_cast<std::uint16_t>(0x2000u | (address & 0x0007u));
    if (traceStream_ && (registerAddress == 0x2000u ||
                         registerAddress == 0x2001u ||
                         registerAddress == 0x2005u ||
                         registerAddress == 0x2006u || address == 0x4014u)) {
        *traceStream_ << "PPU write scanline=" << scanline_ << " dot=" << cycle_
                      << " addr=$" << std::hex << address
                      << " data=$" << static_cast<unsigned>(value)
                      << " v=$" << v_ << " t=$" << t_
                      << " fineX=$" << static_cast<unsigned>(fineX_)
                      << " toggle=" << (writeToggle_ ? 1 : 0)
                      << std::dec << '\n';
    }
    if (address == 0x4014) {
        std::array<std::uint8_t, 256> dmaSource{};
        if (dmaCallback_) {
            const std::uint16_t base = static_cast<std::uint16_t>(value) << 8;
            for (std::uint16_t i = 0; i < 256; ++i) {
                dmaSource[i] = dmaCallback_(static_cast<std::uint16_t>(base + i));
            }
            for (std::uint16_t i = 0; i < 256; ++i) {
                oam_[static_cast<std::uint8_t>(oamAddress_ + i)] = dmaSource[i];
            }
        }
        if (traceStream_) {
            const auto oldFlags = traceStream_->flags();
            const auto oldFill = traceStream_->fill();
            *traceStream_ << "OAM DMA source page=$" << std::hex
                          << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(value) << " bytes=";
            for (const auto byte : dmaSource) {
                *traceStream_ << std::setw(2) << static_cast<unsigned>(byte);
            }
            *traceStream_ << '\n' << "OAM DMA result page=$" << std::hex
                          << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(value)
                          << " start=$" << std::setw(2)
                          << static_cast<unsigned>(oamAddress_)
                          << std::dec << " active=" << activeSpriteCount()
                          << " bytes=";
            for (const auto byte : oam_) {
                *traceStream_ << std::hex << std::setw(2)
                              << static_cast<unsigned>(byte);
            }
            *traceStream_ << std::dec << '\n';
            traceStream_->flags(oldFlags);
            traceStream_->fill(oldFill);
        }
        return;
    }

    switch (address & 0x0007u) {
    case 0: {
        const bool nmiWasEnabled = (control_ & 0x80u) != 0;
        control_ = value;
        t_ = static_cast<std::uint16_t>((t_ & 0xf3ffu) | ((value & 0x03u) << 10));
        if (!nmiWasEnabled && (control_ & 0x80u) != 0 &&
            (status_ & 0x80u) != 0 && !suppressNmiThisFrame_ && vblankCallback_) {
            vblankCallback_();
        }
        break;
    }
    case 1:
        mask_ = value;
        break;
    case 3:
        oamAddress_ = value;
        break;
    case 4:
        oam_[oamAddress_++] = value;
        break;
    case 5:
        if (!writeToggle_) {
            scrollX_ = value;
            fineX_ = value & 0x07u;
            t_ = static_cast<std::uint16_t>((t_ & 0xffe0u) | (value >> 3));
            if (scanline_ < height && cycle_ >= 1 && cycle_ <= width) {
                backgroundAnchorX_ = static_cast<int>(cycle_);
            }
        } else {
            scrollY_ = value;
            t_ = static_cast<std::uint16_t>(
                (t_ & 0x0c1fu) |
                ((static_cast<std::uint16_t>(value) & 0x07u) << 12) |
                ((static_cast<std::uint16_t>(value) & 0xf8u) << 2));
        }
        writeToggle_ = !writeToggle_;
        break;
    case 6:
        if (!writeToggle_) {
            t_ = static_cast<std::uint16_t>(
                (t_ & 0x00ffu) | ((static_cast<std::uint16_t>(value) & 0x3fu) << 8));
        } else {
            t_ = static_cast<std::uint16_t>((t_ & 0xff00u) | value);
            v_ = t_;
            if (scanline_ < height && cycle_ >= 1 && cycle_ <= width) {
                backgroundStartV_ = v_;
                backgroundAnchorX_ = static_cast<int>(cycle_);
                cachedBackgroundAddress_ = 0xffff;
            }
        }
        writeToggle_ = !writeToggle_;
        break;
    case 7:
        bus_.write(static_cast<std::uint16_t>(v_ & 0x3fffu), value);
        incrementAddress();
        break;
    default:
        break;
    }
}

void Ppu::incrementAddress() {
    v_ = static_cast<std::uint16_t>((v_ + ((control_ & 0x04u) ? 32u : 1u)) & 0x7fffu);
}

void Ppu::reset() {
    control_ = mask_ = status_ = oamAddress_ = 0;
    writeToggle_ = false;
    fineX_ = 0;
    scrollX_ = scrollY_ = 0;
    v_ = t_ = 0;
    readBuffer_ = 0;
    backgroundPatternShiftLow_ = 0;
    backgroundPatternShiftHigh_ = 0;
    backgroundAttributeShiftLow_ = 0;
    backgroundAttributeShiftHigh_ = 0;

    nextTileId_ = 0;
    nextTileAttribute_ = 0;
    nextTileLow_ = 0;
    nextTileHigh_ = 0;
    scanline_ = 0;
    cycle_ = 0;
    frameComplete_ = false;
    suppressNmiThisFrame_ = false;
    startupScrollPending_ = true;
    frameSpritePixelCount_ = lastFrameSpritePixelCount_ = 0;
    scanlineSpriteBufferedPixelCount_ = 0;
    scanlineSpriteRenderedPixelCount_ = 0;
    frameSpriteBufferedPixelCount_ = 0;
    frameSpriteScanlineCount_ = 0;
    scanlineSelectedSpriteCount_ = 0;
    traceSpriteLinePending_ = false;
    traceSpriteLineLoggedThisFrame_ = false;
    oam_.fill(0xff);
    framebuffer_.fill(nesPalette[0]);
}

std::size_t Ppu::activeSpriteCount() const noexcept {
    std::size_t count = 0;
    for (std::size_t sprite = 0; sprite < 64; ++sprite) {
        if (oam_[sprite * 4] < 0xefu) {
            ++count;
        }
    }
    return count;
}

std::uint8_t Ppu::debugPeekPpuMemory(std::uint16_t address) {
    return bus_.peek(static_cast<std::uint16_t>(address & 0x3fffu));
}

std::optional<std::size_t> Ppu::debugChrBank(std::uint16_t address) {
    return bus_.peekChrBank(address);
}

void Ppu::writeOamByte(std::size_t index, std::uint8_t value) noexcept {
    oam_[index & 0xffu] = value;
}

std::uint32_t Ppu::paletteColor(std::uint8_t paletteIndex) {
    const std::uint8_t color = bus_.read(static_cast<std::uint16_t>(0x3f00u + paletteIndex)) & 0x3fu;
    return nesPalette[color];
}

void Ppu::renderScanline() {
    if (scanline_ < 0 || scanline_ >= height) {
        return;
    }
    sprite0HitCycle_ = 0xffffffffu;
    scanlineSpriteBufferedPixelCount_ = 0;
    scanlineSpriteRenderedPixelCount_ = 0;
    scanlineSelectedSpriteCount_ = 0;
    spriteOpaque_.fill(false);
    spriteBehindBackground_.fill(false);
    spriteZeroOpaque_.fill(false);
    spritePaletteIndex_.fill(0);
    if (dotRendererEnabled_) {
        renderBackground(scanline_);
    } else {
        renderBackgroundScanline(scanline_);
    }
    renderSprites(scanline_);
    if (!dotRendererEnabled_) {
        const std::size_t row = static_cast<std::size_t>(scanline_) * width;
        for (std::size_t x = 0; x < width; ++x) {
            if (!spriteOpaque_[x]) {
                continue;
            }
            if (spriteZeroOpaque_[x] && backgroundOpaque_[x] &&
                (mask_ & 0x18u) == 0x18u && x < width - 1 &&
                (x >= 8 || (mask_ & 0x06u) == 0x06u)) {
                sprite0HitCycle_ = std::min(
                    sprite0HitCycle_, static_cast<std::uint32_t>(x + 1));
            }
            if (spriteBehindBackground_[x] && backgroundOpaque_[x]) {
                continue;
            }
            framebuffer_[row + x] = paletteColor(spritePaletteIndex_[x]);
            ++frameSpritePixelCount_;
            ++scanlineSpriteRenderedPixelCount_;
        }
    }
}

void Ppu::renderBackground(int scanline) {
    const std::size_t row = static_cast<std::size_t>(scanline) * width;
    const std::uint32_t backdrop = paletteColor(0);
    std::fill_n(framebuffer_.begin() + static_cast<std::ptrdiff_t>(row), width, backdrop);
    backgroundOpaque_.fill(false);
    cachedBackgroundAddress_ = 0xffff;
    cachedBackgroundFineY_ = 0xff;
    cachedBackgroundPatternBase_ = 0xff;
    backgroundStartV_ = v_;
    backgroundAnchorX_ = 0;
}

void Ppu::renderBackgroundScanline(int scanline) {
    const std::size_t row = static_cast<std::size_t>(scanline) * width;
    const std::uint32_t backdrop = paletteColor(0);
    std::fill_n(framebuffer_.begin() + static_cast<std::ptrdiff_t>(row), width, backdrop);
    backgroundOpaque_.fill(false);

    if ((mask_ & 0x08u) == 0) {
        return;
    }

    const std::uint16_t patternBase = (control_ & 0x10u) ? 0x1000u : 0x0000u;
    const int worldY = static_cast<int>(scrollY_) + scanline;
    const int baseNametableX = control_ & 0x01u;
    const int baseNametableY = (control_ >> 1) & 0x01u;
    const int nametableY = baseNametableY ^ ((worldY / 240) & 1);
    const int tileY = (worldY / 8) % 30;
    const int fineY = worldY & 0x07;

    std::uint16_t cachedNametableAddress = 0xffff;
    std::uint8_t cachedLow = 0;
    std::uint8_t cachedHigh = 0;
    std::uint8_t cachedPalette = 0;
    int cachedFineY = -1;
    for (int screenX = 0; screenX < width; ++screenX) {
        if (screenX < 8 && (mask_ & 0x02u) == 0) {
            continue;
        }

        const int worldX = static_cast<int>(scrollX_) + screenX;
        const int nametableX = baseNametableX ^ ((worldX / width) & 1);
        const int tileX = (worldX / 8) & 31;
        const int fineX = worldX & 0x07;
        const std::uint16_t nametableBase = static_cast<std::uint16_t>(
            0x2000u + (nametableY * 2 + nametableX) * 0x0400u);
        const std::uint16_t nametableAddress = static_cast<std::uint16_t>(
            nametableBase + tileY * 32 + tileX);

        if (nametableAddress != cachedNametableAddress || fineY != cachedFineY) {
            const std::uint8_t tile = bus_.read(nametableAddress);
            const std::uint16_t patternAddress = static_cast<std::uint16_t>(
                patternBase + static_cast<std::uint16_t>(tile) * 16u +
                static_cast<std::uint16_t>(fineY));
            cachedLow = bus_.read(patternAddress);
            cachedHigh = bus_.read(static_cast<std::uint16_t>(patternAddress + 8u));

            const int attributeX = tileX >> 2;
            const int attributeY = tileY >> 2;
            const std::uint16_t attributeAddress = static_cast<std::uint16_t>(
                nametableBase + 0x03c0u + attributeY * 8 + attributeX);
            const std::uint8_t attributes = bus_.read(attributeAddress);
            const unsigned shift = static_cast<unsigned>(((tileY & 2) << 1) | (tileX & 2));
            cachedPalette = static_cast<std::uint8_t>((attributes >> shift) & 0x03u);
            cachedNametableAddress = nametableAddress;
            cachedFineY = fineY;
        }

        const unsigned bit = 7u - static_cast<unsigned>(fineX);
        const std::uint8_t pixel = static_cast<std::uint8_t>(
            ((cachedLow >> bit) & 1u) | (((cachedHigh >> bit) & 1u) << 1));
        if (pixel != 0) {
            backgroundOpaque_[static_cast<std::size_t>(screenX)] = true;
            framebuffer_[row + static_cast<std::size_t>(screenX)] =
                paletteColor(static_cast<std::uint8_t>(cachedPalette * 4u + pixel));
        }
    }
}

void Ppu::renderBackgroundPixel(int screenX) {
    const std::size_t x = static_cast<std::size_t>(screenX);
    const bool backgroundVisible =
        (mask_ & 0x08u) != 0 &&
        (screenX >= 8 || (mask_ & 0x02u) != 0);

    std::uint8_t pixel = 0;
    std::uint8_t palette = 0;

    if (backgroundVisible) {
        const std::uint16_t bit = static_cast<std::uint16_t>(0x8000u >> fineX_);

        pixel = static_cast<std::uint8_t>(
            ((backgroundPatternShiftLow_ & bit) != 0 ? 1u : 0u) |
            ((backgroundPatternShiftHigh_ & bit) != 0 ? 2u : 0u));

        palette = static_cast<std::uint8_t>(
            ((backgroundAttributeShiftLow_ & bit) != 0 ? 1u : 0u) |
            ((backgroundAttributeShiftHigh_ & bit) != 0 ? 2u : 0u));

        backgroundOpaque_[x] = pixel != 0;

        if (pixel != 0) {
            const std::size_t framebufferIndex =
                static_cast<std::size_t>(scanline_) * width + x;
            framebuffer_[framebufferIndex] =
                paletteColor(static_cast<std::uint8_t>(palette * 4u + pixel));
        }
    } else {
        backgroundOpaque_[x] = false;
    }

    if (spriteOpaque_[x] && spriteZeroOpaque_[x] && pixel != 0 &&
        (mask_ & 0x18u) == 0x18u && screenX < width - 1 &&
        (screenX >= 8 || (mask_ & 0x06u) == 0x06u)) {
        status_ |= 0x40u;
    }

    if (spriteOpaque_[x] &&
        (!spriteBehindBackground_[x] || pixel == 0)) {
        const std::size_t framebufferIndex =
            static_cast<std::size_t>(scanline_) * width + x;
        framebuffer_[framebufferIndex] =
            paletteColor(spritePaletteIndex_[x]);
        ++frameSpritePixelCount_;
        ++scanlineSpriteRenderedPixelCount_;
    }
}

void Ppu::renderSprites(int scanline) {
    if ((mask_ & 0x10u) == 0) {
        return;
    }

    const bool tall = (control_ & 0x20u) != 0;
    const int spriteHeight = tall ? 16 : 8;
    std::array<int, 8> selected{};
    int selectedCount = 0;
    for (int sprite = 0; sprite < 64; ++sprite) {
        const int top = static_cast<int>(oam_[static_cast<std::size_t>(sprite) * 4]) + 1;
        if (scanline >= top && scanline < top + spriteHeight) {
            if (selectedCount == 8) {
                status_ |= 0x20u;
                break;
            }
            selected[static_cast<std::size_t>(selectedCount++)] = sprite;
        }
    }
    scanlineSelectedSpriteCount_ = static_cast<std::size_t>(selectedCount);
    if (selectedCount > 0) {
        ++frameSpriteScanlineCount_;
    }

    for (int selectedIndex = 0; selectedIndex < selectedCount; ++selectedIndex) {
        const int sprite = selected[static_cast<std::size_t>(selectedIndex)];
        const std::size_t base = static_cast<std::size_t>(sprite) * 4;
        const int top = static_cast<int>(oam_[base]) + 1;
        const std::uint8_t tile = oam_[base + 1];
        const std::uint8_t attributes = oam_[base + 2];
        const int spriteX = oam_[base + 3];
        int sourceY = scanline - top;
        if ((attributes & 0x80u) != 0) {
            sourceY = spriteHeight - 1 - sourceY;
        }

        std::uint16_t patternAddress;
        if (tall) {
            const std::uint16_t table = (tile & 1u) ? 0x1000u : 0x0000u;
            const std::uint8_t firstTile = tile & 0xfeu;
            const std::uint8_t selectedTile = static_cast<std::uint8_t>(firstTile + (sourceY >= 8 ? 1 : 0));
            patternAddress = static_cast<std::uint16_t>(table + selectedTile * 16u + (sourceY & 7));
        } else {
            const std::uint16_t table = (control_ & 0x08u) ? 0x1000u : 0x0000u;
            patternAddress = static_cast<std::uint16_t>(table + tile * 16u + sourceY);
        }

        const std::uint8_t low = bus_.read(patternAddress);
        const std::uint8_t high = bus_.read(static_cast<std::uint16_t>(patternAddress + 8u));
        for (int column = 0; column < 8; ++column) {
            const int x = spriteX + column;
            if (x < 0 || x >= width || (x < 8 && (mask_ & 0x04u) == 0)) {
                continue;
            }
            const int bit = (attributes & 0x40u) ? column : (7 - column);
            const std::uint8_t pixel = static_cast<std::uint8_t>(
                ((low >> bit) & 1u) | (((high >> bit) & 1u) << 1));
            if (pixel == 0) {
                continue;
            }

            const std::size_t screenIndex = static_cast<std::size_t>(x);
            if (spriteOpaque_[screenIndex]) {
                continue;
            }
            if (sprite == 0) {
                spriteZeroOpaque_[screenIndex] = true;
            }
            spriteOpaque_[screenIndex] = true;
            spriteBehindBackground_[screenIndex] = (attributes & 0x20u) != 0;
            spritePaletteIndex_[screenIndex] =
                static_cast<std::uint8_t>(0x10u + (attributes & 0x03u) * 4u + pixel);
            ++scanlineSpriteBufferedPixelCount_;
            ++frameSpriteBufferedPixelCount_;
        }
    }

       if (traceStream_ && selectedCount > 0 && !traceSpriteLineLoggedThisFrame_) {
        traceSpriteLineLoggedThisFrame_ = true;
        traceSpriteLinePending_ = true;
    }
}

void Ppu::loadBackgroundShifters() {
    backgroundPatternShiftLow_ =
        static_cast<std::uint16_t>((backgroundPatternShiftLow_ & 0xff00u) | nextTileLow_);
    backgroundPatternShiftHigh_ =
        static_cast<std::uint16_t>((backgroundPatternShiftHigh_ & 0xff00u) | nextTileHigh_);

    backgroundAttributeShiftLow_ = static_cast<std::uint16_t>(
        (backgroundAttributeShiftLow_ & 0xff00u) |
        ((nextTileAttribute_ & 0x01u) ? 0x00ffu : 0x0000u));
    backgroundAttributeShiftHigh_ = static_cast<std::uint16_t>(
        (backgroundAttributeShiftHigh_ & 0xff00u) |
        ((nextTileAttribute_ & 0x02u) ? 0x00ffu : 0x0000u));
}

void Ppu::incrementScrollX() {
    if ((v_ & 0x001fu) == 31u) {
        v_ &= static_cast<std::uint16_t>(~0x001fu);
        v_ ^= 0x0400u;
    } else {
        ++v_;
    }
}

void Ppu::incrementScrollY() {
    if ((v_ & 0x7000u) != 0x7000u) {
        v_ += 0x1000u;
        return;
    }

    v_ &= static_cast<std::uint16_t>(~0x7000u);
    unsigned coarseY = (v_ & 0x03e0u) >> 5;

    if (coarseY == 29u) {
        coarseY = 0;
        v_ ^= 0x0800u;
    } else if (coarseY == 31u) {
        coarseY = 0;
    } else {
        ++coarseY;
    }

    v_ = static_cast<std::uint16_t>(
        (v_ & static_cast<std::uint16_t>(~0x03e0u)) | (coarseY << 5));
}

void Ppu::clockBackgroundPipeline() {
    const bool visibleScanline = scanline_ >= 0 && scanline_ < height;
    const bool renderingScanline = visibleScanline || scanline_ == 261;
    const bool renderingEnabled = (mask_ & 0x18u) != 0;
    const bool fetchWindow =
        (cycle_ >= 1 && cycle_ <= 256) ||
        (cycle_ >= 321 && cycle_ <= 336);

    if (!renderingScanline || !renderingEnabled || !fetchWindow) {
        return;
    }

    if ((cycle_ & 0x07u) == 1u) {
        loadBackgroundShifters();
    }

    switch (cycle_ & 0x07u) {
    case 1:
        nextTileId_ = bus_.read(
            static_cast<std::uint16_t>(0x2000u | (v_ & 0x0fffu)));
        break;

    case 3: {
        const std::uint16_t attributeAddress = static_cast<std::uint16_t>(
            0x23c0u |
            (v_ & 0x0c00u) |
            ((v_ >> 4) & 0x0038u) |
            ((v_ >> 2) & 0x0007u));
        const std::uint8_t attributes = bus_.read(attributeAddress);
        const unsigned shift =
            static_cast<unsigned>(((v_ >> 4) & 0x04u) | (v_ & 0x02u));
        nextTileAttribute_ =
            static_cast<std::uint8_t>((attributes >> shift) & 0x03u);
        break;
    }

    case 5: {
        const std::uint16_t patternBase =
            (control_ & 0x10u) ? 0x1000u : 0x0000u;
        const std::uint16_t fineY = (v_ >> 12) & 0x07u;
        const std::uint16_t patternAddress = static_cast<std::uint16_t>(
            patternBase + static_cast<std::uint16_t>(nextTileId_) * 16u + fineY);
        nextTileLow_ = bus_.read(patternAddress);
        break;
    }

    case 7: {
        const std::uint16_t patternBase =
            (control_ & 0x10u) ? 0x1000u : 0x0000u;
        const std::uint16_t fineY = (v_ >> 12) & 0x07u;
        const std::uint16_t patternAddress = static_cast<std::uint16_t>(
            patternBase + static_cast<std::uint16_t>(nextTileId_) * 16u +
            fineY + 8u);
        nextTileHigh_ = bus_.read(patternAddress);
        break;
    }

    case 0:
        incrementScrollX();
        break;

    default:
        break;
    }
}

void Ppu::clock(std::uint32_t cycles) {
    for (std::uint32_t i = 0; i < cycles; ++i) {
        ++cycle_;
        const bool rendering = (mask_ & 0x18u) != 0;

        if (dotRendererEnabled_ && scanline_ == 261) {
            clockBackgroundPipeline();
            if (rendering && ((cycle_ >= 1 && cycle_ <= 256) || (cycle_ >= 321 && cycle_ <= 336))) {
                backgroundPatternShiftLow_ <<= 1;
                backgroundPatternShiftHigh_ <<= 1;
                backgroundAttributeShiftLow_ <<= 1;
                backgroundAttributeShiftHigh_ <<= 1;
            }
        }

        if (scanline_ < height) {
            if (cycle_ == 1) {
                if (dotRendererEnabled_) {
                    renderBackground(scanline_);
                } else {
                    renderScanline();
                }
            }

            if (dotRendererEnabled_) {
                clockBackgroundPipeline();
            }

            if (dotRendererEnabled_ && cycle_ >= 1 && cycle_ <= width) {
                renderBackgroundPixel(static_cast<int>(cycle_ - 1));
                if (rendering) {
                    backgroundPatternShiftLow_ <<= 1;
                    backgroundPatternShiftHigh_ <<= 1;
                    backgroundAttributeShiftLow_ <<= 1;
                    backgroundAttributeShiftHigh_ <<= 1;
                }
                if (cycle_ == width && traceStream_ && traceSpriteLinePending_) {
                    *traceStream_ << "Sprite sample scanline=" << scanline_
                                  << " mask=$" << std::hex
                                  << static_cast<unsigned>(mask_) << std::dec
                                  << " selected=" << scanlineSelectedSpriteCount_
                                  << " buffered=" << scanlineSpriteBufferedPixelCount_
                                  << " rendered=" << scanlineSpriteRenderedPixelCount_
                                  << '\n';
                    traceSpriteLinePending_ = false;
                }
            } else if (dotRendererEnabled_ && rendering && cycle_ >= 321 && cycle_ <= 336) {
                backgroundPatternShiftLow_ <<= 1;
                backgroundPatternShiftHigh_ <<= 1;
                backgroundAttributeShiftLow_ <<= 1;
                backgroundAttributeShiftHigh_ <<= 1;
            }

            if (!dotRendererEnabled_ && cycle_ == sprite0HitCycle_) {
                status_ |= 0x40u;
            }
        }

        if (rendering && (scanline_ < height || scanline_ == 261)) {
            if (cycle_ == 256) {
                incrementScrollY();
            }

            if (cycle_ == 257) {
                v_ = static_cast<std::uint16_t>((v_ & ~0x041fu) | (t_ & 0x041fu));
            }
        }

        if (dotRendererEnabled_ && cycle_ == 257) {
            scanlineSpriteBufferedPixelCount_ = 0;
            scanlineSpriteRenderedPixelCount_ = 0;
            scanlineSelectedSpriteCount_ = 0;
            spriteOpaque_.fill(false);
            spriteBehindBackground_.fill(false);
            spriteZeroOpaque_.fill(false);
            spritePaletteIndex_.fill(0);
            if (scanline_ == 261) {
                renderSprites(0);
            } else if (scanline_ >= 0 && scanline_ < height - 1) {
                renderSprites(scanline_ + 1);
            }
        }

        if (scanline_ == 241 && cycle_ == 1) {
            status_ |= 0x80u;
            if ((control_ & 0x80u) != 0 && !suppressNmiThisFrame_ && vblankCallback_) {
                vblankCallback_();
            }
        }

        if (scanline_ == 261) {
            if (cycle_ == 1) {
                status_ &= static_cast<std::uint8_t>(~0xe0u);
                suppressNmiThisFrame_ = false;
            }
            if (rendering && cycle_ >= 280 && cycle_ <= 304) {
                v_ = static_cast<std::uint16_t>((v_ & ~0x7be0u) | (t_ & 0x7be0u));
            }
        }

        if (cycle_ >= 341) {
            cycle_ = 0;
            ++scanline_;
            if (scanline_ >= 262) {
                scanline_ = 0;
                frameComplete_ = true;
                lastFrameSpritePixelCount_ = frameSpritePixelCount_;
                if (traceStream_) {
                    *traceStream_ << "Sprite frame mask=$" << std::hex
                                  << static_cast<unsigned>(mask_) << std::dec
                                  << " active=" << activeSpriteCount()
                                  << " sprite_scanlines=" << frameSpriteScanlineCount_
                                  << " buffered=" << frameSpriteBufferedPixelCount_
                                  << " rendered=" << frameSpritePixelCount_
                                  << '\n';
                }
                frameSpritePixelCount_ = 0;
                frameSpriteBufferedPixelCount_ = 0;
                frameSpriteScanlineCount_ = 0;
                traceSpriteLineLoggedThisFrame_ = false;
            }
        }
    }
}

} // namespace runamuck