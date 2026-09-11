#pragma once
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::lab {
// User-authorized development grant. Applied once per character at battle
// entry; the resulting original-format saves intentionally retain the grant.
class DebugStatFixture {
public:
    bool enabled=false;
    static constexpr unsigned multiplier=10;
    static constexpr unsigned development_limit=100000;
    // Max HP, max MP, base INT, base attack, base defense in the188-byte row.
    static constexpr std::array<unsigned,5> offsets{0x18,0x20,0x40,0x48,0x4c};
    struct Change{unsigned character;std::array<unsigned,5> before,after;};
    std::vector<Change> apply(core::Runtime& runtime){
        using namespace core;auto& memory=runtime.memory;std::vector<Change> changes;
        if(!enabled||memory.read(globals::game_mode)!=4)return changes;
        for(unsigned slot=0;slot<memory.read(globals::party_count);++slot){
            const auto id=memory.read(globals::party_actor_ids+slot*4);
            if(id>=16)throw Fault(id,"debug stat character outside original table");
            if(applied_&(1u<<id))continue;
            const auto record=BattleRules::party_record(id);Change change{id,{},{}};
            for(unsigned i=0;i<offsets.size();++i){
                const auto value=memory.read(record+offsets[i]);change.before[i]=value;
                change.after[i]=signed32(value)>0?unsigned(std::min<std::uint64_t>(std::uint64_t(value)*multiplier,development_limit)):value;
                memory.write(record+offsets[i],change.after[i]);
            }
            if(memory.read(record+0x1c))memory.write(record+0x1c,memory.read(record+0x18));
            memory.write(record+0x24,memory.read(record+0x20));
            runtime.actors.refresh_stats(id);applied_|=1u<<id;changes.push_back(change);
        }
        return changes;
    }
private:
    unsigned applied_=0;
};
} // namespace fsb::lab
