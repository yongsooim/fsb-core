#include "fsb_core/runtime.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=4)return 2;
        const auto before=fsb::lab::read(argv[2]),expected=fsb::lab::read(argv[3]);
        const auto word=[](const auto& bytes,std::size_t& cursor){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(cursor++))<<(i*8);return value;};
        if(before.size()<12||std::string(before.begin(),before.begin()+8)!="FSBDRAW1")throw std::runtime_error("invalid input snapshot");
        Memory memory;std::size_t cursor=8;const auto regions=word(before,cursor);
        for(unsigned i=0;i<regions;++i){const auto base=word(before,cursor),size=word(before,cursor);if(cursor+size>before.size())throw std::runtime_error("truncated region");memory.map(base,{before.begin()+cursor,before.begin()+cursor+size},true);cursor+=size;}
        Runtime runtime(fsb::lab::read(argv[1]));runtime.memory=std::move(memory);
        const auto service=runtime.battle.recovered.service;
        runtime.battle.recovered.service=[&](Address entry,RecoveredBattle& call){
            if(entry==0x4320f6){call.result(0,4);return true;} // Same viewport boundary as the original x86 oracle.
            return service(entry,call);
        };
        runtime.battle.recovered.invoke(0x44ab67);
        if(expected.size()<12||std::string(expected.begin(),expected.begin()+8)!=std::string("FSBSET1\0",8))throw std::runtime_error("invalid setup oracle");
        cursor=8;const auto count=word(expected,cursor);unsigned mismatches=0,printed=0;std::size_t total=0;
        for(unsigned i=0;i<count;++i){
            const auto base=word(expected,cursor),size=word(expected,cursor);const auto actual=runtime.memory.bytes(base,size);total+=size;
            for(unsigned j=0;j<size;++j)if(actual[j]!=expected.at(cursor+j)){
                ++mismatches;if(printed++<24)std::cerr<<"setup byte 0x"<<std::hex<<base+j<<" actual="<<unsigned(actual[j])<<" original="<<unsigned(expected[cursor+j])<<std::dec<<'\n';
            }
            cursor+=size;
        }
        std::cout<<"battle_setup_bytes="<<total<<" mismatches="<<mismatches<<'\n';return mismatches?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
