// BUG-9119: progress debt. A downed player whose partners did not come in time (or who gave up) no
// longer takes DNA / food away from the party: the party owes progress instead. The debt is shown as
// a deep red stretch at the end of the stage's progress bar; every new gain repays it first, and only
// a gain beyond the debt adds DNA / food again. Nothing already earned is ever removed.
//
// The host keeps the ledger (one per stage and world: a stage change, another world or the session's
// end clears it) and announces it to the guests (MSG_PROGRESS_DEBT). Units are the stage's own
// progress: DNA in Creature, food points in Cell.
#pragma once

#include <cstdint>
#include <string>
#include "net/session.h"
#include "progress_debt_logic.h"
#include "sporekit.h"

namespace progress_debt {

void init(const SkHost* host, net::Session* session, void (*notify)(const std::string&, int));
void tick(uint32_t mode, uint32_t world, double now); // every frame (coop's frame)
void reset();                                          // the session ended

bool enabled();                 // `coop.fix core.progress_debt` (off: the old loss)
float add(float amount);        // host: a new debt in this stage (returns it, 0 when off / not host)
float add_death();              // host: a death's debt, 10% of this stage's progress so far (progress_cost)
float absorb(float gain);       // host: a gain into the party's progress; returns what is left of it
int absorb_food(int gain);      // the same for whole food points
bool owing(uint32_t mode);      // this stage has a debt now (host: its ledger; guest: the host's word)
float owed();                   // the debt now in the current stage (0: none)
void message(uint8_t from, net::Reader& r); // MSG_PROGRESS_DEBT

void draw();                    // SK_EV_OVERLAY: the red stretch on the progress bar
std::string debug();            // coop.debt
int command(int argc, const char* const* argv, std::string& out); // coop.debt add <n> | clear | bar

} // namespace progress_debt
