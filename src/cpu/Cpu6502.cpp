#include "runamuck/cpu/Cpu6502.hpp"

#include <ostream>

namespace runamuck {

Cpu6502::Cpu6502(Bus& bus) : bus_(bus) {}

void Cpu6502::reset() {
    const std::uint16_t low = read(0xfffc);
    const std::uint16_t high = read(0xfffd);
    pc = low | (high << 8);
    sp = 0xfd;
    status = unusedFlag | interruptDisableFlag;
    nmiPending_ = false;
    irqPending_ = false;
}

void Cpu6502::nmi() {
    nmiPending_ = true;
}

void Cpu6502::irq() {
    irqPending_ = true;
}

std::uint8_t Cpu6502::read(std::uint16_t address) const {
    const std::uint8_t value = bus_.read(address);
    if (traceStream_ && address >= 0x2000 && address < 0x4000 &&
        (address & 0x0007u) == 0x0002u) {
        ++statusReadCount_;
        if (statusReadCount_ <= 64 || (statusReadCount_ & 0xffu) == 0) {
            *traceStream_ << "CPU PPUSTATUS read pc=$" << std::hex
                          << currentInstructionAddress_ << " opcode=$"
                          << static_cast<unsigned>(currentOpcode_) << " value=$"
                          << static_cast<unsigned>(value) << std::dec
                          << " count=" << statusReadCount_ << '\n';
        }
    }
    return value;
}

void Cpu6502::write(std::uint16_t address, std::uint8_t value) {
    if (traceStream_) {
        if (address == 0x4014) {
            *traceStream_ << "CPU OAMDMA write pc=$" << std::hex
                          << currentInstructionAddress_ << " opcode=$"
                          << static_cast<unsigned>(currentOpcode_) << " page=$"
                          << static_cast<unsigned>(value) << std::dec << '\n';
        }
        const std::uint16_t ramAddress = address & 0x07ffu;
        if (ramAddress >= 0x0200u && ramAddress < 0x0300u &&
            ((ramAddress - 0x0200u) & 0x03u) == 0) {
            const std::size_t index = (ramAddress - 0x0200u) / 4;
            if (!hasShadowOamY_[index] || lastShadowOamY_[index] != value) {
                *traceStream_ << "CPU shadow-OAM Y write pc=$" << std::hex
                              << currentInstructionAddress_ << " opcode=$"
                              << static_cast<unsigned>(currentOpcode_) << " addr=$"
                              << ramAddress << " value=$"
                              << static_cast<unsigned>(value) << std::dec << '\n';
                lastShadowOamY_[index] = value;
                hasShadowOamY_[index] = true;
            }
        }
    }
    bus_.write(address, value);
}

std::uint8_t Cpu6502::fetch() {
    return read(pc++);
}

std::uint16_t Cpu6502::fetchWord() {
    const std::uint16_t low = fetch();
    const std::uint16_t high = fetch();
    return low | (high << 8);
}

void Cpu6502::push(std::uint8_t value) {
    write(0x0100 | sp--, value);
}

std::uint8_t Cpu6502::pop() {
    return read(0x0100 | ++sp);
}

void Cpu6502::setZN(std::uint8_t value) {
    status = (status & ~(zeroFlag | negativeFlag)) |
             (value == 0 ? zeroFlag : 0) |
             (value & negativeFlag);
}

void Cpu6502::adc(std::uint8_t value) {
    const std::uint16_t sum = a + value + ((status & carryFlag) ? 1 : 0);
    status = (status & ~(carryFlag | overflowFlag | zeroFlag | negativeFlag));
    if (sum > 0xff) status |= carryFlag;
    if (~(a ^ value) & (a ^ sum) & negativeFlag) status |= overflowFlag;
    a = static_cast<std::uint8_t>(sum);
    setZN(a);
}

void Cpu6502::sbc(std::uint8_t value) {
    adc(static_cast<std::uint8_t>(value ^ 0xff));
}

void Cpu6502::andWith(std::uint8_t value) {
    a &= value;
    setZN(a);
}

void Cpu6502::xorWith(std::uint8_t value) {
    a ^= value;
    setZN(a);
}

void Cpu6502::orWith(std::uint8_t value) {
    a |= value;
    setZN(a);
}

void Cpu6502::compare(std::uint8_t left, std::uint8_t value) {
    const std::uint8_t result = left - value;
    status = (status & ~(carryFlag | zeroFlag | negativeFlag));
    if (left >= value) status |= carryFlag;
    setZN(result);
}

std::uint8_t Cpu6502::asl(std::uint8_t value) {
    status = (status & ~(carryFlag | zeroFlag | negativeFlag));
    if (value & 0x80) status |= carryFlag;
    value <<= 1;
    setZN(value);
    return value;
}

std::uint8_t Cpu6502::lsr(std::uint8_t value) {
    status = (status & ~(carryFlag | zeroFlag | negativeFlag));
    if (value & 0x01) status |= carryFlag;
    value >>= 1;
    setZN(value);
    return value;
}

std::uint8_t Cpu6502::rol(std::uint8_t value) {
    const bool oldCarry = (status & carryFlag) != 0;
    status = (status & ~(carryFlag | zeroFlag | negativeFlag));
    if (value & 0x80) status |= carryFlag;
    value = static_cast<std::uint8_t>((value << 1) | (oldCarry ? 1 : 0));
    setZN(value);
    return value;
}

std::uint8_t Cpu6502::ror(std::uint8_t value) {
    const bool oldCarry = (status & carryFlag) != 0;
    status = (status & ~(carryFlag | zeroFlag | negativeFlag));
    if (value & 0x01) status |= carryFlag;
    value = static_cast<std::uint8_t>((value >> 1) | (oldCarry ? 0x80 : 0));
    setZN(value);
    return value;
}

std::uint8_t Cpu6502::step() {
    if (nmiPending_) {
        nmiPending_ = false;
        push(static_cast<std::uint8_t>(pc >> 8));
        push(static_cast<std::uint8_t>(pc));
        push((status & ~breakFlag) | unusedFlag);
        status = (status | interruptDisableFlag | unusedFlag) & ~breakFlag;
        pc = read(0xfffa) | (read(0xfffb) << 8);
        return 7;
    }
    if (irqPending_ && !(status & interruptDisableFlag)) {
        irqPending_ = false;
        push(static_cast<std::uint8_t>(pc >> 8));
        push(static_cast<std::uint8_t>(pc));
        push((status & ~breakFlag) | unusedFlag);
        status = (status | interruptDisableFlag | unusedFlag) & ~breakFlag;
        pc = read(0xfffe) | (read(0xffff) << 8);
        return 7;
    }

    currentInstructionAddress_ = pc;
    const std::uint8_t opcode = fetch();
    currentOpcode_ = opcode;
    std::uint8_t cycles = 2;
    switch (opcode) {
    case 0x00: { push(static_cast<std::uint8_t>(pc >> 8)); push(static_cast<std::uint8_t>(pc)); push(status | breakFlag | unusedFlag); status |= interruptDisableFlag; pc = read(0xfffe) | (read(0xffff) << 8); cycles = 7; break; }
    case 0x08: push(status | unusedFlag | breakFlag); cycles = 3; break;
    case 0x18: status &= ~carryFlag; cycles = 2; break;
    case 0x28: status = (pop() & ~breakFlag) | unusedFlag; cycles = 4; break;
    case 0x38: status |= carryFlag; cycles = 2; break;
    case 0x48: push(a); cycles = 3; break;
    case 0x58: status &= ~interruptDisableFlag; cycles = 2; break;
    case 0x68: a = pop(); setZN(a); cycles = 4; break;
    case 0x78: status |= interruptDisableFlag; cycles = 2; break;
    case 0x88: setZN(--y); cycles = 2; break;
    case 0x98: a = y; setZN(a); cycles = 2; break;
    case 0xa8: y = a; setZN(y); cycles = 2; break;
    case 0xb8: status &= ~overflowFlag; cycles = 2; break;
    case 0xc8: setZN(++y); cycles = 2; break;
    case 0xd8: status &= ~decimalFlag; cycles = 2; break;
    case 0xe8: setZN(++x); cycles = 2; break;
    case 0xea: cycles = 2; break;
    case 0xf8: status |= decimalFlag; cycles = 2; break;
    case 0x40: status = (pop() & ~breakFlag) | unusedFlag; pc = pop(); pc |= pop() << 8; cycles = 6; break;
    case 0x60: pc = pop(); pc |= pop() << 8; pc++; cycles = 6; break;
    case 0x6c: { std::uint16_t ptr = fetchWord(); pc = read(ptr) | (read(static_cast<std::uint16_t>((ptr & 0xff00) | ((ptr + 1) & 0x00ff))) << 8); cycles = 5; break; }
    case 0x4c: pc = fetchWord(); cycles = 3; break;
    case 0x20: { std::uint16_t target = fetchWord(); push(static_cast<std::uint8_t>((pc - 1) >> 8)); push(static_cast<std::uint8_t>(pc - 1)); pc = target; cycles = 6; break; }

    case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xb0: case 0xd0: case 0xf0: {
        const std::int8_t offset = static_cast<std::int8_t>(fetch());
        bool take = (opcode == 0x10 && !(status & negativeFlag)) ||
                    (opcode == 0x30 && (status & negativeFlag)) ||
                    (opcode == 0x50 && !(status & overflowFlag)) ||
                    (opcode == 0x70 && (status & overflowFlag)) ||
                    (opcode == 0x90 && !(status & carryFlag)) ||
                    (opcode == 0xb0 && (status & carryFlag)) ||
                    (opcode == 0xd0 && !(status & zeroFlag)) ||
                    (opcode == 0xf0 && (status & zeroFlag));
        if (take) {
            const std::uint16_t old = pc;
            pc = static_cast<std::uint16_t>(pc + offset);
            cycles = (old & 0xff00) != (pc & 0xff00) ? 4 : 3;
        }
        break;
    }

    case 0xa9: a = fetch(); setZN(a); cycles = 2; break;
    case 0xa5: a = read(fetch()); setZN(a); cycles = 3; break;
    case 0xb5: a = read(static_cast<std::uint8_t>(fetch() + x)); setZN(a); cycles = 4; break;
    case 0xad: a = read(fetchWord()); setZN(a); cycles = 4; break;
    case 0xbd: { const std::uint16_t base = fetchWord(); std::uint16_t address = base + x; a = read(address); setZN(a); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0xb9: { const std::uint16_t base = fetchWord(); std::uint16_t address = base + y; a = read(address); setZN(a); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0xa1: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); a = read(address); setZN(a); cycles = 6; break; }
    case 0xb1: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); std::uint16_t address = base + y; a = read(address); setZN(a); cycles = (base & 0xff00) != (address & 0xff00) ? 6 : 5; break; }

