#include "fsb_core/combat/attached_effects.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
using namespace combat::attached_effects;
constexpr Address ctrl=0x6df000,other=0x6df200,config=0x6de800,trace=0x6dc000;
std::uint32_t service(Memory& m,Address entry,Address arg){
    const auto ordinal=m.read(trace),row=m.read(config+4),slot=m.read(config+8),start=trace+4+ordinal*32;
    const std::uint32_t values[]{entry,arg,m.read(owners+row*4),m.read(counts+row*4),m.read(objects+slot*4),m.read(kinds+slot*4),m.read(ctrl+object_owner)};
    for(unsigned i=0;i<std::size(values);++i)m.write(start+i*4,values[i]);m.write(trace,ordinal+1);
    const auto mutate=m.read(config);
    if(entry==0x45d89c && mutate&1){m.write(counts+row*4,0xffffffff);m.write(owners+row*4,0x3333);m.write(objects+slot*4,other);}
    if(entry==0x45d91d && mutate&2){m.write(counts+row*4,7);m.write(owners+row*4,0x2222);m.write(objects+slot*4,other);m.write(kinds+slot*4,0x12345678);}
    if(entry==0x45d91d && mutate&4 && !ordinal && slot<319)m.write(objects+(slot+1)*4,other);
    return entry==0x45d89c?ctrl:0;
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3&&argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid attached-effect fixture");
    std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(i*8);return v;};
    const auto total=word();
    for(unsigned index=0;index<total;++index){
        Memory m=initial;const auto entry=word(),mask=word();auto n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto at=word(),width=word(),value=word();m.write(at,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto at=word();expected.at(at-0x4a5000)=bytes.at(cursor++);}
        std::uint32_t result=0;
        if(direct){combat::AttachedEffectServices s;
            s.spawn=[&](Address cb){return service(m,0x45d89c,cb);};s.release=[&](Address obj){service(m,0x45d91d,obj);};s.report=[&](Address msg){service(m,0x401ad8,msg);};
            combat::AttachedEffects effects(m,s);
            switch(entry){
            case 0x4622c2:result=effects.attach(args[0],signed32(args[1]));break;
            case 0x462370:result=effects.detach(args[0],signed32(args[1]));break;
            case 0x4623dd:result=effects.clear_owner(args[0]);break;
            case 0x462452:effects.clear_all();result=1;break;
            default:throw std::runtime_error("unknown attached-effect entry");}
        }else{RecoveredBattle code(m);code.service=[&](Address entry,RecoveredBattle& call){
            if(entry!=0x45d89c&&entry!=0x45d91d&&entry!=0x401ad8)return false;
            call.result(service(m,entry,call.argument(0)),entry==0x401ad8?0:4);return true;};result=code.invoke(entry,args);}
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((result&mask)!=expected_return||actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" result="<<result<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<'\n';break;}return 1;}
    }
    if(cursor!=bytes.size())throw std::runtime_error("trailing attached-effect fixture bytes");
    Memory m=initial;for(unsigned row=0;row<owner_capacity;++row)m.write(owners+row*4,0);m.write(objects,0);m.write(counts,7);
    combat::AttachedEffectServices s;s.spawn=[](Address){return 0u;};s.report=[](Address){};
    bool failed=false;try{combat::AttachedEffects(m,s).attach(0x1234,1);}catch(const Fault& e){failed=e.address==object_owner;}
    if(!failed||m.read(objects)||m.read(owners)||m.read(counts)!=7)throw std::runtime_error("failed spawn was published as an attached object");
    std::cout<<"attached_effect_cases="<<total<<" direct="<<direct<<" matched original data, returns and callback observations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
