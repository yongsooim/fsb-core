#include "fsb_core/vm.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3&&argc!=4)return 2;const bool native_entry=argc==4&&std::string(argv[3])=="--native-entry";if(argc==4&&!native_entry)return 2;const auto initial=Memory::from_pe32(fsb::lab::read(argv[1]));const auto bytes=fsb::lab::read(argv[2]);std::size_t cursor=8;
        const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(cursor++))<<(i*8);return value;};
        const auto magic=std::string(bytes.begin(),bytes.begin()+std::min<std::size_t>(8,bytes.size()));
        const bool error_cases=magic==std::string("FSBFVE1\0",8);
        if(!error_cases&&magic!=std::string("FSBFVM1\0",8))throw std::runtime_error("invalid VM reference header");
        const auto cases=word();unsigned failed=0;
        for(unsigned index=0;index<cases;++index){
            Memory memory=initial;const auto entry=word(),object=word();const bool expect_fault=error_cases&&word();const auto writes=word();
            for(unsigned i=0;i<writes;++i){const auto at=word(),width=word(),value=word();memory.write(at,value,width);}
            auto expected=memory.bytes(0x4a5000,3882100);const auto changes=word();
            for(unsigned i=0;i<changes;++i){const auto at=word();expected.at(at-0x4a5000)=bytes.at(cursor++);}
            HsmQueue messages;VmEnvironment environment;RecoveredBattle recovered(memory);environment.recovered=&recovered;Vm vm(memory,messages,environment,object);
            bool faulted=false;
            try{if(native_entry)recovered.invoke(entry,{object});else{const auto yield=vm.step();memory.write(0x768a8c,unsigned(yield));}}
            catch(const Fault&){faulted=true;}
            if(faulted!=expect_fault){std::cerr<<"case="<<index<<" fault="<<faulted<<" expected="<<expect_fault<<'\n';return 1;}
            const auto actual=memory.bytes(0x4a5000,expected.size());unsigned differences=0;
            for(unsigned i=0;i<expected.size();++i)if(expected[i]!=actual[i]&&differences++<5)std::cerr<<"case="<<index<<" entry=0x"<<std::hex<<entry<<" at=0x"<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<std::dec<<'\n';
            if(differences&&++failed>=5)break;
        }
        std::cout<<"field_vm_cases="<<cases<<" failures="<<failed<<'\n';return failed?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
