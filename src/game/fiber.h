// Stackful coroutines for native routines that call original code (see Machine::call_far): the native routine
// runs on its own stack and hands control back to the interpreter while the original code runs, then carries
// on where it left off. ucontext on desktop POSIX, ABI context switches on Android, Fibers on Windows.
// Single-threaded: resume() is only called from the interpreter's own stack, and a fiber yields back to
// whoever resumed it. A call-out that reaches another yielding native is fine: the interpreter (on its own
// stack) starts that one's fiber, so fibers never nest.
//
// Cancelling a suspended fiber (cancel(), or destroying it unfinished) resumes it once with FiberCancelled
// thrown from its yield(), so the body unwinds to the fiber's entry and every destructor on its stack runs;
// nothing else in the body executes. The game's non-local restarts use this (game::Session).
#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace sawgod::game {

// Thrown out of Fiber::yield() in a cancelled fiber. Game code must let it through (never catch (...)).
struct FiberCancelled {};

class Fiber {
  public:
    explicit Fiber(std::function<void()> body, size_t stack_size = 256 * 1024);
    ~Fiber();
    Fiber(const Fiber &) = delete;
    Fiber &operator=(const Fiber &) = delete;

    // Run the body until it yields or finishes; true once it has finished.
    bool resume();
    bool finished() const { return finished_; }
    // From the host stack (no fiber running): unwind a suspended body (see above); a fiber that never
    // started just finishes. No-op once finished.
    void cancel();

    // From inside a body: back to the resume() that ran it.
    static void yield();
    // The fiber whose body is running, or nullptr on the interpreter's stack.
    static Fiber *current();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::function<void()> body_;
    bool started_ = false;
    bool finished_ = false;
    bool cancel_ = false;
    static void entry(Fiber *self);
};

} // namespace sawgod::game
