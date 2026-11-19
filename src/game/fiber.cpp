// macOS only declares the (deprecated but working) ucontext routines with _XOPEN_SOURCE, which must come
// before any system header.
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include "game/fiber.h"

#include <cstdint>

#if defined(__ANDROID__) || defined(SAWGOD_TEST_ANDROID_FIBER)
#define SAWGOD_ANDROID_FIBER
#include "game/fiber_android.h"
#elif defined(_WIN32)
#include <windows.h>
#else
#include <ucontext.h>
#endif
#ifdef __APPLE__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace sawgod::game {
namespace {
Fiber *g_current = nullptr;
} // namespace

#ifdef _WIN32

struct Fiber::Impl {
    void *fiber = nullptr;
    void *caller = nullptr;
};

#elif defined(SAWGOD_ANDROID_FIBER)

struct Fiber::Impl {
    AndroidFiberContext ctx{}, caller{};
    std::unique_ptr<uint8_t[]> stack;
};

#else

struct Fiber::Impl {
    ucontext_t ctx{};
    ucontext_t caller{};
    std::unique_ptr<uint8_t[]> stack; // not zero-filled: allocated per call
};

#endif

Fiber::Fiber(std::function<void()> body, size_t stack_size) : impl_(new Impl), body_(std::move(body)) {
#ifdef _WIN32
    struct Start {
        static void __stdcall run(void *p) { Fiber::entry(static_cast<Fiber *>(p)); }
    };
    impl_->fiber = CreateFiber(stack_size, &Start::run, this);
#elif defined(SAWGOD_ANDROID_FIBER)
    impl_->stack.reset(new uint8_t[stack_size]);
    const uintptr_t top = (reinterpret_cast<uintptr_t>(impl_->stack.get()) + stack_size) & ~uintptr_t(15);
#if defined(__aarch64__)
    impl_->ctx.registers[0] = reinterpret_cast<uintptr_t>(this);                 // x19
    impl_->ctx.registers[1] = reinterpret_cast<uintptr_t>(&Fiber::entry);        // x20
    impl_->ctx.registers[11] = reinterpret_cast<uintptr_t>(&sawgod_fiber_start); // x30
    impl_->ctx.registers[12] = top;                                              // sp
    asm volatile("mrs %0, fpcr" : "=r"(impl_->ctx.registers[21]));
    asm volatile("mrs %0, fpsr" : "=r"(impl_->ctx.registers[22]));
#elif defined(__x86_64__)
    // switch's ret pops the entry address. The trampoline calls entry on a 16-byte aligned stack.
    auto *stack = reinterpret_cast<uintptr_t *>(top - 16);
    *stack = reinterpret_cast<uintptr_t>(&sawgod_fiber_start);
    impl_->ctx.registers[0] = reinterpret_cast<uintptr_t>(stack);
    impl_->ctx.registers[3] = reinterpret_cast<uintptr_t>(this);          // r12
    impl_->ctx.registers[4] = reinterpret_cast<uintptr_t>(&Fiber::entry); // r13
    asm volatile("stmxcsr %0" : "=m"(*reinterpret_cast<uint32_t *>(&impl_->ctx.registers[7])));
    asm volatile("fnstcw %0" : "=m"(*(reinterpret_cast<uint16_t *>(&impl_->ctx.registers[7]) + 2)));
#endif
#else
    impl_->stack.reset(new uint8_t[stack_size]);
    getcontext(&impl_->ctx);
    impl_->ctx.uc_stack.ss_sp = impl_->stack.get();
    impl_->ctx.uc_stack.ss_size = stack_size;
    impl_->ctx.uc_link = nullptr;
    // makecontext passes int arguments: split the pointer
    auto p = reinterpret_cast<uintptr_t>(this);
    makecontext(&impl_->ctx, reinterpret_cast<void (*)()>(+[](unsigned lo, unsigned hi) {
        entry(reinterpret_cast<Fiber *>((uintptr_t(hi) << 16 << 16) | lo));
    }),
                2, unsigned(p & 0xFFFFFFFFu), unsigned(uint64_t(p) >> 32));
#endif
}

Fiber::~Fiber() {
    // Unfinished (a native abandoned mid call-out): unwind its stack first, so its destructors run. Only
    // from the host stack: a fiber can't be switched to from inside another one here.
    if (started_ && !finished_ && !g_current)
        cancel();
#ifdef _WIN32
    if (impl_->fiber)
        DeleteFiber(impl_->fiber);
#endif
}

void Fiber::entry(Fiber *self) {
    try {
        self->body_();
    } catch (const FiberCancelled &) {
        // cancelled: the body has unwound
    }
    self->finished_ = true;
    g_current = nullptr;
#ifdef _WIN32
    SwitchToFiber(self->impl_->caller);
#elif defined(SAWGOD_ANDROID_FIBER)
    sawgod_fiber_switch(&self->impl_->ctx, &self->impl_->caller);
#else
    setcontext(&self->impl_->caller);
#endif
}

bool Fiber::resume() {
    if (finished_)
        return true;
    started_ = true;
    Fiber *prev = g_current;
    g_current = this;
#ifdef _WIN32
    if (!IsThreadAFiber())
        ConvertThreadToFiber(nullptr);
    impl_->caller = GetCurrentFiber();
    SwitchToFiber(impl_->fiber);
#elif defined(SAWGOD_ANDROID_FIBER)
    sawgod_fiber_switch(&impl_->caller, &impl_->ctx);
#else
    swapcontext(&impl_->caller, &impl_->ctx);
#endif
    g_current = prev;
    return finished_;
}

void Fiber::yield() {
    Fiber *self = g_current;
    if (!self)
        return;
#ifdef _WIN32
    SwitchToFiber(self->impl_->caller);
#elif defined(SAWGOD_ANDROID_FIBER)
    sawgod_fiber_switch(&self->impl_->ctx, &self->impl_->caller);
#else
    swapcontext(&self->impl_->ctx, &self->impl_->caller);
#endif
    if (self->cancel_)
        throw FiberCancelled{};
}

void Fiber::cancel() {
    if (finished_ || g_current)
        return;
    if (!started_) {
        finished_ = true;
        return;
    }
    cancel_ = true;
    resume();
}

Fiber *Fiber::current() { return g_current; }

} // namespace sawgod::game
