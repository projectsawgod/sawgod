// Android/Bionic has no makecontext/swapcontext. Save only the ABI's callee-saved registers: all switches
// happen at ordinary function calls on one thread. Signal masks and TLS stay with that thread.
#pragma once
#include <cstdint>

namespace sawgod::game {
struct alignas(16) AndroidFiberContext {
    uint64_t registers[24]{};
};
extern "C" void sawgod_fiber_switch(AndroidFiberContext *from, const AndroidFiberContext *to);
extern "C" void sawgod_fiber_start();
} // namespace sawgod::game
