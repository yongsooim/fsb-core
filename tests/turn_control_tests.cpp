#include "fsb_core/combat/turn_control.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
constexpr Address actor=0x8073d8,trace=0x6dc000,config=0x6deb00;
std::uint32_t service(Memory& m,Address entry,const std::vector<std::uint32_t>& args){
    const auto ordinal=m.read(trace),at=trace+4+ordinal*64;
    std::vector<std::uint32_t> values{entry,unsigned(args.size())};
    values.insert(values.end(),args.begin(),args.end());while(values.size()<10)values.push_back(0);
    for(auto a:{actor+4,0x7757e0u,0x77ecdcu,0x805508u})values.push_back(m.read(a));
    for(unsigned i=0;i<values.size();++i)m.write(at+i*4,values[i]);m.write(trace,ordinal+1);
    std::uint32_t result=0;
    if(entry==0x44ce7b){const auto probe=m.read(config+4);m.write(config+4,probe+1);result=m.read(config+16+probe*4);}
    if(entry==0x44956a)result=m.read(config+32);
    if(entry==0x44d09c)result=0x12;
    if(entry==0x44d102)result=0x34;
    if(m.read(config)){
        if(entry==0x44956a)m.write(actor+60*0x1ac+0x118,1);
        if(entry==0x44d09c){m.write(0x7757e0,1);m.write(actor+0x110,3);}
        if(entry==0x4490c7){m.write(0x7757e0,1);m.write(actor+4,0xaabbcc11);m.write(0x607a68,999);}
        if(entry==0x43934c||entry==0x439701||entry==0x451ec6)m.write(0x77ecdc,7);
    }
    return result;
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3&&argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid turn control fixture");
    std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(8*i);return v;};
    const auto total=word();
    for(unsigned index=0;index<total;++index){
        Memory m=initial;const auto entry=word(),mask=word();auto n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto a=word(),width=word(),value=word();m.write(a,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto a=word();expected.at(a-0x4a5000)=bytes.at(cursor++);}
        std::uint32_t returned=0;
        if(direct){combat::TurnControlServices s;
            s.occupant=[&](auto x,auto y){return service(m,0x44ce7b,{x,y});};
            s.passable=[&](auto x,auto y,combat::turn::Edge edge,unsigned mode){return std::uint8_t(service(m,0x44956a,{x,y,unsigned(edge),mode}));};
            s.default_action=[&](unsigned slot,bool secondary){return service(m,0x44d09c,{slot,unsigned(secondary)});};
            s.default_handler=[&](unsigned slot,bool secondary){return service(m,0x44d102,{slot,unsigned(secondary)});};
            s.prepare_handler=[&](unsigned handler,unsigned facing){service(m,0x45149b,{handler,facing});};
            s.load_menu_assets=[&](unsigned set){service(m,0x43934c,{set});};
            s.open_menu=[&](unsigned slot,unsigned mode){service(m,0x439701,{slot,mode});};
            s.flood_costs=[&](const combat::turn::MovementArea& a){service(m,0x4490c7,{a.grid,a.bounds[0],a.bounds[1],a.bounds[2],a.bounds[3],a.x,a.y,a.marker});};
            s.mark_reachable=[&](std::int32_t budget){service(m,0x451ec6,{std::uint32_t(budget)});};
            s.enemy_turn=[&](Address a){service(m,0x453b8d,{a});};
            s.release=[&](Address a){service(m,0x45d91d,{a});};
            combat::TurnControl control(m,s);
            switch(entry){
            case 0x44ceb2:returned=control.has_adjacent_enemy(args[0],args[1]);break;
            case 0x44cfe2:control.begin_action(args[0]);break;
            case 0x44d17b:control.activate_context();break;
            case 0x461c66:control.tick_defeated_enemy(args[0]);break;
            default:throw std::runtime_error("unknown turn control entry");
            }
        }else{RecoveredBattle code(m);returned=code.invoke(entry,args);}
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((returned&mask)!=expected_return||actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" result="<<returned<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';break;}return 1;}
    }
    if(cursor!=bytes.size())throw std::runtime_error("trailing turn control bytes");
    std::cout<<"turn_control_cases="<<total<<" direct="<<direct<<" matched original data, callbacks and context mutations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
