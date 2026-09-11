#include "fsb_core/input.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));const auto data=fsb::lab::read(argv[2]);unsigned cursor=8;
        if(data.size()<16||std::string(data.begin(),data.begin()+7)!="FSBKEY1")throw std::runtime_error("invalid keyboard oracle");
        const auto u32=[&](){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(data.at(cursor++))<<(i*8);return v;};
        const auto count=u32(),words=u32();std::vector<Address> addresses;for(unsigned i=0;i<words;++i)addresses.push_back(u32());unsigned failures=0;
        for(unsigned i=0;i<count;++i){
            std::uint32_t args[7];for(auto& a:args)a=u32();apply_input_state(memory,keyboard_message(args[0],args[1],args[2],args[3],args[4],args[5],args[6]));
            for(auto address:addresses){const auto expected=u32(),actual=memory.read(address);if(actual!=expected){if(failures++<5)std::cerr<<"input case"<<i<<" address0x"<<std::hex<<address<<" got"<<actual<<" expected"<<expected<<std::dec<<'\n';}}
        }
        if(cursor!=data.size())throw std::runtime_error("trailing keyboard oracle data");
        const auto check=[&](bool value){if(!value)++failures;};
        auto mouse=mouse_button_message(0,true);check(mouse.key==0x83);apply_input_state(memory,mouse);check(memory.read(0x74b4a0,1)==1);
        mouse=mouse_button_message(0,false);check(mouse.key==0x98);apply_input_state(memory,mouse);check(memory.read(0x74b4a0,1)==0);
        mouse=mouse_button_message(1,true);check(mouse.key==0x43);apply_input_state(memory,mouse);check(memory.read(0x74b4a1,1)==1);
        apply_input_state(memory,mouse_button_message(1,false));check(memory.read(0x74b4a1,1)==0);
        memory.write(0x6d9eb8,1);apply_input_state(memory,keyboard_message(19,0xc5,true));check(memory.read(0x6d9eb8)==3);
        apply_input_state(memory,keyboard_message(19,0xc5,false));check(memory.read(0x6d9eb8)==3);
        apply_input_state(memory,keyboard_message(19,0xc5,true));check(memory.read(0x6d9eb8)==1);
        std::cout<<"input_oracle_cases="<<count<<" words_per_case="<<words<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
