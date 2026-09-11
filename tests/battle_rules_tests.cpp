#include "fsb_core/battle_rules.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;
        auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));BattleRules rules(memory);const auto data=fsb::lab::read(argv[2]);std::size_t cursor=8;
        if(data.size()<12||std::string(data.begin(),data.begin()+8)!=std::string("FSBBAT1\0",8))throw std::runtime_error("invalid battle rule fixture");
        const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(data.at(cursor++))<<(i*8);return value;};
        const auto count=word();unsigned failures=0;
        for(unsigned i=0;i<count;++i){
            const auto kind=word(),arity=word();std::vector<std::uint32_t> args(arity);for(auto& value:args)value=word();
            const auto writes=word();for(unsigned j=0;j<writes;++j){const auto address=word(),value=word();memory.write(address,value);}
            std::uint32_t result=0;
            switch(kind){
            case 0:result=rules.precheck();break;
            case 1:rules.decay_status();break;
            case 2:rules.advance_gauges();break;
            case 3:{const auto next=rules.next_context();memory.write(args.at(0),next.value_or(0xffffffffu));result=next.has_value();break;}
            case 4:rules.compute_rewards();break;
            case 5:result=std::uint32_t(rules.relation(args.at(0),args.at(1)));break;
            case 6:result=BattleRules::relation_hit_bonus(args.at(0));break;
            case 7:result=BattleRules::relation_damage_percent(args.at(0));break;
            case 8:result=BattleRules::counter_chance(args.at(0),args.at(1));break;
            case 9:result=BattleRules::guard_percent(args.at(0),args.at(1));break;
            case 10:result=std::uint32_t(BattleRules::accuracy_bonus(args.at(0),args.at(1)));break;
            default:throw std::runtime_error("unknown battle fixture kind");
            }
            const auto expected=word(),observed=word();bool equal=result==expected;Address first=0;std::uint32_t expected_word=0;
            for(unsigned j=0;j<observed;++j){const auto address=word(),value=word();if(memory.read(address)!=value){equal=false;if(!first){first=address;expected_word=value;}}}
            if(!equal&&failures++<12){std::cerr<<"battle rule case="<<i<<" kind="<<kind<<" result="<<result<<'/'<<expected;
                if(first)std::cerr<<" address=0x"<<std::hex<<first<<" value="<<memory.read(first)<<'/'<<expected_word<<std::dec;std::cerr<<'\n';}
        }
        if(cursor!=data.size())throw std::runtime_error("trailing battle fixture data");
        std::cout<<"battle_rule_cases="<<count<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
