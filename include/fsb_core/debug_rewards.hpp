#pragma once
#include "primitives.hpp"
namespace fsb::core {
class RecoveredBattle;
// Explicit development policy, separate from original guest state/save data.
class DebugRewards {
public:
    explicit DebugRewards(Memory& memory):memory_(memory){}
    void enable(bool value){enabled_=value;}
    bool enabled()const{return enabled_;}
    unsigned multiplier()const{return enabled_?10:1;}
    std::uint32_t scale_positive(std::uint32_t amount)const;
    bool service(Address entry,RecoveredBattle& call);
private:
    Memory& memory_;
    bool enabled_=false,awarding_=false;
};
} // namespace fsb::core
