// BUG-9119: the progress debt's pure rules (mods/coop/progress_debt_logic.h)
#include <cstdio>
#include "progress_debt_logic.h"
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
using namespace progress_debt;
int main()
{
    // Creature: 10% of the DNA, whole points (the old loss, now owed)
    CHECK(creature_cost(29) == 3);
    CHECK(creature_cost(100) == 10);
    CHECK(creature_cost(1) == 1);
    CHECK(creature_cost(0.5f) == 0);
    CHECK(creature_cost(0) == 0);
    CHECK(creature_cost(-5) == 0);
    // Cell: 10% of the editor's DNA, in food at the average rate
    CHECK(cell_cost(204, 40, 0) == 21);  // 4 DNA of 40 -> 4 * 204 / 40 = 20.4 -> 21 food
    CHECK(cell_cost(204, 40, 40) == 0);  // everything spent: nothing owed
    CHECK(cell_cost(204, 40, 35) == 6);  // 5 left -> 1 DNA -> 5.1 -> 6 food
    CHECK(cell_cost(0, 0, 0) == 0);
    CHECK(cell_cost(10, 1, 0) == 10);    // never more than the bar holds
    // the ledger: gains repay first, nothing earned is taken away
    Ledger l;
    CHECK(l.place(1, 7));
    l.add(3);
    CHECK(l.absorb(2) == 0 && l.owed == 1);        // all of it repays
    CHECK(l.absorb(5) == 4 && l.owed == 0);        // the rest counts again
    CHECK(l.absorb(5) == 5);                        // no debt: whole
    l.add(4);
    CHECK(!l.place(1, 7) && l.owed == 4);           // the same stage and world (an editor between) keeps it
    CHECK(l.place(1, 8) && l.owed == 0);            // another world clears it
    l.add(-2);
    CHECK(l.owed == 0);
    CHECK(l.absorb(-1) == -1);                      // a spend is not a repayment
    if (!failures) printf("progress_debt_tests: all passed\n");
    return failures ? 1 : 0;
}
