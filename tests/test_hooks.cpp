#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "emu/wsc.h"
#include "game/hooks.h"
#include "game/machine.h"
#include "game/session.h"

using namespace sawgod::game;

namespace {
constexpr auto kMeter = hooks::Value::BattleMeterChange;
constexpr auto kStart = hooks::Event::BattleStart;
constexpr auto kEnd = hooks::Event::BattleEnd;

struct Rig {
    sawgod::emu::Wsc w{std::vector<uint8_t>(0x10000), 0};
    Machine m{w};
    hooks::Registry &h = Session::of(w).hooks();
};
} // namespace

TEST_CASE("hooks are no-ops with nothing subscribed (job number one)") {
    Rig r;
    CHECK(hooks::filter(kMeter, r.m, 123) == 123);
    hooks::emit(kStart, r.m); // nothing happens
    CHECK(hooks::should(hooks::VB::Count, r.m, true));

    sawgod::emu::Wsc bare(std::vector<uint8_t>(0x10000), 0); // no Session (the difftest driver's machines)
    Machine m(bare);
    CHECK(hooks::filter(kMeter, m, 7) == 7);
    CHECK(Session::find(bare) == nullptr);
}

TEST_CASE("value hooks chain in subscription order and unsubscribe cleanly") {
    Rig r;
    int seen_actor = -1, starts = 0;
    hooks::Subscription a = r.h.subscribe(kMeter, [&](Machine &, int32_t v, const hooks::Args &args) {
        seen_actor = args.a;
        return v * 2;
    });
    hooks::Subscription b =
        r.h.subscribe(kMeter, [](Machine &, int32_t v, const hooks::Args &) { return v + 1; });
    hooks::Subscription e = r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) { ++starts; });
    CHECK(hooks::filter(kMeter, r.m, 10, {3, 50}) == 21);
    CHECK(seen_actor == 3);
    hooks::emit(kStart, r.m);
    CHECK(starts == 1);
    a.reset();
    CHECK(!a.active());
    CHECK(hooks::filter(kMeter, r.m, 10) == 11);
    { hooks::Subscription gone = std::move(b); } // dropping the token unsubscribes
    e.reset();
    CHECK(hooks::filter(kMeter, r.m, 10) == 10);
    hooks::emit(kStart, r.m);
    CHECK(starts == 1);
    CHECK(r.h.size() == 0);
}

TEST_CASE("a subscriber can remove itself mid-dispatch and stays alive until it returns") {
    Rig r;
    std::vector<int> calls;
    hooks::Subscription self;
    auto big = std::make_shared<std::vector<int>>(1000, 7); // state the callable owns
    self = r.h.subscribe(kStart, [&, big](Machine &, const hooks::Args &) {
        self.reset();                 // destroys this subscription...
        calls.push_back((*big)[999]); // ...while its captures are still in use
    });
    hooks::Subscription after =
        r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) { calls.push_back(2); });
    hooks::emit(kStart, r.m);
    CHECK(calls == std::vector<int>{7, 2});
    hooks::emit(kStart, r.m);
    CHECK(calls == std::vector<int>{7, 2, 2});
}

TEST_CASE("removing a later subscriber mid-dispatch skips it in that same dispatch") {
    Rig r;
    std::vector<int> calls;
    hooks::Subscription second;
    hooks::Subscription first = r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) {
        calls.push_back(1);
        second.reset();
    });
    second = r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) { calls.push_back(2); });
    hooks::emit(kStart, r.m);
    CHECK(calls == std::vector<int>{1});
}

TEST_CASE("a subscriber added mid-dispatch runs from the next dispatch on") {
    Rig r;
    std::vector<int> calls;
    std::vector<hooks::Subscription> added; // may reallocate while the dispatch runs
    hooks::Subscription adder = r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) {
        calls.push_back(1);
        for (int i = 0; i < 20; ++i) // enough to reallocate the registry's list too
            added.push_back(
                r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) { calls.push_back(9); }));
        adder.reset();
    });
    hooks::emit(kStart, r.m);
    CHECK(calls == std::vector<int>{1});
    calls.clear();
    hooks::emit(kStart, r.m);
    CHECK(calls == std::vector<int>(20, 9));
}

TEST_CASE("clear() mid-dispatch stops the rest of it") {
    Rig r;
    int later = 0;
    hooks::Subscription a = r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) { r.h.clear(); });
    hooks::Subscription b = r.h.subscribe(kStart, [&](Machine &, const hooks::Args &) { ++later; });
    hooks::emit(kStart, r.m);
    CHECK(later == 0);
    CHECK(!a.active());
    CHECK(!b.active());
    CHECK(r.h.size() == 0);
}

TEST_CASE("nested dispatch follows the same rules") {
    Rig r;
    std::vector<int> calls;
    hooks::Subscription inner;
    hooks::Subscription outer = r.h.subscribe(kStart, [&](Machine &m, const hooks::Args &) {
        calls.push_back(1);
        hooks::emit(kEnd, m); // a subscriber causing another hook call
        calls.push_back(3);
    });
    inner = r.h.subscribe(kEnd, [&](Machine &, const hooks::Args &) {
        calls.push_back(2);
        inner.reset();
        outer.reset();
    });
    hooks::emit(kStart, r.m);
    CHECK(calls == std::vector<int>{1, 2, 3});
    hooks::emit(kStart, r.m);
    hooks::emit(kEnd, r.m);
    CHECK(calls == std::vector<int>{1, 2, 3});
    CHECK(r.h.size() == 0);
}

TEST_CASE("each machine has its own hooks, tokens may outlive their machine") {
    std::optional<Rig> one(std::in_place);
    Rig two;
    hooks::Subscription s =
        one->h.subscribe(kMeter, [](Machine &, int32_t v, const hooks::Args &) { return v + 5; });
    CHECK(hooks::filter(kMeter, one->m, 1) == 6);
    CHECK(hooks::filter(kMeter, two.m, 1) == 1);
    one.reset(); // machine and registry gone
    CHECK(!s.active());
    s.reset(); // harmless
}
