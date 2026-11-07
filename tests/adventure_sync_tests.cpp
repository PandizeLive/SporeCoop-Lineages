#include "adventure_sync.h"
#include <cassert>
#include <cstdio>
using namespace adventure_sync;
static bool decode(const net::Writer& w) {
    net::Reader r(w.data.data(),w.size()); Packet p; return p.read(r);
}
int main() {
    Packet p; p.kind=Commit; p.mission={11,22,33}; p.run=7; p.visitor=9; p.sequence=1;
    p.goal={2,2,2,1161,UINT32_MAX,1};
    net::Writer valid; p.write(valid); assert(decode(valid));
    net::Reader r(valid.data.data(),valid.size()); Packet q;
    assert(q.read(r) && q.goal==p.goal && q.mission==p.mission && q.run==7 && q.visitor==9);
    for (size_t n=0;n<valid.size();++n) {
        net::Reader short_read(valid.data.data(),n); assert(!q.read(short_read));
    }
    auto reject=[&](Packet bad){net::Writer w; bad.write(w); assert(!decode(w));};
    q=p; q.sequence=0; reject(q);
    q=p; q.sequence=kMaxCredits+1; reject(q);
    q=p; q.run=0; reject(q);
    q=p; q.visitor=0; reject(q);
    q=p; q.goal.kind=3; reject(q); // Combat must not enter the dialogue-only path.
    q=p; q.goal.slot=8; reject(q);
    q=p; q.goal.act=16; reject(q);
    q=p; q.goal.required=2; reject(q);
    q=p; q.mission={}; reject(q);
    q=p; q.kind=Kind(255); reject(q);
    valid.u8(1); assert(!decode(valid));
    p.kind=Hello; p.run=0; p.sequence=0;
    net::Writer hello; p.write(hello); assert(decode(hello));
    puts("PASS Adventure packets: round-trip, every truncation, bounds, unsupported goals, trailing bytes.");
}
