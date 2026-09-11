#include "fsb_core/combat/entrance.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/combat/markers.hpp"
#include "fsb_core/combat/status_visuals.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/camera.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
constexpr Address actor=0x8073d8,trace=0x6dc000,config=0x6deb00;
constexpr Address object=0x810a50;
std::uint32_t service(Memory& m,Address entry,const std::vector<std::uint32_t>& args){
    const auto ordinal=m.read(trace);std::vector<std::uint32_t> values{entry,unsigned(args.size())};
    values.insert(values.end(),args.begin(),args.end());while(values.size()<8)values.push_back(0);
    for(auto a:{0x77ec04u,0x77ecd8u,0x803a20u,0x776484u,0x77ec60u,0x607a10u,0x806b68u,0x7873b8u})values.push_back(m.read(a));
    for(unsigned i=0;i<values.size();++i)m.write(trace+4+ordinal*64+i*4,values[i]);m.write(trace,ordinal+1);
    const auto result=entry==0x45d89c?object:entry==0x4644d3?41:entry==0x498090?m.read(config+4):0;
    if(m.read(config)){
        if(entry==0x45d774){m.write(0x775630,19);m.write(0x775634,20);m.write(0x77563c,3);m.write(0x7755dc,21);m.write(0x7755e0,22);m.write(0x803a20,2);}
        if(entry==0x45d91d||entry==0x45d89c)m.write(0x77ec60,object+0x1ac);
        if(entry==0x45d889){m.write(0x806b64,2);m.write(0x609cb8,777);}
        if(entry==0x44c1fc)m.write(0x776484,2);
        if(entry==0x462370){m.write(0x607a10,m.read(0x607a10)^0x3ff00);m.write(0x806b68,m.read(0x806b68)^0x3ff00);m.write(0x803a20,2);m.write(0x776484,2);m.write(0x5d2258,2);m.write(0x806b60,61);}
        if(entry==0x45451c||entry==0x4544cf)m.write(0x77a4e0,99);
    }
    return result;
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3&&argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid entrance fixture");
    std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(8*i);return v;};
    const auto total=word();
    for(unsigned index=0;index<total;++index){
        Memory m=initial;const auto entry=word(),mask=word();auto n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto a=word(),width=word(),value=word();m.write(a,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto a=word();expected.at(a-0x4a5000)=bytes.at(cursor++);}
        std::uint32_t returned=0;
        if(direct){
            const auto place=[&](Address a,std::uint32_t x,std::uint32_t y,std::uint32_t grid){service(m,0x45d774,{a,x,y,grid});};
            combat::EntranceServices e;e.place_actor=place;
            e.select_music=[&]{return service(m,0x4644d3,{});};
            e.transition_music=[&](unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f){service(m,0x4337f4,{a,b,c,d,e,f});};
            e.clear_timer=[&](Address at){service(m,0x45d889,{at});};e.random=[&]{return service(m,0x498090,{});};
            e.activate_enemy=[&](unsigned i,bool refresh){service(m,0x44c1fc,{i,unsigned(refresh)});};
            e.move_focus=[&](unsigned from,unsigned to,unsigned ticks,bool enabled){service(m,0x4544cf,{from,to,ticks,unsigned(enabled)});};
            e.focus=[&](unsigned to,unsigned ticks){service(m,0x45451c,{to,ticks});};e.mark_entrance=[&]{service(m,0x44ab05,{});};
            combat::Entrance entrance(m,e);
            combat::MarkerServices ms;ms.place_actor=place;ms.spawn=[&](Address cb){return service(m,0x45d89c,{cb});};ms.release=[&](Address obj){service(m,0x45d91d,{obj});};
            combat::Markers markers(m,ms);
            const combat::StatusVisuals::Detach detach=[&](Address actor,combat::attached_effects::Kind kind){service(m,0x462370,{actor,unsigned(kind)});};
            switch(entry){
            case 0x44c1fc:returned=entrance.activate_enemy(args[0],(args[1]&255)!=0);break;
            case 0x44c3b6:entrance.tick((args[0]&255)!=0,(args[1]&255)!=0);break;
            case 0x44c337:returned=markers.spawn(args[0],args[1],args[2]);break;
            case 0x44c2f4:markers.release_slot(args[0]);break;
            case 0x44c313:markers.tick(args[0]);break;
            case 0x44c954:combat::StatusVisuals(m,detach).remove_expired();break;
            default:throw std::runtime_error("unknown entrance entry");
            }
        }else{RecoveredBattle code(m);code.service=[&](Address entry,RecoveredBattle& call){
            if(entry==0x45d774){set_actor_tile_position(m,call.argument(0),signed32(call.argument(1)),signed32(call.argument(2)),signed32(call.argument(3)));call.result(0,16);return true;}
            if(entry==0x4544cf){Camera(m).line_focus(call.argument(0),call.argument(1),call.argument(2));call.result(0,16);return true;}
            if(entry==0x45451c){Camera(m).line_focus(call.argument(0),call.argument(1));call.result(0,8);return true;}
            if(entry==0x4337f4){std::vector<std::uint32_t> args;for(unsigned i=0;i<6;++i)args.push_back(call.argument(i));call.result(service(m,entry,args),24);return true;}
            return false;
        };returned=code.invoke(entry,args);}
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((returned&mask)!=expected_return||actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" result="<<returned<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';break;}return 1;}
    }
    if(cursor!=bytes.size())throw std::runtime_error("trailing entrance bytes");
    std::cout<<"entrance_cases="<<total<<" direct="<<direct<<" matched original data, callbacks and context mutations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
