# Runamuck

Runamuck is a Nintendo Entertainment System (NES) emulator project written in
C++20.

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
  signatures. NES 2.0 parsing and mapper behavior are not yet implemented.
- Mapper interface defines CPU/PPU address mapping, nametable mirroring, and
  optional clock/IRQ hooks. NROM address mapping is implemented and tested but
  is not yet connected to cartridge loading or the system bus.
- SDL3 and PPU are planned but not yet implemented.
- Three test games are available for later integration and compatibility
  testing, once cartridge loading and the PPU are implemented: Super Dodge
  Ball, Bases Loaded 2, and Mike Tyson's Punch-Out!!.

## Build and test

The presets use Ninja. With CMake and Ninja installed and available on `PATH`:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Roadmap

1. Connect the tested NROM mapper to cartridge loading and the CPU/PPU buses.
2. Add mapper support incrementally, including mapper 2 and mapper 9.
3. Implement the PPU and connect cartridge CHR access.
4. Add SDL3 video and input.
5. Improve CPU and whole-system compatibility with the available test games
   and broader ROM test suites.

`cpu_tests`, `nes_bus_tests`, `cartridge_tests`, and `nrom_mapper_tests` are
registered with CTest.

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
