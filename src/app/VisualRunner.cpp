#include "runamuck/cpu/Cpu6502.hpp"
#include "runamuck/nes/Apu.hpp"
#include "runamuck/nes/Cartridge.hpp"
#include "runamuck/nes/NesBus.hpp"
#include "runamuck/nes/Ppu.hpp"

#include <SDL3/SDL.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <array>
#include <fstream>

namespace {

HWND windowHandle = nullptr;
HDC bitmapDc = nullptr;
HBITMAP bitmap = nullptr;
std::uint32_t* pixels = nullptr;
runamuck::Ppu* activePpu = nullptr;
std::array<SDL_Gamepad*, 2> gamepads{};
bool debugReaderVisible = false;
bool debugReaderOamView = true;
std::uint16_t debugVramBase = 0;

void setControllerState(
    runamuck::NesBus& bus,
    std::uint8_t port,
    SDL_Gamepad* gamepad,
    bool keyboard) {
    using runamuck::ControllerButton;
    const auto pressed = [gamepad](SDL_GamepadButton button) {
        return gamepad && SDL_GetGamepadButton(gamepad, button);
    };
    const auto keyDown = [](int key) {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    };

    bus.setControllerButton(
        port, ControllerButton::a,
        pressed(SDL_GAMEPAD_BUTTON_SOUTH) ||
            (keyboard && keyDown('Z')));
    bus.setControllerButton(
        port, ControllerButton::b,
        pressed(SDL_GAMEPAD_BUTTON_EAST) ||
            (keyboard && keyDown('X')));
    bus.setControllerButton(
        port, ControllerButton::select,
        pressed(SDL_GAMEPAD_BUTTON_BACK) ||
            (keyboard && keyDown(VK_RSHIFT)));
    bus.setControllerButton(
        port, ControllerButton::start,
        pressed(SDL_GAMEPAD_BUTTON_START) ||
            (keyboard && keyDown(VK_RETURN)));
    bus.setControllerButton(
        port, ControllerButton::up,
        pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) ||
            (keyboard && keyDown(VK_UP)));
    bus.setControllerButton(
        port, ControllerButton::down,
        pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
            (keyboard && keyDown(VK_DOWN)));
    bus.setControllerButton(
        port, ControllerButton::left,
        pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
            (keyboard && keyDown(VK_LEFT)));
    bus.setControllerButton(
        port, ControllerButton::right,
        pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
            (keyboard && keyDown(VK_RIGHT)));
}

bool openGamepad(SDL_JoystickID id) {
    for (auto* gamepad : gamepads) {
        if (gamepad && SDL_GetGamepadID(gamepad) == id) {
            return true;
        }
    }

    for (auto& gamepad : gamepads) {
        if (!gamepad) {
            gamepad = SDL_OpenGamepad(id);
            if (!gamepad) {
                MessageBoxA(
                    windowHandle,
                    SDL_GetError(),
                    "Could not open game controller",
                    MB_OK | MB_ICONERROR);
                return false;
            }
            return true;
        }
    }
    return true;
}

void closeGamepad(SDL_JoystickID id) {
    for (auto& gamepad : gamepads) {
        if (gamepad && SDL_GetGamepadID(gamepad) == id) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
            return;
        }
    }
}

void pollGamepads() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            if (!openGamepad(event.gdevice.which)) {
                PostQuitMessage(1);
            }
        } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            closeGamepad(event.gdevice.which);
        }
    }
}

void createFramebuffer() {
    bitmapDc = CreateCompatibleDC(nullptr);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = runamuck::Ppu::width;
    info.bmiHeader.biHeight = -runamuck::Ppu::height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bitmap = CreateDIBSection(
        bitmapDc,
        &info,
        DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels),
        nullptr,
        0);
    SelectObject(bitmapDc, bitmap);
}

std::wstring hexByte(std::uint8_t value) {
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring text(2, L'0');
    text[0] = digits[(value >> 4) & 0x0f];
    text[1] = digits[value & 0x0f];
    return text;
}

std::wstring hexAddress(std::uint16_t value) {
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring text(4, L'0');
    for (int index = 3; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = digits[value & 0x0f];
        value >>= 4;
    }
    return text;
}

