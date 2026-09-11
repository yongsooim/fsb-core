#include "fsb_core/combat/anchor_effects.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actor_core/motion_callbacks.hpp"
#include "fsb_core/actor_fields.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
constexpr Address actor=0x8073d8,trace=0x6dc000,config=0x6deb00;
constexpr Address owner=actor+0x1ac,object=0x810a50;
std::uint32_t service(Memory& m,Address entry,const std::vector<std::uint32_t>& args){
    const auto ordinal=m.read(trace);std::vector<std::uint32_t> values{entry,unsigned(args.size())};
    values.insert(values.end(),args.begin(),args.end());while(values.size()<4)values.push_back(0);
    for(auto a:{actor+0x14c,actor+0x16c,actor+0x198,actor+0x160,actor+0x10,actor+0x18c,actor+0x3c,object+0x160,object+0x14c,object+4})values.push_back(m.read(a));
    for(unsigned i=0;i<values.size();++i)m.write(trace+4+ordinal*64+i*4,values[i]);m.write(trace,ordinal+1);
    unsigned result=0;
    if(entry==0x45d89c){result=object+m.read(config+8)*0x1ac;m.write(config+8,m.read(config+8)+1);}
    if(entry==0x498090){result=m.read(config+4);m.write(config+4,result+1);}
    if(m.read(config)){
        if(entry==0x447841){m.write(actor+0x14c,5);m.write(actor+0x16c,100);m.write(args[0]+0x160,owner+0x1ac);}
        if(entry==0x45d208){m.write(actor+0x160,owner+0x1ac);m.write(actor+0x1a0,m.read(actor+0x1a0)+3);}
        if(entry==0x498090){m.write(actor+0x10,80<<16);m.write(actor+0x18c,47<<16);m.write(owner+8,m.read(owner+8)+100);}
        if(entry==0x45d91d)m.write(actor+0x40,object+3*0x1ac);
        if(entry==0x4626ab)m.write(actor+0x198,77);
    }
    return result;
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3&&argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid anchor effect fixture");
    std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(8*i);return v;};
    const auto total=word();
    for(unsigned index=0;index<total;++index){
        Memory m=initial;const auto entry=word(),mask=word();auto n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto a=word(),width=word(),value=word();m.write(a,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto a=word();expected.at(a-0x4a5000)=bytes.at(cursor++);}
        std::uint32_t returned=0;
        if(direct){combat::AnchorEffectServices s;
            s.spawn=[&](Address cb){return service(m,0x45d89c,{cb});};
            s.release=[&](Address obj){service(m,0x45d91d,{obj});};
            s.start_script=[&](Address obj,Address script){service(m,0x447841,{obj,script});};
            s.oscillate=[&](Address obj){service(m,0x45d208,{obj});};
            s.spark_cluster=[&](Address obj){service(m,0x465e0b,{obj});};
            s.spawn_rising=[&](Address obj){service(m,0x4626ab,{obj});};
            s.random=[&]{return service(m,0x498090,{});};
            combat::AnchorEffects effects(m,s);
            switch(entry){
            case 0x45d208:actor_core::tick_oscillation(actor_core::memory_words(m),args[0],
                [&](std::uint32_t angle){return fixed_sin(m,angle);},
                [&](std::uint32_t angle){return fixed_cos(m,angle);});break;
            case 0x4624ad:effects.tick_exploding_mark(args[0]);break;
            case 0x46256a:effects.emit_floating_marks(args[0]);break;
            case 0x4626ab:effects.spawn_rising(args[0]);break;
            case 0x46274d:effects.emit_rising(args[0]);break;
            case 0x4629cb:effects.tick_orbit(args[0]);break;
            case 0x462a49:effects.orbit_ring(args[0],false);break;
            case 0x462b50:effects.orbit_ring(args[0],true);break;
            default:throw std::runtime_error("unknown anchor effect entry");
            }
        }else{RecoveredBattle code(m);returned=code.invoke(entry,args);}
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((returned&mask)!=expected_return||actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" result="<<returned<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';break;}return 1;}
    }
    if(direct){
        for(const auto height:{0, -65536}){
            Memory m=initial;m.write(actor+0x14c,10);m.write(actor+0x160,owner);
            m.write(actor+0x10,std::uint32_t(height));m.write(actor+0x18c,height?0xfffe0000:0);
            combat::AnchorEffectServices s;s.oscillate=[](Address){};
            s.random=[&]{return height?0x80000000u:0u;};
            bool faulted=false;
            try{combat::AnchorEffects(m,s).tick_exploding_mark(actor);}
            catch(const Fault& e){faulted=std::string(e.what()).find("0x462515")!=std::string::npos;}
            if(!faulted)throw std::runtime_error("missing original division fault");
        }
    }
    if(cursor!=bytes.size())throw std::runtime_error("trailing anchor effect bytes");
    std::cout<<"anchor_effect_cases="<<total<<" direct="<<direct<<" matched original data, callbacks and context mutations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
