// Portable, bounded Adventure messages. Native pointers never cross the network.
#pragma once
#include <array>
#include "net/buffer.h"

namespace adventure_sync {
constexpr uint32_t kMaxCredits = 512;
constexpr uint32_t kMaxRequired = 255;
// Hello / Welcome / Propose / Commit: shared objective credits (host journal).
// Death (BUG-9089, 44): a scenario object of this id died n times in the sender's game.
// Outcome (BUG-9089, 44): the host's mission ended (state 5 success, 6 failure).
enum Kind : uint8_t { Hello, Welcome, Propose, Commit, Death, Outcome };
// One credit of one goal. A credit for an object (a defeated building or creature) is that object's,
// once, whoever's game saw it (object = the scenario marker id, see adventure.cpp object_id); a credit
// without an object is the goal's count-th one (count = the native count before it).
struct Goal {
    uint32_t act = 0, slot = 0, kind = 0, target = 0, secondary = 0, required = 0;
    uint32_t object = 0, count = 0;
    bool operator==(const Goal&) const = default;
    bool same_credit(const Goal& o) const {
        return act == o.act && slot == o.slot && kind == o.kind && target == o.target && secondary == o.secondary &&
               required == o.required && (object ? object == o.object : !o.object && count == o.count);
    }
    bool supported() const {
        return act < 16 && slot < 8 && kind >= 1 && kind <= 15 && target < 0x100000 &&
               (secondary == UINT32_MAX || secondary < 0x100000) && required >= 1 && required <= kMaxRequired &&
               count < required;
    }
    void write(net::Writer& w) const {
        w.u32(act); w.u32(slot); w.u32(kind); w.u32(target); w.u32(secondary); w.u32(required);
        w.u32(object); w.u32(count);
    }
    void read(net::Reader& r) {
        act=r.u32(); slot=r.u32(); kind=r.u32(); target=r.u32(); secondary=r.u32(); required=r.u32();
        object=r.u32(); count=r.u32();
    }
};
struct Packet {
    Kind kind = Hello;
    std::array<uint32_t,3> mission{};
    uint32_t run = 0, visitor = 0, sequence = 0;
    Goal goal;
    uint32_t object = 0, deaths = 0; // Death
    uint32_t state = 0;              // Outcome
    void write(net::Writer& w) const {
        w.u8(uint8_t(kind)); for (auto v:mission) w.u32(v);
        w.u32(run); w.u32(visitor); w.u32(sequence);
        if (kind == Propose || kind == Commit) goal.write(w);
        if (kind == Death) { w.u32(object); w.u32(deaths); }
        if (kind == Outcome) w.u32(state);
    }
    bool read(net::Reader& r) {
        kind=Kind(r.u8()); for (auto& v:mission) v=r.u32();
        run=r.u32(); visitor=r.u32(); sequence=r.u32();
        if (kind == Propose || kind == Commit) goal.read(r);
        if (kind == Death) { object=r.u32(); deaths=r.u32(); }
        if (kind == Outcome) state=r.u32();
        return r.ok() && !r.left() && kind <= Outcome && visitor &&
            sequence <= kMaxCredits && (mission[0] || mission[1] || mission[2]) &&
            (kind == Hello || run) &&
            ((kind != Propose && kind != Commit) || goal.supported()) &&
            (kind != Commit || sequence) &&
            (kind != Death || (object && deaths >= 1 && deaths <= 64)) &&
            (kind != Outcome || state == 5 || state == 6);
    }
};
}
