#include "fsb_core/combat/handler_lifecycle.hpp"
#include "fsb_core/combat/flow.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
#include <map>
using namespace fsb::core;
namespace {
constexpr Address ctrl=0x6df000,trace=0x6de000,config=0x6deb00,actor=0x8073d8;
const std::map<Address,std::pair<unsigned,unsigned>> abi{{0x45d89c,{1,4}},{0x401ad8,{1,0}},
    {0x464494,{1,4}},{0x447841,{2,8}},{0x4646be,{2,8}},{0x45d91d,{1,4}},{0x45d84b,{2,8}}};
std::uint32_t service(Memory& m,Address entry,const std::vector<std::uint32_t>& args) {
    const auto row=trace+4+m.read(trace)*48;
    std::vector<std::uint32_t> words{entry,unsigned(args.size())};words.insert(words.end(),args.begin(),args.end());
    while(words.size()<4)words.push_back(0);
    for(auto at:{0x5d2698u,0x5d269cu,0x805680u,ctrl+0x14c,ctrl+0x78,actor+4})words.push_back(m.read(at));
    for(unsigned i=0;i<words.size();++i)m.write(row+4*i,words[i]);m.write(trace,m.read(trace)+1);
    const auto mutate=m.read(config+4);
    if(entry==0x447841 && mutate&1){m.write(ctrl+0x14c,77);m.write(0x7760d0,0x12345678);}
    if(entry==0x4646be && mutate&2)m.write(ctrl+0x14c,0xfffffffc);
    if(entry==0x45d89c && mutate&4){m.write(0x5d2698,42);m.write(0x805680,0xbadcafe);m.write(0x5d269c,7);}
    if(entry==0x401ad8 && mutate&8){m.write(0x5d2698,77);m.write(0x805680,ctrl);m.write(0x5d269c,2);}
    if(entry==0x464494 && mutate&16){m.write(0x8064f4,0xabcd);m.write(0x5d2698,88);}
    if(entry==0x45d91d && mutate&32)m.write(0x805680,ctrl);
    return entry==0x45d89c?m.read(config):0;
}
combat::HandlerLifecycleServices hooks(Memory& m){
    combat::HandlerLifecycleServices s;
    s.spawn=[&](Address cb){return service(m,0x45d89c,{cb});};
    s.report_failure=[&](Address msg){service(m,0x401ad8,{msg});};
    s.show_banner=[&](unsigned id){service(m,0x464494,{id});};
    s.start_script=[&](Address obj,Address pc){service(m,0x447841,{obj,pc});};
    s.show_number=[&](Address obj,std::int32_t amount){service(m,0x4646be,{obj,std::uint32_t(amount)});};
    s.release=[&](Address obj){service(m,0x45d91d,{obj});};return s;
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3 && argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid handler fixture");
    std::size_t at=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(at++))<<(8*i);return v;};
    const auto count=word();
    for(unsigned index=0;index<count;++index){
        Memory m=initial;auto entry=word(),mask=word(),n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto address=word(),width=word(),value=word();m.write(address,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto address=word();expected.at(address-0x4a5000)=bytes.at(at++);}
        std::uint32_t returned=0;
        if(direct){auto s=hooks(m);combat::HandlerLifecycle flow(m,s);
            switch(entry){
            case 0x44e085:{combat::Flow restored(m);restored.restore_field_view=[&](unsigned first,unsigned end){service(m,0x45d84b,{first,end});};restored.restore_after_handler();break;}
            case 0x461854:returned=flow.start_player(args[0],args[1]);break;
            case 0x461903:returned=flow.start_enemy(args[0]);break;
            case 0x461a4f:returned=flow.queue_status_delta();break;
            case 0x461a91:returned=flow.poll_completion();break;
            case 0x46196c:flow.tick_status_delta(args[0]);break;
            default:throw std::runtime_error("unknown handler entry");}
        }else{
            RecoveredBattle code(m);code.service=[&](Address entry,RecoveredBattle& call){
                if(entry==0x45c526){Actors(m).tick_default_visual(call.argument(0));call.result(0,4);return true;}
                if(entry==0x8594c0){const auto text=call.format_text(call.argument(1),call.r[4]+12);for(unsigned i=0;i<=text.size();++i)call.write(call.argument(0)+i,i<text.size()?std::uint8_t(text[i]):0,1);call.result(unsigned(text.size()));return true;}
                if(entry==0x435373){call.result(0,4);return true;}
                const auto found=abi.find(entry);if(found==abi.end())return false;
                std::vector<std::uint32_t> args;for(unsigned i=0;i<found->second.first;++i)args.push_back(call.argument(i));
                call.result(service(m,entry,args),found->second.second);return true;};
            returned=code.invoke(entry,args);
        }
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((returned&mask)!=expected_return || actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" return="<<returned<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<'\n';break;}return 1;
        }
    }
    if(at!=bytes.size())throw std::runtime_error("trailing handler fixture bytes");
    Memory m=initial;auto s=hooks(m);s.spawn=[](Address){return 0u;};
    s.report_failure=[](Address at){throw Fault(at,"injected report failure");};
    m.write(combat::handler_flow::completion_latch,77);m.write(combat::handler_flow::phase_gate,combat::handler_flow::pre_action_gate);
    bool threw=false;try{combat::HandlerLifecycle(m,s).queue_status_delta();}catch(const Fault&){threw=true;}
    if(!threw||m.read(combat::handler_flow::active_handler)!=0||m.read(combat::handler_flow::completion_latch)!=77)throw std::runtime_error("spawn failure mutation order changed");
    std::cout<<"handler_cases="<<count<<" direct="<<direct<<" original data, returns and call trace matched\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
