#include "runamuck/cpu/Cpu6502.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

class TestBus : public runamuck::Bus {
public:
    std::array<std::uint8_t, 65536> memory{};

    std::uint8_t read(std::uint16_t address) override {
        return memory[address];
    }

    void write(std::uint16_t address, std::uint8_t value) override {
        memory[address] = value;
    }
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void resetVector(TestBus& bus, std::uint16_t address) {
    bus.memory[0xfffc] = static_cast<std::uint8_t>(address);
    bus.memory[0xfffd] = static_cast<std::uint8_t>(address >> 8);
}

} // namespace

int main() {
    TestBus bus;
    runamuck::Cpu6502 cpu(bus);

    resetVector(bus, 0x8000);
    bus.memory[0x8000] = 0xa9;
    bus.memory[0x8001] = 0x42;
    cpu.reset();
    require(cpu.step() == 2, "LDA immediate cycle count");
    require(cpu.a == 0x42, "LDA immediate loads A");
    require(cpu.pc == 0x8002, "LDA immediate advances PC");

    bus.memory[0x8002] = 0x69;
    bus.memory[0x8003] = 0x01;
    cpu.status &= ~runamuck::Cpu6502::carryFlag;
    require(cpu.step() == 2, "ADC immediate cycle count");
    require(cpu.a == 0x43, "ADC immediate result");

    bus.memory[0x8004] = 0x85;
    bus.memory[0x8005] = 0x20;
    require(cpu.step() == 3, "STA zero-page cycle count");
    require(bus.memory[0x20] == 0x43, "STA zero-page writes memory");

    resetVector(bus, 0x9000);
    bus.memory[0x9000] = 0x18;
    cpu.reset();
    require(cpu.step() == 2, "CLC cycle count");
    require(!(cpu.status & runamuck::Cpu6502::carryFlag), "CLC clears carry");

    resetVector(bus, 0x9000);
    bus.memory[0x9000] = 0xd8;
    cpu.reset();
    cpu.status |= runamuck::Cpu6502::decimalFlag;
    cpu.step();
    require(!(cpu.status & runamuck::Cpu6502::decimalFlag), "CLD clears decimal");

    resetVector(bus, 0x9000);
    bus.memory[0x9000] = 0x20;
    bus.memory[0x9001] = 0x08;
    bus.memory[0x9002] = 0x90;
    bus.memory[0x9008] = 0x60;
    cpu.reset();
    require(cpu.step() == 6, "JSR cycle count");
    require(cpu.pc == 0x9008, "JSR sets target");
    require(cpu.step() == 6, "RTS cycle count");
    require(cpu.pc == 0x9003, "RTS restores PC");

    resetVector(bus, 0x8000);
    bus.memory[0x8000] = 0x69;
    bus.memory[0x8001] = 0x50;
    cpu.reset();
    cpu.a = 0x50;
    cpu.status &= ~runamuck::Cpu6502::carryFlag;
    cpu.step();
    require(cpu.a == 0xa0, "ADC wraps result");
    require((cpu.status & runamuck::Cpu6502::overflowFlag) != 0, "ADC sets signed overflow");
    require((cpu.status & runamuck::Cpu6502::negativeFlag) != 0, "ADC sets negative");
    require((cpu.status & runamuck::Cpu6502::carryFlag) == 0, "ADC leaves carry clear without unsigned overflow");

    bus.memory[0x8002] = 0xe9;
    bus.memory[0x8003] = 0x01;
    cpu.a = 0;
    cpu.status |= runamuck::Cpu6502::carryFlag;
    cpu.step();
    require(cpu.a == 0xff, "SBC subtracts with no borrow");
    require((cpu.status & runamuck::Cpu6502::carryFlag) == 0, "SBC clears carry on borrow");

    resetVector(bus, 0x80fe);
    bus.memory[0x80fe] = 0xd0;
    bus.memory[0x80ff] = 0xff;
    cpu.reset();
    cpu.status &= ~runamuck::Cpu6502::zeroFlag;
    require(cpu.step() == 4, "taken branch crossing a page takes four cycles");
    require(cpu.pc == 0x80ff, "branch crosses a page");

    resetVector(bus, 0x8000);
    bus.memory[0x8000] = 0xea;
    bus.memory[0xfffa] = 0x34;
    bus.memory[0xfffb] = 0x12;
    cpu.reset();
    cpu.status |= runamuck::Cpu6502::breakFlag;
    cpu.nmi();
    require(cpu.step() == 7, "NMI cycle count");
    require(cpu.pc == 0x1234, "NMI loads its vector");
    require(bus.memory[0x01fb] == (runamuck::Cpu6502::unusedFlag |
                                    runamuck::Cpu6502::interruptDisableFlag),
            "NMI pushes status with break clear and unused set");
    require((cpu.status & runamuck::Cpu6502::breakFlag) == 0, "NMI clears internal break flag");

    resetVector(bus, 0x8000);
    bus.memory[0x8000] = 0x28;
    cpu.reset();
    bus.memory[0x01fe] = runamuck::Cpu6502::breakFlag;
    cpu.sp = 0xfd;
    cpu.step();
    require((cpu.status & runamuck::Cpu6502::breakFlag) == 0, "PLP does not retain the break flag");
    require((cpu.status & runamuck::Cpu6502::unusedFlag) != 0, "PLP keeps the unused flag set");

    std::cout << "All CPU tests passed.\n";
    return 0;
}
