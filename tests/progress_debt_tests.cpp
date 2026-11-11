// BUG-9119: the progress debt's pure rules (mods/coop/progress_debt_logic.h)
#include <cstdio>
#include "progress_debt_logic.h"
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
using namespace progress_debt;
int main()
{
    // a death: 10% of the stage's progress so far, whole points, at least 1 (never DNA)
    CHECK(progress_cost(321) == 32);
    CHECK(progress_cost(204) == 20);
    CHECK(progress_cost(5) == 1);
    CHECK(progress_cost(0.5f) == 0);
    CHECK(progress_cost(0) == 0);
    CHECK(progress_cost(-5) == 0);
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
