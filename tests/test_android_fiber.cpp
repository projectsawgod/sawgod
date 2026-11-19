// Exercise the Android context switch on a native host ABI, without a ROM or Android runtime.
#include "game/fiber.h"
#include <cfenv>
#include <doctest/doctest.h>
#include <vector>

using sawgod::game::Fiber;
TEST_CASE("Android fibers alternate stacks, preserve FP control and unwind cancellation") {
    const int original_rounding = std::fegetround();
    int count = 0;
    std::vector<int> destroyed;
    struct Guard {
        std::vector<int> &log;
        int id;
        ~Guard() { log.push_back(id); }
    };
    Fiber a([&] {
        Guard outer{destroyed, 1};
        std::fesetround(FE_DOWNWARD);
        for (int i = 0; i < 10000; ++i) {
            CHECK(Fiber::current() == &a);
            CHECK(std::fegetround() == FE_DOWNWARD);
            ++count;
            Fiber::yield();
        }
        Guard inner{destroyed, 2};
        Fiber::yield();
        FAIL("cancelled fiber resumed past its yield");
    });
    Fiber b([&] {
        std::fesetround(FE_UPWARD);
        for (int i = 0; i < 10000; ++i) {
            CHECK(std::fegetround() == FE_UPWARD);
            ++count;
            Fiber::yield();
        }
    });
    for (int i = 0; i < 10000; ++i) {
        CHECK_FALSE(a.resume());
        CHECK(Fiber::current() == nullptr);
        CHECK(std::fegetround() == original_rounding);
        CHECK_FALSE(b.resume());
        CHECK(std::fegetround() == original_rounding);
    }
    CHECK(count == 20000);
    CHECK(b.resume());
    CHECK(b.resume());
    CHECK_FALSE(a.resume());
    a.cancel();
    CHECK(a.finished());
    CHECK(destroyed == std::vector<int>{2, 1});
    CHECK(std::fegetround() == original_rounding);
    {
        Fiber c([&] {
            Guard g{destroyed, 3};
            Fiber::yield();
        });
        CHECK_FALSE(c.resume());
    }
    CHECK(destroyed.back() == 3);
    Fiber never([] { FAIL("cancelled unstarted fiber ran"); });
    never.cancel();
    CHECK(never.resume());
}
