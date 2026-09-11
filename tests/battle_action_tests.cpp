#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actors.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;const auto input=fsb::lab::read(argv[1]),reference=fsb::lab::read(argv[2]);
        const auto word=[](const auto& bytes,std::size_t& cursor){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(cursor++))<<(i*8);return value;};
        Memory initial;std::size_t cursor=8;const auto regions=word(input,cursor);
        for(unsigned i=0;i<regions;++i){const auto base=word(input,cursor),size=word(input,cursor);initial.map(base,{input.begin()+cursor,input.begin()+cursor+size},true);cursor+=size;}
        if(reference.size()<12||std::string(reference.begin(),reference.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid battle action oracle");
        cursor=8;const auto cases=word(reference,cursor);unsigned failed=0;std::uint64_t compared=0;
        for(unsigned index=0;index<cases;++index){
            Memory memory=initial;RecoveredBattle code(memory);const auto entry=word(reference,cursor),mask=word(reference,cursor),argc=word(reference,cursor);
            if(entry==0x45cc1b)code.service=[&](Address target,RecoveredBattle& call){if(target!=0x45cc1b)return false;Actors actors(memory);if(memory.read(0x80465c)==9)actors.resolve_battle_frame(call.argument(0));else actors.resolve_field_frame(call.argument(0));call.result(0,4);return true;};
            if(entry==0x45abf9)code.service=[](Address target,RecoveredBattle& call){if(target!=0x435373&&target!=0x4353cb)return false;call.result(0,4);return true;};
            if(entry==0x476674||entry==0x476a83||entry==0x476bba||entry==0x461854)code.service=[](Address target,RecoveredBattle& call){if(target!=0x435373)return false;call.result(0,4);return true;};
            if(entry==0x45f07e)code.service=[](Address target,RecoveredBattle& call){if(target!=0x401980)return false;if(call.argument(0))throw Fault(target,"unexpected snapshot assertion");call.result(0);return true;};
            if(entry==0x407ffd)code.service=[](Address target,RecoveredBattle& call){if(target!=0x40110f)return false;if(call.argument(0)!=0x10||call.argument(1)||call.argument(2))throw Fault(target,"unexpected shutdown message");call.result(1,12);return true;};
            if(entry==0x4302d2||entry==0x430385)code.service=[&](Address target,RecoveredBattle& call){if(target!=0x42feb9)return false;call.result(lookup_actor(memory,call.argument(0)),4);return true;};
            if(entry==0x42ff9b)code.service=[&](Address target,RecoveredBattle& call){if(target!=0x42ff9b)return false;Actors(memory).visible(call.argument(0),call.argument(1)!=0);call.result(0,8);return true;};
            if(entry==0x44ab67)code.service=[](Address target,RecoveredBattle& call){
                if(target==0x4320f6){call.result(0,4);return true;}
                if(target==0x401980){if(call.argument(0))throw Fault(target,"unexpected original setup assertion");call.result(0);return true;}
                return false;
            };
            std::vector<std::uint32_t> args;for(unsigned i=0;i<argc;++i)args.push_back(word(reference,cursor));
            const auto writes=word(reference,cursor);for(unsigned i=0;i<writes;++i){const auto a=word(reference,cursor),width=word(reference,cursor),value=word(reference,cursor);memory.write(a,value,width);}
            auto expected=memory.bytes(0x4a5000,3882100);const auto expected_return=word(reference,cursor),changes=word(reference,cursor);
            for(unsigned i=0;i<changes;++i){const auto a=word(reference,cursor);expected.at(a-0x4a5000)=reference.at(cursor++);}
            const auto actual_return=code.invoke(entry,args)&mask;const auto actual=memory.bytes(0x4a5000,expected.size());compared+=actual.size();
            unsigned mismatches=0;for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){
                if(mismatches++<8)std::cerr<<"case="<<index<<" entry=0x"<<std::hex<<entry<<" address=0x"<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<std::dec<<'\n';
            }
            if(actual_return!=expected_return)std::cerr<<"case="<<index<<" return="<<actual_return<<" expected="<<expected_return<<'\n';
            if(mismatches||actual_return!=expected_return){++failed;if(failed>=5)break;}
        }
        std::cout<<"battle_action_cases="<<cases<<" failed="<<failed<<" guest_bytes_compared="<<compared<<'\n';return failed?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
