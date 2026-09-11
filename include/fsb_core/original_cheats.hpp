#pragma once
#include "fsb_core/primitives.hpp"
#include <vector>

namespace fsb::original_cheats {
using namespace fsb::core;
inline constexpr Address enabled = 0x6d9ec4, history = 0x804a78, cursor = 0x804a98;
inline constexpr Address lengths = 0x5d22a8, phrases = 0x5d2310;
// 407b06..407b30: launch switch requested and SYSTEMTIME.wYear > 1998.
inline void configure(Memory& memory, bool requested, unsigned year) {
    memory.write(enabled,requested && std::uint16_t(year)>1998);
}

inline void effect(Memory& m, unsigned code) {
    const auto set_item = [&](unsigned id, unsigned count) { m.write(0x806e30 + id * 4, count); };
    switch (code) {
    case 0: // 45d748: copy the original ten-slot skill table for all 16 profiles.
        for (unsigned actor = 0; actor < 16; ++actor)
            for (unsigned word = 0; word < 10; ++word)
                m.write(0x607a9c + actor * 0xbc + word * 4, m.read(0x6085c8 + actor * 40 + word * 4));
        break;
    case 1: m.write(0x803a18, m.read(0x803a18) + 100000u); break;
    case 2: for (unsigned id : {66u,200u,231u,257u}) set_item(id,10); break;
    case 3: for (unsigned id : {298u,297u,324u}) set_item(id,1); break;
    case 4:
        for (int i = 0; i < signed32(m.read(0x803a20)); ++i) {
            const auto id = m.read(0x5d2258 + unsigned(i) * 4), record = 0x607a08 + id * 0xbc;
            const auto hp = m.read(record + 0x18);
            m.write(record + 0x1c, hp); m.write(0x776418 + id * 4, hp);
            m.write(record + 0x24, m.read(record + 0x20));
            if (signed32(m.read(record + 0x90)) > 1) m.write(record + 0x2c,16);
            m.write(record + 9, m.read(record + 9,1) & 0xe0,1);
            m.write(record + 12, m.read(record + 12) & 0xfff00000);
            m.write(record + 8, (m.read(record + 8) & 0xffffe0ff) | 0x3401f);
            m.write(record + 15, m.read(record + 15,1) | 15,1);
            m.write(record + 16,255,1);
        }
        break;
    case 5: set_item(225,10); break;
    case 6: m.write(0x804a9c,!m.read(0x804a9c)); break;
    case 7: m.write(0x804aa0,!m.read(0x804aa0)); break;
    case 8: set_item(322,10); break;
    case 9: m.write(0x804aa4,m.read(0x804aa4)==4?0:4); break;
    case 10: m.write(0x77ebf8,1000000); break; // Pending battle EXP, not immediate level-ups.
    case 11: m.write(0x804aa4,m.read(0x804aa4)==2?0:2); break;
    case 12: m.write(0x804aa4,m.read(0x804aa4)==1?0:1); break;
    case 13: set_item(128,1); break;
    case 14: set_item(136,1); break;
    case 15: set_item(144,1); break;
    case 16: set_item(152,1); break;
    case 17: set_item(160,1); break;
    case 18: set_item(167,1); break;
    case 19: set_item(175,1); break;
    case 20: set_item(182,1); break;
    case 21: set_item(191,1); break;
    case 22: set_item(199,1); break;
    case 23: m.write(0x6da2e0,m.read(0x6da2e0)|2); break;
    case 24: m.write(0x6da2e0,m.read(0x6da2e0)&~2u); break;
    case 25: for (unsigned id=318;id<=321;++id) set_item(id,m.read(0x806e30+id*4)+1); break;
    }
}

// 45f600; the callback boundaries are the already-ported timer and text services.
template<class Call> void draw_time(Memory& m, unsigned x, unsigned y, Call&& call) {
    call(0x460d4c,{});
    const auto h=m.read(0x804c70), min=m.read(0x804c74), sec=m.read(0x804c78), ms=m.read(0x804ab8);
    call(0x4067ec,{x,y,0x10000ff,0x10000fe,0x5d249c,h,min/10,min%10,sec/10,sec%10,ms/100,(ms/10)%10,ms%10});
}

// 45fbe0..45ffaf, reached at the end of the original 45f82d game frame.
// Its 32-byte ring deliberately compares the preceding keys, not the key just
// stored. A completed phrase therefore fires on the next non-repeat keydown.
template<class Call> void frame(Memory& m, Call&& call) {
    if (!m.read(enabled)) return;
    const auto left=m.read(0x6e1468), top=m.read(0x74b470);
    if (m.read(0x804a9c)) call(0x45f600,{left+130,top+2});
    if (m.read(0x804aa0)) {
        unsigned found=0;
        for(unsigned i=0;i<1023;++i) found+=(m.read(0x7ab4d8+(i/32)*4)>>(i%32))&1;
        call(0x406576,{left+12,top+2,0x10000fe,0x10000ff,0x5d24c0,found});
    }
    const auto encounter=m.read(0x804aa4);
    if(encounter==4)call(0x406576,{left+2,top+2,0x10000fe,0x10000ff,0x5d24bc});
    if(encounter==2)call(0x406576,{left+2,top+2,0x10000fe,0x10000ff,0x5d24b8});
    if(encounter==1)call(0x406576,{left+2,top+2,0x10000fe,0x10000ff,0x5d24b4});
    if(m.read(0x6da2dc)!=0x100 || (m.read(0x6d6688)&0x40000000))return;
    const auto at=m.read(cursor);
    if(at>=32)throw Fault(cursor,"invalid original cheat ring cursor");
    m.write(history+at,(m.read(0x6d66b0,1)+0x32)&255,1);
    for(unsigned code=0;code<26;++code) {
        const auto length=m.read(lengths+code*4);
        if(length>15)throw Fault(lengths+code*4,"invalid original cheat phrase length");
        unsigned matches=0;
        for(unsigned j=0;j<length;++j)
            matches+=m.read(history+(at+31-j)%32,1)==m.read(phrases+code*15+length-1-j,1);
        if(matches<length)continue;
        call(0x435373,{0x14b});
        for(unsigned i=0;i<8;++i)m.write(history+i*4,0);
        effect(m,code);
    }
    m.write(cursor,(at+1)%32);
}
}
