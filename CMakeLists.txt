cmake_minimum_required(VERSION 3.20)

project(Runamuck VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(runamuck_options INTERFACE)

if(MSVC)
    target_compile_options(runamuck_options INTERFACE /W4 /permissive- /EHsc)
else()
    target_compile_options(runamuck_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_library(runamuck_cpu
    src/cpu/Bus.cpp
    src/cpu/Cpu6502.cpp
    src/cpu/NesBus.cpp
    src/nes/Cartridge.cpp
    src/nes/Controller.cpp
    src/nes/NromMapper.cpp
    src/nes/Mmc1Mapper.cpp
    src/nes/Mmc2Mapper.cpp
    src/nes/PpuBus.cpp
    src/nes/Ppu.cpp
    src/nes/Apu.cpp
)

target_link_libraries(runamuck_cpu PRIVATE runamuck_options)

target_include_directories(runamuck_cpu
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

enable_testing()

add_executable(cpu_tests
    tests/cpu_tests.cpp
)

target_link_libraries(cpu_tests PRIVATE runamuck_cpu runamuck_options)
add_test(NAME cpu_tests COMMAND cpu_tests)

add_executable(nes_bus_tests
    tests/nes_bus_tests.cpp
)

target_link_libraries(nes_bus_tests PRIVATE runamuck_cpu runamuck_options)
add_test(NAME nes_bus_tests COMMAND nes_bus_tests)

add_executable(cartridge_tests
    tests/cartridge_tests.cpp
)

target_link_libraries(cartridge_tests PRIVATE runamuck_cpu runamuck_options)
add_test(NAME cartridge_tests COMMAND cartridge_tests)

add_executable(nrom_mapper_tests
    tests/nrom_mapper_tests.cpp
)

target_link_libraries(nrom_mapper_tests PRIVATE runamuck_cpu runamuck_options)
add_test(NAME nrom_mapper_tests COMMAND nrom_mapper_tests)

add_executable(ppu_tests
    tests/ppu_tests.cpp
)

target_link_libraries(ppu_tests PRIVATE runamuck_cpu runamuck_options)
add_test(NAME ppu_tests COMMAND ppu_tests)

if(MSVC)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.10/SDL3-3.2.10.tar.gz
        URL_HASH SHA256=F87BE7B4DEC66DB4098E9C167B2AA34E2CA10AEB5443BDDE95AE03185ED513E0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
    FetchContent_MakeAvailable(SDL3)

    add_executable(runamuck_visual WIN32
        src/app/VisualRunner.cpp
    )
    target_link_libraries(runamuck_visual PRIVATE
        runamuck_cpu
        runamuck_options
        SDL3::SDL3-static
        gdi32
        user32
    )
endif()
