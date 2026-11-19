// Visual rows around the native party categories. With no host rows, every index is native.
#pragma once
#include <algorithm>

namespace sawgod::game {
struct PartyMenuRows {
    bool config;
    int hosts;
    PartyMenuRows(bool show_config, int host_rows)
        : config(show_config), hosts(std::clamp(host_rows, 0, 2)) {}
    int options() const { return config ? 6 : 5; }
    int save() const { return options() + (hosts > 0); }
    int load() const { return hosts == 2 ? save() + 1 : -1; }
    int count() const { return (config ? 7 : 6) + hosts; }
    // Host actions have no entry in the ROM's dispatch table.
    int native(int row) const {
        if ((hosts && row == options()) || row == load())
            return -1;
        return row == save() ? 6 : row;
    }
    int pitch() const { return hosts ? (count() > 8 ? 10 : 11) : (config ? 12 : 14); }
};
} // namespace sawgod::game