void drawDebugReader(HDC dc, const RECT& client) {
    HBRUSH background = CreateSolidBrush(RGB(12, 16, 24));
    FillRect(dc, &client, background);
    DeleteObject(background);

    HFONT font = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 232, 245));

    const auto writeLine = [dc](int x, int y, const std::wstring& line) {
        TextOutW(dc, x, y, line.c_str(), static_cast<int>(line.size()));
    };

    if (debugReaderOamView) {
        writeLine(24, 20, L"LIVE PPU OAM READER  |  64 entries, 4 bytes each");
        writeLine(24, 44, L"ID   Y   TILE ATTR  X");
        writeLine(390, 44, L"ID   Y   TILE ATTR  X");
        for (std::size_t row = 0; row < 32; ++row) {
            for (std::size_t column = 0; column < 2; ++column) {
                const std::size_t index = row + column * 32;
                const std::size_t base = index * 4;
                std::wstring line = hexByte(static_cast<std::uint8_t>(index));
                line += L"   ";
                line += hexByte(activePpu->debugOamByte(base));
                line += L"   ";
                line += hexByte(activePpu->debugOamByte(base + 1));
                line += L"   ";
                line += hexByte(activePpu->debugOamByte(base + 2));
                line += L"   ";
                line += hexByte(activePpu->debugOamByte(base + 3));
                writeLine(column == 0 ? 24 : 390, 68 + static_cast<int>(row) * 18, line);
            }
        }
        writeLine(24, 662, L"F1: game view    F2: VRAM view");
    } else {
        const std::uint16_t lastAddress = static_cast<std::uint16_t>(debugVramBase + 0xff);
        writeLine(24, 20, L"LIVE PPU VRAM READER  |  $" + hexAddress(debugVramBase) +
            L"-$" + hexAddress(lastAddress));

        const auto bank0 = activePpu->debugChrBank(0x0000);
        const auto bank1 = activePpu->debugChrBank(0x1000);
        std::wstring bankLine = L"CHR 4 KB windows: PPU $0000 -> ";
        bankLine += bank0 ? L"bank $" + hexByte(static_cast<std::uint8_t>(*bank0)) : L"unmapped";
        bankLine += L"    PPU $1000 -> ";
        bankLine += bank1 ? L"bank $" + hexByte(static_cast<std::uint8_t>(*bank1)) : L"unmapped";
        writeLine(24, 44, bankLine);

        const std::uint8_t control = activePpu->control();
        const std::wstring controlLine = L"PPUCTRL=$" + hexByte(control) +
            L"  background table=$" + hexAddress((control & 0x10u) ? 0x1000 : 0x0000) +
            L"  8x8 sprite table=$" + hexAddress((control & 0x08u) ? 0x1000 : 0x0000);
        writeLine(24, 68, controlLine);

        for (std::uint16_t row = 0; row < 16; ++row) {
            const std::uint16_t address = static_cast<std::uint16_t>(debugVramBase + row * 16);
            std::wstring line = L"$" + hexAddress(address) + L": ";
            for (std::uint16_t column = 0; column < 16; ++column) {
                const auto value = activePpu->debugPeekPpuMemory(
                    static_cast<std::uint16_t>(address + column));
                line += hexByte(value);
                line += L' ';
            }
            writeLine(24, 106 + static_cast<int>(row) * 28, line);
        }
        writeLine(24, 662, L"F1: game view    F2: OAM view    PgUp/PgDn: previous/next $0100 bytes");
    }

    SelectObject(dc, previousFont);
    DeleteObject(font);
}

