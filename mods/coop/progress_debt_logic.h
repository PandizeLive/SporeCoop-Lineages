// BUG-9119: the pure part of the progress debt (host unit tests: tests/progress_debt_tests.cpp)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace progress_debt {

// what a debt costs, given the stage's numbers (pure, host tests)
// 10% of the DNA budget, whole points (0 below 1 DNA)
inline float creature_cost(float dna)
{
    if (!std::isfinite(dna) || dna < 1)
        return 0;
    return std::min(std::floor(dna), std::ceil(dna * 0.10f));
}
// 10% of the editor's DNA (DNAForFood(food) - spent), in food points at the stage's average rate
inline int cell_cost(int food, int dna_for_food, int spent)
{
    if (food <= 0 || dna_for_food <= 0)
        return 0;
    const int editor = dna_for_food - std::max(0, spent);
    if (editor <= 0)
        return 0;
    const int dna = (editor + 9) / 10;
    const int cost = (int)std::ceil((double)dna * food / dna_for_food);
    return std::clamp(cost, 1, food);
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
