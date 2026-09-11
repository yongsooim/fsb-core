#include "fsb_core/debug_rewards.hpp"
#include "fsb_core/recovered_battle.hpp"
#include <algorithm>

namespace fsb::core {
namespace {
constexpr Address award_party_experience=0x44de92,apply_experience=0x44de15;
constexpr Address reward_exp=0x77ebf8,reward_gold=0x77ec00,level_thresholds=0x5c0238;
}
std::uint32_t DebugRewards::scale_positive(std::uint32_t amount)const{
    if(!enabled_||signed32(amount)<=0)return amount;
    return std::uint32_t(std::min<std::uint64_t>(std::uint64_t(amount)*10,0x7fffffffu));
}
bool DebugRewards::service(Address entry,RecoveredBattle& call){
    if(entry==award_party_experience&&enabled_&&!awarding_){
        // Both normal and scripted battles pass this payout boundary. Their
        // reward totals are prepared at different times, so don't scale earlier.
        memory_.write(reward_exp,scale_positive(memory_.read(reward_exp)));
        memory_.write(reward_gold,scale_positive(memory_.read(reward_gold)));
        awarding_=true;
        try{const auto result=call.callback(entry);awarding_=false;call.result(result);}
        catch(...){awarding_=false;throw;}
        return true;
    }
    if(entry!=apply_experience||!awarding_)return false;
    const auto id=call.argument(0),amount=call.argument(1);if(id>=16||signed32(amount)<=0)return false;
    const auto record=0x607a08+id*188,old_level=memory_.read(record+0x28);
    if(old_level>=99)return false;
    // Original44de15 discards XP beyond a second level. Development mode
    // keeps the boosted award and applies all crossed thresholds instead.
    const auto total=std::uint32_t(std::min<std::uint64_t>(std::uint64_t(memory_.read(record+0x34))+amount,memory_.read(level_thresholds+99*4)-1));
    auto level=old_level;while(level<99&&total>=memory_.read(level_thresholds+level*4))++level;
    memory_.write(record+0x34,total);
    memory_.write(record+0x38,level==99?0:memory_.read(level_thresholds+level*4)-total);
    // 450b1c applies incremental stat gains, skills and Son's equipment growth;
    // these are not recomputed from the final level. Run each intermediate
    // level through that original function. Victory still displays/applies
    // the final level once using its normal result sequence.
    if(level>old_level+1){
        const auto text=memory_.allocate_zeroed(4096);
        try{
            for(unsigned reached=old_level+1;reached<level;++reached){
                memory_.write(record+0x28,reached);memory_.write(text,0,1);
                call.callback(0x450b1c,{id,reached,text});
            }
        }catch(...){memory_.release_allocation(text);throw;}
        memory_.release_allocation(text);
    }
    memory_.write(record+0x28,level);
    call.result(level!=old_level,8);return true;
}
} // namespace fsb::core
