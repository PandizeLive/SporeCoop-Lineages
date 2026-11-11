// BUG-9119: the pure part of the progress debt (host unit tests: tests/progress_debt_tests.cpp)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace progress_debt {

// What a death costs (owner, 2026-11-11: progression debt, not DNA): 10% of the progress earned in this
// stage so far (Creature: cPlayer +0x10F0, the bar's DNA earned; Cell: the food), whole points, at least
// 1 once there is any. Spendable DNA (the editors') never changes.
inline float progress_cost(float done)
{
    if (!std::isfinite(done) || done < 1)
        return 0;
    return std::max(1.f, std::round(done * 0.10f));
}

// Pure ledger, one stage and world at a time (host tests use it directly)
struct Ledger {
    uint32_t mode = 0, world = 0;
    float owed = 0;
    // a new place clears what was owed in the old one
    bool place(uint32_t m, uint32_t w)
    {
        if (m == mode && w == world) return false;
        mode = m; world = w; owed = 0;
        return true;
    }
    void add(float amount) { if (amount > 0) owed += amount; }
    // a gain repays the debt first; what is left of it goes to the party
    float absorb(float gain)
    {
        if (gain <= 0 || owed <= 0) return gain;
        float repaid = gain < owed ? gain : owed;
        owed -= repaid;
        if (owed < 0.001f) owed = 0;
        return gain - repaid;
    }
};

} // namespace progress_debt
