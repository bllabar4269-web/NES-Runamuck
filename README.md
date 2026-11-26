# Runamuck

Runamuck is a Nintendo Entertainment System (NES) emulator project written in
C++20.

**Status:** Early development; this is not yet a playable emulator. The PPU,
controller input, SDL frontend, and executable runner are not implemented.

## Compatibility goal

The long-term goal is to play NES ROMs broadly, not just games using NROM.
Cartridge boards use different mappers, so the cartridge system must support
separate, extensible mapper implementations. We will add and validate mapper
support incrementally, prioritize the boards needed by the available test
games, and report unsupported mapper IDs clearly.

## Current status

- MOS 6502 CPU core with instruction stepping, status flags, and interrupt
  handling.
- CPU unit tests covering basic instructions, arithmetic flags, branches, and
  interrupt status.
- CMake build configuration and debug/release presets.
- NES CPU bus with 2 KB internal RAM mirrored across `0x0000-0x1FFF`; unmapped
  addresses currently return the open-bus value.
- CPU and NES bus tests pass, including internal RAM mirror checks.
- iNES 1.0 cartridge loading reads PRG/CHR data, records the mapper ID, and
  allocates CHR RAM when needed; cartridge tests cover these cases and invalid
  signatures. NROM, MMC1, and MMC2 mapper support is implemented; NES 2.0
  parsing is not yet implemented.
- Mapper interface defines CPU/PPU address mapping, nametable mirroring, and
  optional clock/IRQ hooks. NROM address mapping is connected to cartridge
  loading and both CPU and PPU buses.
- PPU memory, registers, nametable mirroring, background/sprite rendering,
  sprite-zero-hit, VBlank timing, and a native Win32 visual test runner are
  implemented.
- Rendering currently has a scanline path used by default and an experimental
  per-pixel path selected with `--dot-renderer`. The experimental path now
  clocks background nametable, attribute, and pattern fetches through shifters,
  with horizontal and vertical scroll updates on their PPU dots. Sprite
  CHR data is fetched in ascending OAM order to preserve MMC2 latch behavior;
  a regression test covers this ordering. Sprite evaluation/fetch timing and
  CPU-to-PPU sub-instruction timing still need work. Keep the scanline path
  available as a fallback while improving the timed path.
- NES controller ports support two serial gamepads. The visual runner uses SDL3
  for Xbox and PlayStation-compatible controllers, with keyboard fallback
  (arrows, Z/X, right Shift, Enter).
- The visual runner window title reports OAM sprite counts, rendered sprite
  pixels, the PPU mask, loop FPS, and CPU/PPU frame-work time for renderer A/B
  checks.
- Three test games are available for compatibility testing: Super Dodge Ball,
  Bases Loaded 2, and Mike Tyson's Punch-Out!!. Gameplay compatibility remains
  incomplete.

## Build and test

The presets use Ninja. With CMake and Ninja installed and available on `PATH`,
the first configure downloads the pinned SDL3 release for the Windows visual
runner:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Roadmap

1. Resolve the MMC1 sprite and character visibility problems in Super Dodge
   Ball and Bases Loaded 2. Punch-Out!! has passed the first three fights and
   demo checks on the dot renderer; keep comparing representative scenes and
   recording the window metrics.
2. Add focused regression coverage for the MMC1, OAM DMA, and sprite-rendering
   behavior implicated by those games.
3. Improve dot-renderer timing, especially per-dot sprite evaluation/fetch and
   CPU-to-PPU sub-instruction timing, while preserving scanline-renderer
   compatibility.
4. Add mapper 2 and additional board variants needed by the compatibility ROMs.
5. Validate against the available games and broader ROM test suites before
   considering the dot renderer as the default.

`cpu_tests`, `nes_bus_tests`, `cartridge_tests`, `nrom_mapper_tests`, and
`ppu_tests` are registered with CTest.

## Performance guardrails

The earlier emulator showed that both CPU work and rendering can limit
throughput. A cross-frame colored-tile cache regressed gameplay when cache-miss
overhead and CHR churn outweighed reuse; per-scanline palette reuse performed
better. For this emulator:

- Profile representative ROM gameplay before optimizing, and benchmark the
  same deterministic input replay before and after each change.
- Measure CPU and rendering costs separately; CPU timing may include bus and
  PPU advancement, so do not double-count it with PPU time.
- Keep caches bounded, invalidate them on source changes, and measure miss-path
  overhead as well as cache hits.
- Preserve behavior with CPU/PPU state and rendered-frame tests alongside
  performance measurements.

## PPU renderer work

The existing scanline renderer is the compatibility fallback and should remain
available while the timed renderer is developed. `--dot-renderer` selects the
experimental path, which now models background tile fetches and shift-register
output per dot. It is still not a fully cycle-accurate PPU: sprite evaluation
and fetches remain scanline-based, and CPU register writes are applied at
instruction boundaries.

The next renderer work should validate horizontal/vertical scrolling and
mid-frame scroll splits against real game frames, then improve per-dot sprite
evaluation/fetch timing and CPU/PPU phase alignment. PPU bus accesses must
continue through `PpuBus` so cartridge CHR banking and mapper-controlled
nametable mirroring remain active. A focused test covers dot-timed background
fetching; the scanline renderer remains the default until game compatibility is
confirmed.
