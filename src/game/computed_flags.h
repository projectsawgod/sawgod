// Width-explicit arithmetic and stack operations shared by computed-entry handlers.
// These helpers preserve the V30's incidental flags, including multi-bit shift overflow.
#pragma once
#include "game/machine.h"
#include <bit>
#include <limits>
namespace sawgod::game::computed {
inline void flag(Machine &m, uint16_t mask, bool value) {
    m.flags() = uint16_t((m.flags() & ~mask) | (value ? mask : 0));
}
template <typename T> inline void result_flags(Machine &m, T value) {
    flag(m, emu::SF, value & (T(1) << (sizeof(T) * 8 - 1)));
    flag(m, emu::ZF, value == 0);
    flag(m, emu::PF, (std::popcount(uint8_t(value)) & 1) == 0);
}
template <typename T> inline T add(Machine &m, T a, T b, unsigned carry = 0) {
    uint32_t sum = uint32_t(a) + b + carry;
    constexpr uint32_t sign = 1u << (sizeof(T) * 8 - 1);
    flag(m, emu::CF, sum > std::numeric_limits<T>::max());
    flag(m, emu::OF, (sum ^ a) & (sum ^ b) & sign);
    flag(m, emu::AF, (a ^ b ^ sum) & 0x10);
    result_flags(m, T(sum));
    return T(sum);
}
template <typename T> inline T subtract(Machine &m, T a, T b, unsigned borrow = 0) {
    uint32_t difference = uint32_t(a) - b - borrow;
    constexpr uint32_t sign = 1u << (sizeof(T) * 8 - 1);
    flag(m, emu::CF, difference > std::numeric_limits<T>::max());
    flag(m, emu::OF, (a ^ b) & (a ^ difference) & sign);
    flag(m, emu::AF, (a ^ b ^ difference) & 0x10);
    result_flags(m, T(difference));
    return T(difference);
}
template <typename T> inline T logic(Machine &m, T value) {
    m.flags() &= ~(emu::CF | emu::OF | emu::AF);
    result_flags(m, value);
    return value;
}
template <typename T> inline T increment(Machine &m, T value, bool down = false) {
    const bool carry = m.flags() & emu::CF;
    T result = down ? subtract(m, value, T(1)) : add(m, value, T(1));
    flag(m, emu::CF, carry);
    return result;
}
template <typename T> inline T shift_left(Machine &m, T value, unsigned count) {
    count &= 31;
    if (!count)
        return value;
    bool carry = false;
    for (unsigned i = 0; i < count; ++i) {
        carry = value & (T(1) << (sizeof(T) * 8 - 1));
        value = T(value << 1);
    }
    flag(m, emu::CF, carry);
    flag(m, emu::OF, bool(value & (T(1) << (sizeof(T) * 8 - 1))) != carry);
    m.flags() &= ~emu::AF;
    result_flags(m, value);
    return value;
}
template <typename T> inline T shift_right(Machine &m, T value, unsigned count, bool arithmetic = false) {
    count &= 31;
    if (!count)
        return value;
    constexpr T sign = T(1) << (sizeof(T) * 8 - 1);
    const T original = value;
    bool carry = false;
    for (unsigned i = 0; i < count; ++i) {
        carry = value & 1;
        value = T((value >> 1) | (arithmetic ? value & sign : 0));
    }
    flag(m, emu::CF, carry);
    flag(m, emu::OF, !arithmetic && count == 1 && (original & sign));
    m.flags() &= ~emu::AF;
    result_flags(m, value);
    return value;
}
template <typename T> inline T rotate_left(Machine &m, T value, unsigned count, bool through_carry = false) {
    count &= 31;
    if (!count)
        return value;
    constexpr T sign = T(1) << (sizeof(T) * 8 - 1);
    bool carry = m.flags() & emu::CF;
    for (unsigned i = 0; i < count; ++i) {
        bool high = value & sign;
        value = T((value << 1) | (through_carry ? carry : high));
        carry = high;
    }
    flag(m, emu::CF, carry);
    flag(m, emu::OF, bool(value & sign) != carry);
    return value;
}
template <typename T> inline T rotate_right(Machine &m, T value, unsigned count) {
    count &= 31;
    if (!count)
        return value;
    constexpr T sign = T(1) << (sizeof(T) * 8 - 1);
    bool carry = m.flags() & emu::CF;
    for (unsigned i = 0; i < count; ++i) {
        bool low = value & 1;
        value = T((value >> 1) | (carry ? sign : 0));
        carry = low;
    }
    flag(m, emu::CF, carry);
    flag(m, emu::OF, ((value >> (sizeof(T) * 8 - 1)) ^ (value >> (sizeof(T) * 8 - 2))) & 1);
    return value;
}
inline void push(Machine &m, uint16_t value) {
    m.sp() = uint16_t(m.sp() - 2);
    m.wr16(m.ss(), m.sp(), value);
}
inline uint16_t pop(Machine &m) {
    uint16_t value = m.rd16(m.ss(), m.sp());
    m.sp() = uint16_t(m.sp() + 2);
    return value;
}
inline void save_registers(Machine &m) {
    uint16_t sp = m.sp();
    for (uint16_t v : {m.ax(), m.cx(), m.dx(), m.bx(), sp, m.bp(), m.si(), m.di()})
        push(m, v);
}
inline void restore_registers(Machine &m) {
    m.di() = pop(m);
    m.si() = pop(m);
    m.bp() = pop(m);
    (void)pop(m);
    m.bx() = pop(m);
    m.dx() = pop(m);
    m.cx() = pop(m);
    m.ax() = pop(m);
}
inline void out_word(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}
inline void copy_words(Machine &m) {
    while (m.cx()) {
        m.wr16(m.es(), m.di(), m.rd16(m.ds(), m.si()));
        int step = m.flags() & emu::DF ? -2 : 2;
        m.si() = uint16_t(m.si() + step);
        m.di() = uint16_t(m.di() + step);
        --m.cx();
    }
}
inline void fill_words(Machine &m) {
    while (m.cx()) {
        m.wr16(m.es(), m.di(), m.ax());
        m.di() = uint16_t(m.di() + (m.flags() & emu::DF ? -2 : 2));
        --m.cx();
    }
}
inline void fill_bytes(Machine &m) {
    while (m.cx()) {
        m.wr8(m.es(), m.di(), m.al());
        m.di() = uint16_t(m.di() + (m.flags() & emu::DF ? -1 : 1));
        --m.cx();
    }
}
} // namespace sawgod::game::computed
