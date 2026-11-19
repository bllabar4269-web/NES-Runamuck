cmake_minimum_required(VERSION 3.20)

project(Runamuck VERSION 0.1.0 LANGUAGES CXX)

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
    src/nes/NromMapper.cpp
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