    case 0xa2: x = fetch(); setZN(x); cycles = 2; break;
    case 0xa6: x = read(fetch()); setZN(x); cycles = 3; break;
    case 0xb6: x = read(static_cast<std::uint8_t>(fetch() + y)); setZN(x); cycles = 4; break;
    case 0xae: x = read(fetchWord()); setZN(x); cycles = 4; break;
    case 0xbe: { std::uint16_t base = fetchWord(); std::uint16_t address = base + y; x = read(address); setZN(x); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }

    case 0xa0: y = fetch(); setZN(y); cycles = 2; break;
    case 0xa4: y = read(fetch()); setZN(y); cycles = 3; break;
    case 0xb4: y = read(static_cast<std::uint8_t>(fetch() + x)); setZN(y); cycles = 4; break;
    case 0xac: y = read(fetchWord()); setZN(y); cycles = 4; break;
    case 0xbc: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; y = read(address); setZN(y); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }

    case 0x85: write(fetch(), a); cycles = 3; break;
    case 0x95: write(static_cast<std::uint8_t>(fetch() + x), a); cycles = 4; break;
    case 0x8d: write(fetchWord(), a); cycles = 4; break;
    case 0x9d: write(fetchWord() + x, a); cycles = 5; break;
    case 0x99: write(fetchWord() + y, a); cycles = 5; break;
    case 0x81: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); write(address, a); cycles = 6; break; }
    case 0x91: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); write(base + y, a); cycles = 6; break; }
    case 0x86: write(fetch(), x); cycles = 3; break;
    case 0x96: write(static_cast<std::uint8_t>(fetch() + y), x); cycles = 4; break;
    case 0x8e: write(fetchWord(), x); cycles = 4; break;
    case 0x84: write(fetch(), y); cycles = 3; break;
    case 0x94: write(static_cast<std::uint8_t>(fetch() + x), y); cycles = 4; break;
    case 0x8c: write(fetchWord(), y); cycles = 4; break;

    case 0x8a: a = x; setZN(a); cycles = 2; break;
    case 0x9a: sp = x; cycles = 2; break;
    case 0xaa: x = a; setZN(x); cycles = 2; break;
    case 0xba: x = sp; setZN(x); cycles = 2; break;

    case 0xe6: { std::uint16_t address = fetch(); std::uint8_t value = read(address); write(address, ++value); setZN(value); cycles = 5; break; }
    case 0xf6: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint8_t value = read(zp); write(zp, ++value); setZN(value); cycles = 6; break; }
    case 0xee: { std::uint16_t address = fetchWord(); std::uint8_t value = read(address); write(address, ++value); setZN(value); cycles = 6; break; }
    case 0xfe: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; std::uint8_t value = read(address); write(address, ++value); setZN(value); cycles = 7; break; }
    case 0xc6: { std::uint16_t address = fetch(); std::uint8_t value = read(address); write(address, --value); setZN(value); cycles = 5; break; }
    case 0xd6: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint8_t value = read(zp); write(zp, --value); setZN(value); cycles = 6; break; }
    case 0xce: { std::uint16_t address = fetchWord(); std::uint8_t value = read(address); write(address, --value); setZN(value); cycles = 6; break; }
    case 0xde: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; std::uint8_t value = read(address); write(address, --value); setZN(value); cycles = 7; break; }
    case 0xca: setZN(--x); cycles = 2; break;

    case 0x69: adc(fetch()); cycles = 2; break;
    case 0x65: adc(read(fetch())); cycles = 3; break;
    case 0x75: adc(read(static_cast<std::uint8_t>(fetch() + x))); cycles = 4; break;
    case 0x6d: adc(read(fetchWord())); cycles = 4; break;
    case 0x7d: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; adc(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x79: { std::uint16_t base = fetchWord(); std::uint16_t address = base + y; adc(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x61: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); adc(read(address)); cycles = 6; break; }
    case 0x71: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); std::uint16_t address = base + y; adc(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 6 : 5; break; }

    case 0xe9: case 0xeb: sbc(fetch()); cycles = 2; break;
    case 0xe5: sbc(read(fetch())); cycles = 3; break;
    case 0xf5: sbc(read(static_cast<std::uint8_t>(fetch() + x))); cycles = 4; break;
    case 0xed: sbc(read(fetchWord())); cycles = 4; break;
    case 0xfd: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; sbc(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0xf9: { std::uint16_t base = fetchWord(); std::uint16_t address = base + y; sbc(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0xe1: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); sbc(read(address)); cycles = 6; break; }
    case 0xf1: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); std::uint16_t address = base + y; sbc(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 6 : 5; break; }

    case 0x29: andWith(fetch()); cycles = 2; break;
    case 0x25: andWith(read(fetch())); cycles = 3; break;
    case 0x35: andWith(read(static_cast<std::uint8_t>(fetch() + x))); cycles = 4; break;
    case 0x2d: andWith(read(fetchWord())); cycles = 4; break;
    case 0x3d: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; andWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x39: { std::uint16_t base = fetchWord(); std::uint16_t address = base + y; andWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x21: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); andWith(read(address)); cycles = 6; break; }
    case 0x31: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); std::uint16_t address = base + y; andWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 6 : 5; break; }

    case 0x49: xorWith(fetch()); cycles = 2; break;
    case 0x45: xorWith(read(fetch())); cycles = 3; break;
    case 0x55: xorWith(read(static_cast<std::uint8_t>(fetch() + x))); cycles = 4; break;
    case 0x4d: xorWith(read(fetchWord())); cycles = 4; break;
    case 0x5d: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; xorWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x59: { std::uint16_t base = fetchWord(); std::uint16_t address = base + y; xorWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x41: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); xorWith(read(address)); cycles = 6; break; }
    case 0x51: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); std::uint16_t address = base + y; xorWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 6 : 5; break; }

    case 0x09: orWith(fetch()); cycles = 2; break;
    case 0x05: orWith(read(fetch())); cycles = 3; break;
    case 0x15: orWith(read(static_cast<std::uint8_t>(fetch() + x))); cycles = 4; break;
    case 0x0d: orWith(read(fetchWord())); cycles = 4; break;
    case 0x1d: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; orWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x19: { std::uint16_t base = fetchWord(); std::uint16_t address = base + y; orWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0x01: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); orWith(read(address)); cycles = 6; break; }
    case 0x11: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); std::uint16_t address = base + y; orWith(read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 6 : 5; break; }

    case 0xc9: compare(a, fetch()); cycles = 2; break;
    case 0xc5: compare(a, read(fetch())); cycles = 3; break;
    case 0xd5: compare(a, read(static_cast<std::uint8_t>(fetch() + x))); cycles = 4; break;
    case 0xcd: compare(a, read(fetchWord())); cycles = 4; break;
    case 0xdd: { std::uint16_t base = fetchWord(); std::uint16_t address = base + x; compare(a, read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0xd9: { std::uint16_t base = fetchWord(); std::uint16_t address = base + y; compare(a, read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 5 : 4; break; }
    case 0xc1: { std::uint8_t zp = static_cast<std::uint8_t>(fetch() + x); std::uint16_t address = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); compare(a, read(address)); cycles = 6; break; }
    case 0xd1: { std::uint8_t zp = fetch(); std::uint16_t base = read(zp) | (read(static_cast<std::uint8_t>(zp + 1)) << 8); std::uint16_t address = base + y; compare(a, read(address)); cycles = (base & 0xff00) != (address & 0xff00) ? 6 : 5; break; }
    case 0xe0: compare(x, fetch()); cycles = 2; break;
    case 0xe4: compare(x, read(fetch())); cycles = 3; break;
    case 0xec: compare(x, read(fetchWord())); cycles = 4; break;
    case 0xc0: compare(y, fetch()); cycles = 2; break;
    case 0xc4: compare(y, read(fetch())); cycles = 3; break;
    case 0xcc: compare(y, read(fetchWord())); cycles = 4; break;

    case 0x24: { std::uint8_t value = read(fetch()); if (value & 0x40) status |= overflowFlag; else status &= ~overflowFlag; status = (status & ~(zeroFlag | negativeFlag)) | ((a & value) == 0 ? zeroFlag : 0) | (value & negativeFlag); cycles = 3; break; }
    case 0x2c: { std::uint8_t value = read(fetchWord()); if (value & 0x40) status |= overflowFlag; else status &= ~overflowFlag; status = (status & ~(zeroFlag | negativeFlag)) | ((a & value) == 0 ? zeroFlag : 0) | (value & negativeFlag); cycles = 4; break; }

    case 0x0a: a = asl(a); cycles = 2; break;
    case 0x06: { std::uint16_t address = fetch(); write(address, asl(read(address))); cycles = 5; break; }
    case 0x16: { std::uint8_t address = static_cast<std::uint8_t>(fetch() + x); write(address, asl(read(address))); cycles = 6; break; }
    case 0x0e: { std::uint16_t address = fetchWord(); write(address, asl(read(address))); cycles = 6; break; }
    case 0x1e: { std::uint16_t address = fetchWord() + x; write(address, asl(read(address))); cycles = 7; break; }
    case 0x4a: a = lsr(a); cycles = 2; break;
    case 0x46: { std::uint16_t address = fetch(); write(address, lsr(read(address))); cycles = 5; break; }
    case 0x56: { std::uint8_t address = static_cast<std::uint8_t>(fetch() + x); write(address, lsr(read(address))); cycles = 6; break; }
    case 0x4e: { std::uint16_t address = fetchWord(); write(address, lsr(read(address))); cycles = 6; break; }
    case 0x5e: { std::uint16_t address = fetchWord() + x; write(address, lsr(read(address))); cycles = 7; break; }
    case 0x2a: a = rol(a); cycles = 2; break;
    case 0x26: { std::uint16_t address = fetch(); write(address, rol(read(address))); cycles = 5; break; }
    case 0x36: { std::uint8_t address = static_cast<std::uint8_t>(fetch() + x); write(address, rol(read(address))); cycles = 6; break; }
    case 0x2e: { std::uint16_t address = fetchWord(); write(address, rol(read(address))); cycles = 6; break; }
    case 0x3e: { std::uint16_t address = fetchWord() + x; write(address, rol(read(address))); cycles = 7; break; }
    case 0x6a: a = ror(a); cycles = 2; break;
    case 0x66: { std::uint16_t address = fetch(); write(address, ror(read(address))); cycles = 5; break; }
    case 0x76: { std::uint8_t address = static_cast<std::uint8_t>(fetch() + x); write(address, ror(read(address))); cycles = 6; break; }
    case 0x6e: { std::uint16_t address = fetchWord(); write(address, ror(read(address))); cycles = 6; break; }
    case 0x7e: { std::uint16_t address = fetchWord() + x; write(address, ror(read(address))); cycles = 7; break; }

    default:
        // Unofficial opcodes are treated as a 2-cycle NOP for now.
        cycles = 2;
        break;
    }

    return cycles;
}

} // namespace runamuck
