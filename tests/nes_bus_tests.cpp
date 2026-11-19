#include "runamuck/nes/NesBus.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    runamuck::NesBus bus;

    bus.write(0x0000, 0x42);
    require(bus.read(0x0000) == 0x42, "RAM reads back its written value");
    require(bus.read(0x0800) == 0x42, "RAM mirrors at 0x800");
    require(bus.read(0x1000) == 0x42, "RAM mirrors at 0x1000");
    require(bus.read(0x1800) == 0x42, "RAM mirrors at 0x1800");

    bus.write(0x1fff, 0xa5);
    require(bus.read(0x07ff) == 0xa5, "last RAM byte mirrors to 0x07ff");

    std::cout << "ALL NesBus Tests Passed.\n";
    return 0;

}
