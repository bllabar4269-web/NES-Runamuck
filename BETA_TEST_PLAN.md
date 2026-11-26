# Beta Test Plan

This is a local test candidate for comparing the existing scanline renderer
with the experimental dot renderer. It is not a claim that the supported games
are fully playable. Keep the scanline renderer as the default unless the visual
checks below pass.

## Build and automated checks

The local Release candidate is:

```text
C:\Users\Macokner\NES Emulator Build\NES++Runamuck\build\beta-mmc2-nmake\runamuck_visual.exe
```

It was built with MSVC/NMake because the configured Ninja executable was
blocked by Windows. It includes the MMC2 sprite-fetch order fix. All five
registered CTest suites pass, including a regression check for MMC2 sprite
bank switching in OAM order. Do not include commercial ROM files in a package.

## Renderer A/B run

Use the same ROM, game state, input sequence, and test duration for both runs.
Wait at least five seconds after reaching the test scene before recording the
window title metrics.

Run Super Dodge Ball with the default scanline renderer:

```powershell
& "C:\Users\Macokner\NES Emulator Build\NES++Runamuck\build\beta-mmc2-nmake\runamuck_visual.exe" "C:\Users\Macokner\NES Emulator Build\Super Dodge Ball\Super Dodge Ball (USA).nes"
```

Run the same ROM with the experimental dot renderer:

```powershell
& "C:\Users\Macokner\NES Emulator Build\NES++Runamuck\build\beta-mmc2-nmake\runamuck_visual.exe" --dot-renderer "C:\Users\Macokner\NES Emulator Build\Super Dodge Ball\Super Dodge Ball (USA).nes"
```

Repeat the two runs with Mike Tyson's Punch-Out!!:

```powershell
& "C:\Users\Macokner\NES Emulator Build\NES++Runamuck\build\beta-mmc2-nmake\runamuck_visual.exe" "C:\Users\Macokner\NES Emulator Build\Mike Tyson's Punch-Out!! (Japan, USA) (Rev 1).nes"
```

```powershell
& "C:\Users\Macokner\NES Emulator Build\NES++Runamuck\build\beta-mmc2-nmake\runamuck_visual.exe" --dot-renderer "C:\Users\Macokner\NES Emulator Build\Mike Tyson's Punch-Out!! (Japan, USA) (Rev 1).nes"

For MMC1 gameplay diagnostics, open PowerShell in the repository folder and add
`--trace-mmc1` to a dot-renderer launch. The runner writes MMC1 serial writes,
PPU mask/scroll/address writes, OAM DMA events, active OAM entries after each
DMA, and a per-frame sprite fetch/render sample to a log in the current folder,
named after the ROM. Stop the run after reaching the broken gameplay screen so
the log is closed and flushed.
```

Record the window-title values for OAM sprites, rendered sprite pixels, PPU
mask, loop FPS, and frame work milliseconds. Loop FPS includes the runner's
frame pacing sleep; frame work milliseconds measures CPU and PPU work before
that sleep.

## Visual checks

For Super Dodge Ball, check the title/team menus, full court, both teams, ball,
HUD, scrolling, and Start behavior. For Punch-Out!!, check the menu, Little Mac,
the opponent, and any mid-frame background splits. For both renderers, note
missing or torn sprites, flashing, scrambled tiles, frozen frames, or input
problems. Capture a screenshot and the title metrics for any failure.

## Results

| ROM / scene | Renderer | OAM sprites | Sprite pixels | PPU mask | Loop FPS | Frame work ms | Visual result |
|---|---|---:|---:|---:|---:|---:|---|
| Super Dodge Ball / match | Scanline | 9 | 310 | 24 | | | Fails visually: no recognizable characters; only half the court background is clean |
| Super Dodge Ball / match | Dot | 1 | 2 | 24 | 53.4 | 1.0 | Fails: no visible characters; only half the court background is clean |
| Super Dodge Ball / second menu | Dot | 0–9 | 0–347 | 24 | 53.1 | 1.0 | Sprites sometimes animate behind the menu; match scene has missing characters |
| Punch-Out!! / opponent | Scanline | | | | | | |
| Punch-Out!! / first three fights | Dot | 61 | 1511 | 30 | 52.7 | 1.0 | Passed; no visible issues |
| Punch-Out!! / fight 1 | Dot | 53 | 200 | 30 | 53.4 | 1.0 | Passed; no visible issues |
| Punch-Out!! / demo | Dot | 41–45 | 2500–3700 | 30 | 52.8 | 1.0 | Passed; no visible issues |

Do not promote the dot renderer to the default until both the automated checks
and these visual comparisons pass without regressions.
