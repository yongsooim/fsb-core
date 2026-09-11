#include "fsb_core/script_control.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv) {
    try {
        if(argc!=3&&argc!=4)return 2;
        const bool bridge=argc==4&&std::string(argv[3])=="--native-entry";
        if(argc==4&&!bridge)return 2;
        const auto initial=Memory::from_pe32(fsb::lab::read(argv[1]));const auto bytes=fsb::lab::read(argv[2]);std::size_t cursor=8;
        if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBCHP1\0",8))throw std::runtime_error("invalid control helper fixture");
        const auto word=[&](){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(i*8);return v;};
        const auto count=word();
        for(unsigned index=0;index<count;++index) {
            auto memory=initial;RecoveredBattle code(memory);
            const auto entry=word(),argc=word();std::vector<unsigned> args;for(unsigned i=0;i<argc;++i)args.push_back(word());
            const auto writes=word();for(unsigned i=0;i<writes;++i){const auto at=word(),width=word(),value=word();memory.write(at,value,width);}
            auto expected=memory.bytes(0x4a5000,3882100);const auto wanted_fault=word(),wanted_return=word(),changes=word();
            for(unsigned i=0;i<changes;++i){const auto at=word();expected.at(at-0x4a5000)=bytes.at(cursor++);}
            bool faulted=false;unsigned result=0;
            try {
                if(bridge)result=code.invoke(entry,args);
                else if(entry==0x41a0fb)result=ScriptControl::find_marker(memory,args[0],std::uint16_t(args[1]),std::uint16_t(args[2]));
                else {
                    ScriptControl control(memory,args[0]);
                    if(entry==0x41b38c){control.push_call(args[1]);result=args[0]+vm_offset::block_depth;}
                    else if(entry==0x41b97f){control.pop_loop();result=memory.read(args[0]+vm_offset::pc);}
                    else if(entry==0x41c74e){const auto before=memory.read(args[0]+vm_offset::pc);control.select_branch(args[1]!=0);result=args[1]?before:memory.read(args[0]+vm_offset::pc);}
                    else throw std::runtime_error("unknown helper entry");
                }
            }catch(const Fault&){faulted=true;}
            const auto actual=memory.bytes(0x4a5000,expected.size());
            if(faulted!=bool(wanted_fault)||(!faulted&&result!=wanted_return)||actual!=expected) {
                for(unsigned i=0,n=0;i<actual.size()&&n<5;++i)if(actual[i]!=expected[i]){++n;std::cerr<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';}
                throw std::runtime_error("control helper mismatch case "+std::to_string(index));
            }
        }
        if(cursor!=bytes.size())throw std::runtime_error("unconsumed helper fixture");
        std::cout<<"control_helper_original_cases="<<count<<" passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
