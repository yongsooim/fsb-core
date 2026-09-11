#include "fsb_core/combat/followup.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actors.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
#include <map>
using namespace fsb::core;
namespace {
constexpr Address actor=0x8073d8,trace=0x6dc000,config=0x6deb00;
const std::map<Address,std::pair<unsigned,unsigned>> calls{{0x45db53,{1,4}},{0x45db6c,{1,4}},
    {0x4622c2,{2,8}},{0x4623dd,{1,4}},{0x447841,{2,8}}};
std::uint32_t service(Memory& m,Address entry,const std::vector<std::uint32_t>& args){
    const auto ordinal=m.read(trace),at=trace+4+ordinal*48;std::vector<std::uint32_t> values{entry,unsigned(args.size())};
    values.insert(values.end(),args.begin(),args.end());while(values.size()<4)values.push_back(0);
    for(auto a:{0x8064f0u,0x8059f0u,0x8059f8u,0x77a50cu,0x773f8cu,0x805608u})values.push_back(m.read(a));
    for(unsigned i=0;i<values.size();++i)m.write(at+i*4,values[i]);m.write(trace,ordinal+1);
    const auto mutate=m.read(config);
    if(mutate&&entry==0x45db53){m.write(0x8059f0,actor+3*0x1ac);if(!ordinal)m.write(0x77a50c,2);m.write(0x773f8c,m.read(0x773f8c)^0x300);}
    if(mutate&&entry==0x45db6c){m.write(0x8059f0,actor+3*0x1ac);m.write(0x8059f8,actor+0x1ac);}
    if(mutate&&entry==0x4622c2){m.write(0x773f8c,m.read(0x773f8c)^0x8400);m.write(0x8059f8,actor+0x1ac);}
    if(mutate&&entry==0x447841){m.write(0x8064f0,2);m.write(0x773f8c,m.read(0x773f8c)|0x08080000);}
    if(mutate&&entry==0x4623dd){m.write(0x8064f0,1);m.write(0x8059f0,actor+3*0x1ac);}
    return 1;
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3&&argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid follow-up fixture");
    std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(8*i);return v;};
    const auto total=word();
    for(unsigned index=0;index<total;++index){
        Memory m=initial;const auto entry=word(),mask=word();auto n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto a=word(),width=word(),value=word();m.write(a,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto a=word();expected.at(a-0x4a5000)=bytes.at(cursor++);}
        std::uint32_t returned=0;
        if(direct){combat::FollowupServices s;
            s.snapshot_motion=[&](Address a){service(m,0x45db53,{a});};s.restore_motion=[&](Address a){service(m,0x45db6c,{a});};
            s.attach_effect=[&](Address a,std::int32_t kind){service(m,0x4622c2,{a,std::uint32_t(kind)});};
            s.clear_effects=[&](Address a){service(m,0x4623dd,{a});};s.start_script=[&](Address a,Address pc){service(m,0x447841,{a,pc});};
            combat::Followup flow(m,s);if(entry==0x461d82)flow.prepare(args[0]);else if(entry==0x461b11)flow.enqueue_wave(args[0]);else throw std::runtime_error("unknown follow-up entry");
        }else{RecoveredBattle code(m);code.service=[&](Address entry,RecoveredBattle& call){
            if(entry==0x45c526){Actors(m).tick_default_visual(call.argument(0));call.result(0,4);return true;}
            if(entry==0x435373||entry==0x4353cb){call.result(0,4);return true;}
            if(entry==0x401ad8){call.result(0);return true;}
            if(entry==0x8594c0){const auto s=call.format_text(call.argument(1),call.r[4]+12);for(unsigned i=0;i<=s.size();++i)call.write(call.argument(0)+i,i<s.size()?std::uint8_t(s[i]):0,1);call.result(unsigned(s.size()));return true;}
            const auto found=calls.find(entry);if(found==calls.end())return false;
            std::vector<std::uint32_t> args;for(unsigned i=0;i<found->second.first;++i)args.push_back(call.argument(i));
            call.result(service(m,entry,args),found->second.second);return true;};returned=code.invoke(entry,args);}
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((returned&mask)!=expected_return||actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" result="<<returned<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';break;}return 1;}
    }
    if(cursor!=bytes.size())throw std::runtime_error("trailing follow-up bytes");
    std::cout<<"followup_cases="<<total<<" direct="<<direct<<" matched original data, callbacks and context mutations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
