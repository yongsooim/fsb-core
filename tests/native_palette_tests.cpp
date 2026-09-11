#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "../tools/lab_io.hpp"
#include <algorithm>
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;
        auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));
        RecoveredBattle code(memory);Palette palette(memory,{});HsmQueue messages;
        VmEnvironment environment;environment.palette=&palette; // Deliberately no legacy executor.
        const auto reference=fsb::lab::read(argv[2]);std::size_t cursor=8;
        if(reference.size()<16||std::string(reference.begin(),reference.begin()+8)!=std::string("FSBPAL1\0",8))throw std::runtime_error("bad palette reference");
        const auto word=[&](){unsigned value=0;for(unsigned i=0;i<4;++i)value|=unsigned(reference.at(cursor++))<<(i*8);return value;};
        const auto count=word(),size=word();
        const auto storage=memory.allocate_zeroed(size),object=memory.allocate_zeroed(0x200),pc=memory.allocate_zeroed(64);
        unsigned direct_callbacks=0;
        for(unsigned index=0;index<count;++index){
            const auto entry=word(),dst=word(),src=word(),first=word(),length=word(),delta=word(),expected=word();
            std::vector<std::uint8_t> initial(size),after(size);
            for(auto& value:initial)value=reference.at(cursor++);
            for(auto& value:after)value=reference.at(cursor++);
            const auto check=[&](unsigned actual,unsigned wanted,const auto& bytes,const char* path){
                if(actual!=wanted||bytes!=after)throw std::runtime_error("palette case "+std::to_string(index)+" "+path+" result="+std::to_string(actual)+" expected="+std::to_string(wanted));
            };
            const auto args_for=[&](Address base){
                std::vector<unsigned> args{base+dst,base+src};
                if(entry==0x404cf0)args.push_back(length);
                else{args.push_back(entry==0x404ed0?first:delta);args.push_back(length);}
                return args;
            };
            const auto wanted=[&](Address base){return entry==0x404ed0&&length?base+expected:expected;};
            for(const Address base:{Address(0x1000400),storage}){
                for(unsigned i=0;i<size;++i)code.write(base+i,initial[i],1);
                const auto result=code.invoke(entry,args_for(base));
                std::vector<std::uint8_t> actual(size);for(unsigned i=0;i<size;++i)actual[i]=code.read(base+i,1);
                check(result,wanted(base),actual,base==storage?"memory ABI":"stack ABI");
            }
            auto local=initial;
            const auto bytes=signed32(length)>0?std::size_t(length)*4:0;
            auto destination=std::span(local).subspan(dst+(entry==0x404ed0?first*4:0),bytes);
            auto source=std::span<const std::uint8_t>(local).subspan(src+(entry==0x404ed0?first*4:0),bytes);
            unsigned result=length;
            if(entry==0x404ed0){Palette::copy_words(destination,source);result=length?dst+first*4+length*4:0;}
            else if(bytes)result=entry==0x404cf0?unsigned(Palette::grayscale(destination,source))*0x101u:Palette::adjust_rgb(destination,source,delta);
            check(result,expected,local,"plain C++ spans");
            if(entry!=0x404e4c){
                std::copy(initial.begin(),initial.end(),memory.span(storage,size).begin());
                const auto args=args_for(storage);const unsigned command_length=9+args.size()*5;
                memory.write(pc,0x1f,1);memory.write(pc+1,0,1);memory.write(pc+2,command_length,2);
                memory.write(pc+4,0,1);memory.write(pc+5,entry);
                for(unsigned i=0;i<args.size();++i){memory.write(pc+9+i*5,0,1);memory.write(pc+10+i*5,args[i]);}
                memory.write(object+vm_offset::pc,pc);
                Vm vm(memory,messages,environment,object);
                if(vm.step()!=Yield::Continue||vm.pc()!=pc+command_length)throw std::runtime_error("direct callback did not advance");
                check(memory.read(object+vm_offset::result),wanted(storage),memory.bytes(storage,size),"direct VM callback");++direct_callbacks;
            }
        }
        if(cursor!=reference.size())throw std::runtime_error("palette fixture extent mismatch");
        std::cout<<"native_palette_original_cases="<<count<<" stack/memory/span paths passed; direct_vm_callbacks="<<direct_callbacks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