LRESULT CALLBACK windowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_KEYDOWN && (lParam & (1u << 30)) == 0) {
        if (wParam == VK_F1) {
            debugReaderVisible = !debugReaderVisible;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (debugReaderVisible && wParam == VK_F2) {
            debugReaderOamView = !debugReaderOamView;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (debugReaderVisible && !debugReaderOamView &&
            (wParam == VK_PRIOR || wParam == VK_NEXT)) {
            if (wParam == VK_PRIOR) {
                debugVramBase = debugVramBase == 0
                    ? 0x3f00
                    : static_cast<std::uint16_t>(debugVramBase - 0x0100);
            } else {
                debugVramBase = debugVramBase >= 0x3f00
                    ? 0
                    : static_cast<std::uint16_t>(debugVramBase + 0x0100);
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);

        RECT client;
        GetClientRect(window, &client);
        if (debugReaderVisible && activePpu) {
            drawDebugReader(dc, client);
        } else {
            if (activePpu && pixels) {
                const auto& framebuffer = activePpu->framebuffer();
                for (std::size_t i = 0; i < framebuffer.size(); ++i) {
                    pixels[i] = framebuffer[i] & 0x00ffffff;
                }
            }
            StretchBlt(
                dc,
                0,
                0,
                client.right - client.left,
                client.bottom - client.top,
                bitmapDc,
                0,
                0,
                runamuck::Ppu::width,
                runamuck::Ppu::height,
                SRCCOPY);
        }
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int) {
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        MessageBoxA(nullptr, SDL_GetError(), "SDL initialization failed",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    int gamepadCount = 0;
    SDL_JoystickID* gamepadIds = SDL_GetGamepads(&gamepadCount);
    if (!gamepadIds && gamepadCount > 0) {
        MessageBoxA(
            nullptr,
            SDL_GetError(),
            "Could not enumerate game controllers",
            MB_OK | MB_ICONERROR);
        SDL_Quit();
        return 1;
    }
    if (gamepadIds) {
        for (int i = 0; i < gamepadCount; ++i) {
            if (!openGamepad(gamepadIds[i])) {
                SDL_free(gamepadIds);
                SDL_Quit();
                return 1;
            }
        }
        SDL_free(gamepadIds);
    }

    const std::filesystem::path defaultRom =
        std::filesystem::path(__FILE__).parent_path()
            .parent_path().parent_path().parent_path()
            / "test_pattern.nes";

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::filesystem::path romPath = defaultRom;
    bool dotRendererEnabled = true;
    bool traceMmc1 = false;
    if (argv) {
        for (int argument = 1; argument < argc; ++argument) {
            if (std::wstring_view(argv[argument]) == L"--dot-renderer") {
                dotRendererEnabled = true;
            } else if (std::wstring_view(argv[argument]) == L"--trace-mmc1") {
                traceMmc1 = true;
            } else {
                romPath = argv[argument];
            }
        }
    }
    if (argv) {
        LocalFree(argv);
    }

    runamuck::NesBus bus;
    std::shared_ptr<runamuck::Cartridge> cartridge;
    try {
        cartridge = std::make_shared<runamuck::Cartridge>(
            runamuck::Cartridge::load(romPath));
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Could not load ROM", MB_OK | MB_ICONERROR);
        return 1;
    }
    bus.setCartridge(cartridge);

    std::ofstream traceFile;
    if (traceMmc1) {
        const auto tracePath = std::filesystem::current_path() /
            (romPath.stem().string() + "-mmc1-ppu-trace.log");
        traceFile.open(tracePath, std::ios::out | std::ios::trunc);
        if (!traceFile) {
            MessageBoxA(nullptr, "Could not create the MMC1/PPU trace file.",
                        "Trace file error", MB_OK | MB_ICONERROR);
            return 1;
        }
        traceFile << "ROM=" << romPath.string()
                  << " mapper=" << cartridge->mapperId()
                  << " PRG bytes=" << cartridge->prgRom().size()
                  << " CHR bytes=" << cartridge->chrData().size() << '\n';
        cartridge->setMmc1TraceStream(&traceFile);
    }

    auto ppu = std::make_shared<runamuck::Ppu>();
    ppu->setCartridge(cartridge);
    ppu->setDotRendererEnabled(dotRendererEnabled);
    if (traceMmc1) {
        ppu->setTraceStream(&traceFile);
    }
    bus.setPpu(ppu);
    activePpu = ppu.get();

       auto apu = std::make_shared<runamuck::Apu>();
    bus.setApu(apu);

    SDL_AudioSpec audioSpec{};
    audioSpec.format = SDL_AUDIO_F32;
    audioSpec.channels = 1;
    audioSpec.freq = runamuck::Apu::sampleRate;
    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &audioSpec,
        nullptr,
        nullptr);
    if (audioStream) {
        SDL_ResumeAudioStreamDevice(audioStream);
    }

    runamuck::Cpu6502 cpu(bus);
    if (traceMmc1) {
        cpu.setTraceStream(&traceFile);
    }
    ppu->setVblankCallback([&cpu]() { cpu.nmi(); });
    cpu.reset();

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"RunamuckVisualRunner";
    RegisterClassW(&windowClass);

    RECT bounds{0, 0, 768, 720};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    windowHandle = CreateWindowW(
        windowClass.lpszClassName,
        L"Runamuck NES Visual Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!windowHandle) {
        return 1;
    }

    createFramebuffer();
    ShowWindow(windowHandle, SW_SHOWNORMAL);
    UpdateWindow(windowHandle);

    MSG message{};
    auto fpsWindowStart = std::chrono::steady_clock::now();
    std::uint32_t framesInFpsWindow = 0;
    std::uint32_t loopFpsTenths = 0;
    double frameWorkMilliseconds = 0.0;
    while (message.message != WM_QUIT) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        pollGamepads();
        setControllerState(bus, 0, gamepads[0], true);
        setControllerState(bus, 1, gamepads[1], false);

        const auto frameWorkStart = std::chrono::steady_clock::now();
        ppu->acknowledgeFrame();
        std::uint32_t cpuCycles = 0;
         while (!ppu->frameComplete() && cpuCycles < 30000) {
            const std::uint8_t cycles = cpu.step();
            cpuCycles += cycles;
            for (std::uint8_t c = 0; c < cycles; ++c) {
                apu->clock();
            }
            if (apu->irqPending()) {
                cpu.irq();
            }
            ppu->clock(static_cast<std::uint32_t>(cycles) * 3);
        }

        const auto audioSamples = apu->consumeSamples();
        if (audioStream && !audioSamples.empty()) {
            const int queuedBytes = SDL_GetAudioStreamQueued(audioStream);
            const int maxQueuedBytes = runamuck::Apu::sampleRate / 10 * static_cast<int>(sizeof(float));
            if (queuedBytes < maxQueuedBytes) {
                SDL_PutAudioStreamData(
                    audioStream,
                    audioSamples.data(),
                    static_cast<int>(audioSamples.size() * sizeof(float)));
            }
        }

        const auto frameWorkEnd = std::chrono::steady_clock::now();
        frameWorkMilliseconds = std::chrono::duration<double, std::milli>(
            frameWorkEnd - frameWorkStart).count();
        ++framesInFpsWindow;
        const double fpsWindowSeconds = std::chrono::duration<double>(
            frameWorkEnd - fpsWindowStart).count();
        if (fpsWindowSeconds >= 1.0) {
            loopFpsTenths = static_cast<std::uint32_t>(
                framesInFpsWindow * 10.0 / fpsWindowSeconds + 0.5);
            framesInFpsWindow = 0;
            fpsWindowStart = frameWorkEnd;
        }

        const auto frameWorkTenths = static_cast<std::uint32_t>(
            frameWorkMilliseconds * 10.0 + 0.5);

        const std::wstring diagnosticTitle =
            std::wstring(L"Runamuck NES Visual Test") +
            (traceMmc1 ? std::wstring(L" [MMC1 CURRENT 2026-11-25]") :
                         std::wstring()) +
            L" - OAM sprites: " +
            std::to_wstring(ppu->activeSpriteCount()) +
            L", rendered sprite pixels: " +
            std::to_wstring(ppu->lastFrameSpritePixelCount()) +
            L", PPU mask: " + std::to_wstring(ppu->renderingMask()) +
            L", loop FPS: " + std::to_wstring(loopFpsTenths / 10) + L"." +
            std::to_wstring(loopFpsTenths % 10) +
            L", frame work ms: " + std::to_wstring(frameWorkTenths / 10) +
            L"." + std::to_wstring(frameWorkTenths % 10);
        SetWindowTextW(windowHandle, diagnosticTitle.c_str());

        InvalidateRect(windowHandle, nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (audioStream) {
        SDL_DestroyAudioStream(audioStream);
    }
    DeleteObject(bitmap);
    DeleteDC(bitmapDc);
    for (auto* gamepad : gamepads) {
        if (gamepad) {
            SDL_CloseGamepad(gamepad);
        }
    }
    SDL_Quit();
    return 0;
}