#include "fsb_core/event_activation.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
#include <map>
using namespace fsb::core;
namespace {
constexpr Address trace=0x6de000,config=0x6deb00,object=0x6df000;
const std::map<Address,std::pair<unsigned,unsigned>> services{{0x41a00b,{2,8}},{0x457e56,{0,0}},
    {0x4300c7,{2,8}},{0x42feb9,{1,4}},{0x457ec9,{1,4}},{0x430059,{2,8}},
    {0x4026c1,{1,4}},{0x4139b6,{3,12}},{0x401a02,{2,0}},{0x412017,{2,8}},{0x44e085,{0,0}}};
std::uint32_t service(Memory& m,Address entry,const std::vector<std::uint32_t>& args) {
    const auto count=m.read(trace),row=trace+4+count*48;
    std::vector<std::uint32_t> words{entry,unsigned(args.size())};
    words.insert(words.end(),args.begin(),args.end());while(words.size()<5)words.push_back(0);
    for(auto at:{0x57fd1cu,0x57fd28u,object+0xf8,object+0xfc,object+0xe4})words.push_back(m.read(at));
    for(unsigned i=0;i<words.size();++i)m.write(row+i*4,words[i]);
    m.write(trace,count+1);
    const auto mutation=m.read(config+20);
    if(entry==0x412017 && mutation&1){m.write(0x57fd28,0x1234);m.write(0x57fd1c,0x4321);}
    if(entry==0x44e085 && mutation&2)m.write(0x57fd28,0xbeef);
    if(entry==0x42feb9 && mutation&4)m.write(object+0xf8,0x9876);
    if(entry==0x41a00b && mutation&8)m.write(0x57fd1c,0x222);
    switch(entry){
    case 0x41a00b:case 0x4139b6:return m.read(config);
    case 0x457e56:return m.read(config+4);
    case 0x42feb9:case 0x457ec9:return m.read(config+8);
    case 0x4026c1:return m.read(config+12);
    case 0x412017:return m.read(config+16);
    default:return 0;
    }
}
EventActivationServices native_services(Memory& m) {
    EventActivationServices s;
    s.clone_definition=[&](Address a,unsigned b){return service(m,0x41a00b,{a,b});};
    s.player_actor=[&]{return service(m,0x457e56,{});};
    s.set_actor_state=[&](Handle a,unsigned b){service(m,0x4300c7,{a,b});};
    s.actor_object=[&](Handle a){return service(m,0x42feb9,{a});};
    s.player_actor_object=[&](Handle a){return service(m,0x457ec9,{a});};
    s.set_object_state=[&](Address a,unsigned b){service(m,0x430059,{a,b});};
    s.runtime_object=[&](Handle a){return service(m,0x4026c1,{a});};
    s.inline_dialog=[&](Handle a,Address b,std::int32_t c){return service(m,0x4139b6,{a,b,std::uint32_t(c)});};
    s.missing_definition=[&](unsigned a){service(m,0x401a02,{0x57fdb8,a});};
    s.request_event=[&](unsigned a,std::uint32_t b){return service(m,0x412017,{a,b});};
    s.alternate_visuals=[&]{service(m,0x44e085,{});};return s;
}
}
int main(int argc,char** argv){
    try{
        if(argc!=3 && argc!=4)return 2;const bool direct=argc==4;
        const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto fixture=fsb::lab::read(argv[2]);
        if(fixture.size()<12 || std::string(fixture.begin(),fixture.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid activation fixture");
        std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(fixture.at(cursor++))<<(8*i);return v;};
        const auto cases=word();
        for(unsigned index=0;index<cases;++index){
            Memory m=initial;const auto entry=word(),mask=word(),argc=word();std::vector<std::uint32_t> args;
            for(unsigned i=0;i<argc;++i)args.push_back(word());
            const auto writes=word();for(unsigned i=0;i<writes;++i){const auto at=word(),width=word(),value=word();m.write(at,value,width);}
            auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word(),changes=word();
            for(unsigned i=0;i<changes;++i){const auto at=word();expected.at(at-0x4a5000)=fixture.at(cursor++);}
            RecoveredBattle code(m);code.service=[&](Address call,RecoveredBattle& abi){
                // Same product service as Battle::service; the original oracle
                // executes this visual finalizer rather than substituting it.
                if(call==0x45c526){Actors(m).tick_default_visual(abi.argument(0));abi.result(0,4);return true;}
                const auto found=services.find(call);if(found==services.end())return false;
                std::vector<std::uint32_t> args;for(unsigned i=0;i<found->second.first;++i)args.push_back(abi.argument(i));
                abi.result(service(m,call,args),found->second.second);return true;};
            std::uint32_t returned=0;
            if(!direct)returned=code.invoke(entry,args);
            else{auto s=native_services(m);EventActivation events(m,s);
                if(entry==0x4120c7)returned=events.spawn_object(args[0],args[1],args[2]);
                else if(entry==0x412147)returned=events.spawn_player_dialog(args[0]);
                else if(entry==0x4138ee)returned=events.resume_pending();
                else throw std::runtime_error("unknown activation entry");}
            const auto actual=m.bytes(0x4a5000,expected.size());
            if((returned&mask)!=expected_return || actual!=expected){
                std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" actual_return="<<returned<<" expected_return="<<expected_return<<'\n';
                for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<'\n';break;}
                return 1;
            }
        }
        if(cursor!=fixture.size())throw std::runtime_error("trailing activation fixture bytes");
        // A throwing downstream callback must not make the final consume write happen.
        Memory m=initial;m.scene_state().resume_event=7;auto s=native_services(m);
        s.request_event=[](unsigned,std::uint32_t)->Handle{throw Fault(0x412017,"injected activation failure");};
        bool threw=false;try{EventActivation(m,s).resume_pending();}catch(const Fault&){threw=true;}
        if(!threw||m.scene_state().resume_event!=7)throw std::runtime_error("resume slot consumed across failed callback");
        RecoveredBattle failing(m);failing.service=[](Address entry,RecoveredBattle&)->bool{
            if(entry==0x412017)throw Fault(entry,"injected activation failure");return false;};
        threw=false;try{failing.invoke(0x4138ee);}catch(const Fault&){threw=true;}
        if(!threw||m.scene_state().resume_event!=7)throw std::runtime_error("ABI resume slot consumed across failed callback");
        std::cout<<"activation_cases="<<cases<<" direct="<<direct<<" matched original data, returns and service order\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
