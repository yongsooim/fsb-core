#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/palette.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));RecoveredBattle code(memory);
        const auto reference=fsb::lab::read(argv[2]);std::size_t cursor=8;
        if(reference.size()<12||std::string(reference.begin(),reference.begin()+8)!=std::string("FSBSCL1\0",8))throw std::runtime_error("bad scalar reference");
        const auto word=[&](){unsigned value=0;for(unsigned i=0;i<4;++i)value|=unsigned(reference.at(cursor++))<<(i*8);return value;};
        const auto count=word();const auto before=memory.bytes(0x4a5000,3882100);
        for(unsigned index=0;index<count;++index){const auto entry=word(),argc=word();std::vector<unsigned> args;for(unsigned i=0;i<argc;++i)args.push_back(word());const auto expected=word();const auto actual=code.invoke(entry,args);
            if(actual!=expected){std::cerr<<"scalar "<<index<<" at "<<std::hex<<entry<<" actual="<<actual<<" expected="<<expected<<'\n';return 1;}}
        if(memory.bytes(0x4a5000,3882100)!=before||cursor!=reference.size())throw std::runtime_error("scalar state or fixture extent mismatch");
        std::cout<<"native_scalar_original_cases="<<count<<" passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
