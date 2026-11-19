// Dispatches game presentation events to observers attached to each session.
#include "game/hooks.h"

#include <algorithm>
#include <array>
#include <vector>

#include "game/machine.h"
#include "game/session.h"

namespace sawgod::game::hooks {

namespace {
template <class Fn> struct Entry {
    int id;
    Fn fn;
    bool alive = true;
};
// Entries are shared so a dispatch can hold the one it is calling while the list changes under it.
template <class Fn> using List = std::vector<std::shared_ptr<Entry<Fn>>>;
} // namespace

struct Core {
    std::array<List<EventFn>, size_t(Event::Count)> events;
    std::array<List<ValueFn>, size_t(Value::Count)> values;
    std::array<List<VbFn>, size_t(VB::Count) + 1> vbs; // (+1: VB may be empty)
    int next_id = 1;
    int depth = 0;      // dispatches running (nested ones included)
    bool dirty = false; // dead entries left in place during a dispatch

    template <class F> void each_list(F f) {
        for (auto &l : events)
            f(l);
        for (auto &l : values)
            f(l);
        for (auto &l : vbs)
            f(l);
    }
    void remove(int id) {
        each_list([&](auto &list) {
            for (auto &e : list)
                if (e->id == id && e->alive) {
                    e->alive = false;
                    dirty = true;
                }
        });
        compact();
    }
    void compact() {
        if (depth || !dirty)
            return;
        each_list([](auto &list) {
            list.erase(std::remove_if(list.begin(), list.end(), [](const auto &e) { return !e->alive; }),
                       list.end());
        });
        dirty = false;
    }
    template <class Fn> int add(List<Fn> &list, Fn fn) {
        list.push_back(std::make_shared<Entry<Fn>>(Entry<Fn>{next_id, std::move(fn)}));
        return next_id++;
    }
    // Calls f(entry) for the entries present when the dispatch started that are still alive when reached.
    template <class Fn, class F> void dispatch(List<Fn> &list, F f) {
        if (list.empty())
            return;
        struct Depth {
            Core &c;
            explicit Depth(Core &c) : c(c) { ++c.depth; }
            ~Depth() {
                --c.depth;
                c.compact();
            }
        } guard(*this);
        const size_t n = list.size();
        for (size_t i = 0; i < n && i < list.size(); ++i) {
            const std::shared_ptr<Entry<Fn>> e = list[i]; // keeps it alive if it unsubscribes itself
            if (e->alive)
                f(*e);
        }
    }
};

Subscription::Subscription(Subscription &&o) noexcept : core_(std::move(o.core_)), id_(o.id_) { o.id_ = 0; }

Subscription &Subscription::operator=(Subscription &&o) noexcept {
    if (this != &o) {
        reset();
        core_ = std::move(o.core_);
        id_ = o.id_;
        o.id_ = 0;
    }
    return *this;
}

void Subscription::reset() {
    if (auto core = core_.lock(); core && id_)
        core->remove(id_);
    core_.reset();
    id_ = 0;
}

bool Subscription::active() const {
    auto core = core_.lock();
    if (!core || !id_)
        return false;
    bool found = false;
    core->each_list([&](auto &list) {
        for (auto &e : list)
            found |= e->id == id_ && e->alive;
    });
    return found;
}

Registry::Registry() : core_(std::make_shared<Core>()) {}
Registry::~Registry() = default;

Subscription Registry::subscribe(Event e, EventFn fn) {
    return {core_, core_->add(core_->events[size_t(e)], std::move(fn))};
}
Subscription Registry::subscribe(Value v, ValueFn fn) {
    return {core_, core_->add(core_->values[size_t(v)], std::move(fn))};
}
Subscription Registry::subscribe(VB v, VbFn fn) {
    return {core_, core_->add(core_->vbs[size_t(v)], std::move(fn))};
}

void Registry::clear() {
    core_->each_list([&](auto &list) {
        for (auto &e : list)
            e->alive = false;
    });
    core_->dirty = true;
    core_->compact();
}

size_t Registry::size() const {
    size_t n = 0;
    core_->each_list([&](auto &list) {
        for (auto &e : list)
            n += e->alive;
    });
    return n;
}

void Registry::emit(Event e, Machine &m, const Args &args) {
    core_->dispatch(core_->events[size_t(e)], [&](auto &s) { s.fn(m, args); });
}

int32_t Registry::filter(Value v, Machine &m, int32_t value, const Args &args) {
    core_->dispatch(core_->values[size_t(v)], [&](auto &s) { value = s.fn(m, value, args); });
    return value;
}

bool Registry::should(VB v, Machine &m, bool vanilla, const Args &args) {
    core_->dispatch(core_->vbs[size_t(v)], [&](auto &s) { vanilla = s.fn(m, vanilla, args); });
    return vanilla;
}

void emit(Event e, Machine &m, const Args &args) {
    if (Session *s = Session::find(m.wsc()))
        s->hooks().emit(e, m, args);
}

int32_t filter(Value v, Machine &m, int32_t value, const Args &args) {
    Session *s = Session::find(m.wsc());
    return s ? s->hooks().filter(v, m, value, args) : value;
}

bool should(VB v, Machine &m, bool vanilla, const Args &args) {
    Session *s = Session::find(m.wsc());
    return s ? s->hooks().should(v, m, vanilla, args) : vanilla;
}

} // namespace sawgod::game::hooks
