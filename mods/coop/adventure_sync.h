// Portable, bounded Adventure messages. Native pointers never cross the network.
#pragma once
#include <array>
#include "net/buffer.h"

namespace adventure_sync {
constexpr uint32_t kMaxCredits = 128;
enum Kind : uint8_t { Hello, Welcome, Propose, Commit };
struct Goal {
    uint32_t act = 0, slot = 0, kind = 0, target = 0, secondary = 0, required = 0;
    bool operator==(const Goal&) const = default;
    // Other goals need actor ownership and lifetime synchronization first.
    bool supported() const { return act < 16 && slot < 8 && (kind == 1 || kind == 2) &&
                                    target < 0x100000 && secondary == UINT32_MAX && required == 1; }
    void write(net::Writer& w) const {
        w.u32(act); w.u32(slot); w.u32(kind); w.u32(target); w.u32(secondary); w.u32(required);
    }
    void read(net::Reader& r) {
        act=r.u32(); slot=r.u32(); kind=r.u32(); target=r.u32(); secondary=r.u32(); required=r.u32();
    }
};
struct Packet {
    Kind kind = Hello;
    std::array<uint32_t,3> mission{};
    uint32_t run = 0, visitor = 0, sequence = 0;
    Goal goal;
    void write(net::Writer& w) const {
        w.u8(uint8_t(kind)); for (auto v:mission) w.u32(v);
        w.u32(run); w.u32(visitor); w.u32(sequence);
        if (kind == Propose || kind == Commit) goal.write(w);
    }
    bool read(net::Reader& r) {
        kind=Kind(r.u8()); for (auto& v:mission) v=r.u32();
        run=r.u32(); visitor=r.u32(); sequence=r.u32();
        if (kind == Propose || kind == Commit) goal.read(r);
        return r.ok() && !r.left() && kind <= Commit && visitor &&
            sequence <= kMaxCredits && (mission[0] || mission[1] || mission[2]) &&
            (kind == Hello || run) &&
            ((kind != Propose && kind != Commit) || goal.supported()) &&
            (kind != Commit || sequence);
    }
};
}
