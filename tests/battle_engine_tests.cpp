#include "fsb_core/combat/engine.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/camera.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
constexpr Address actor=0x8073d8,trace=0x6dc000,config=0x6deb00;
std::uint32_t service(Memory& m,Address entry,const std::vector<std::uint32_t>& args){
    const auto ordinal=m.read(trace);std::vector<std::uint32_t> values{entry,unsigned(args.size())};
    values.insert(values.end(),args.begin(),args.end());while(values.size()<6)values.push_back(0);
    for(auto a:{0x775cacu,0x775cb0u,0x7757e0u,0x77ec4cu,0x77a568u,0x77a50cu,0x77e570u,0x774188u,0x804a60u,actor+4})values.push_back(m.read(a));
    for(unsigned i=0;i<values.size();++i)m.write(trace+4+ordinal*64+i*4,values[i]);m.write(trace,ordinal+1);
    unsigned result=0;
    switch(entry){
    case 0x44d233:result=m.read(config+8);break;
    case 0x44cd05:result=m.read(config+12);m.write(args[0],m.read(config+16));break;
    case 0x44cb26:result=m.read(config+20);break;
    case 0x44cb7a:result=m.read(config+24);break;
    case 0x461a91:result=m.read(config+28);break;
    case 0x461ca2:result=m.read(config+32);break;
    case 0x43955f:result=m.read(config+36);break;
    case 0x44d102:result=m.read(config+40);break;
    }
    if(m.read(config)){
        m.write(0x775cac,0x80+ordinal);m.write(0x7757e0,1);m.write(0x787478,1);
        m.write(0x77ec4c,m.read(0x77ec4c)^0x10);m.write(0x77a568,m.read(0x77a568)+1);m.write(actor+0x110,3);
        if(entry==0x44cdf3)m.write(actor+0x104,m.read(actor+0x104)^1);
        if(entry==0x461d82)m.write(0x77ebfc,1);
        if(entry==0x461ca2)m.write(0x774188,m.read(config+44));
    }
    return result;
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3&&argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid battle engine fixture");
    std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(8*i);return v;};
    const auto total=word();
    for(unsigned index=0;index<total;++index){
        Memory m=initial;const auto entry=word(),mask=word();auto n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto a=word(),width=word(),value=word();m.write(a,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto a=word();expected.at(a-0x4a5000)=bytes.at(cursor++);}
        std::uint32_t returned=0;
        if(direct){combat::EngineServices s;
            s.decay_status=[&]{service(m,0x44c6eb,{});};s.advance_gauges=[&]{service(m,0x44cbd2,{});};
            s.prepare_vitality=[&]{service(m,0x44d297,{});};s.commit_vitality=[&]{service(m,0x44d2d8,{});};
            s.snapshot_party=[&](bool active){service(m,0x44d389,{unsigned(active)});};
            s.snapshot_enemy=[&](bool active){service(m,0x44d40a,{unsigned(active)});};
            s.precheck=[&]{return service(m,0x44d233,{});};
            s.select_next=[&]{return (service(m,0x44cd05,{0x7757e0})&255)!=0;};
            s.move_focus=[&](unsigned from,unsigned to,unsigned ticks,bool enabled){service(m,0x4544cf,{from,to,ticks,unsigned(enabled)});};
            s.has_status=[&](unsigned slot,unsigned mask){return (service(m,0x44cb26,{slot,mask})&255)!=0;};
            s.poison_delta=[&](unsigned slot){return service(m,0x44cb7a,{slot});};
            s.apply_delta=[&](unsigned slot,std::uint32_t delta){service(m,0x44d345,{slot,delta});};
            s.queue_status=[&]{service(m,0x461a4f,{});};s.activate_context=[&]{service(m,0x44d17b,{});};
            s.poll_handler=[&]{return service(m,0x461a91,{})!=0;};
            s.finish_followup=[&]{return service(m,0x461ca2,{})!=0;};
            s.prepare_followup=[&](combat::followup::Stage stage){service(m,0x461d82,{unsigned(stage)});};
            s.enqueue_wave=[&](unsigned wave){service(m,0x461b11,{wave});};
            s.start_player=[&](unsigned mode,unsigned action){service(m,0x461854,{mode,action});};
            s.start_enemy=[&](unsigned action){service(m,0x461903,{action});};
            s.invalidate_panel=[&]{service(m,0x40bb53,{});};s.clear_command=[&]{service(m,0x4394ea,{});};
            s.select_ai_followup=[&]{service(m,0x44e7e5,{});};s.apply_status_icons=[&]{service(m,0x44c954,{});};
            s.menu_result=[&]{return service(m,0x43955f,{});};
            s.clear_cursor=[&](std::uint32_t x,std::uint32_t y,unsigned radius,unsigned keep){service(m,0x44cdf3,{x,y,radius,keep});};
            s.default_handler=[&](unsigned slot,bool secondary){return service(m,0x44d102,{slot,unsigned(secondary)});};
            s.prepare_handler=[&](unsigned handler,unsigned facing){service(m,0x45149b,{handler,facing});};
            s.track_action=[&](std::uint32_t x,std::uint32_t y,bool direct){service(m,0x451746,{x,y,unsigned(direct)});};
            s.mark_entrance=[&]{service(m,0x44ab05,{});};s.retire_entrance=[&]{service(m,0x44ab2e,{});};
            if(entry!=0x44d45a)throw std::runtime_error("unknown battle engine entry");
            combat::Engine(m,s).tick();
        }else{RecoveredBattle code(m);code.service=[&](Address service,RecoveredBattle& call){
            if(service==0x4544cf){Camera(m).line_focus(call.argument(0),call.argument(1),call.argument(2));call.result(0,16);return true;}
            if(service==0x40bb53){m.write(0x766f74,0xffffffffu);call.result(0);return true;}
            return false;
        };returned=code.invoke(entry,args);}
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((returned&mask)!=expected_return||actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" result="<<returned<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';break;}return 1;}
    }
    if(cursor!=bytes.size())throw std::runtime_error("trailing battle engine bytes");
    std::cout<<"battle_engine_cases="<<total<<" direct="<<direct<<" matched original data, callbacks and context mutations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
