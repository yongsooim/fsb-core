// Replays reference/parallel/C_MAPS/map-objects-x86.bin, recorded by running the
// original instructions. Entries whose legacy callers still exist go through the
// product dispatch, so the call lands on the reconstruction and not a fixed body.
// The two overlay marker entries call the integrator-owned position router, which
// the fixture replaced with a stub; those run through the reconstruction's own
// API with the same stub, and their ABI bridge is a one-line forward to it.
#include "fsb_core/primitives.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/map_logic/map_objects.hpp"
#include "../../../tools/lab_io.hpp"
#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>

using namespace fsb::core;
namespace {
constexpr Address region=0x4a5000;
constexpr std::size_t region_size=3882100;
constexpr Address sound_cue_service=0x435373,position_router=0x412181;
constexpr Address map_position_event=0x413787;
using ServiceCall=std::pair<Address,std::vector<std::uint32_t>>;
// The original leaves an incidental EAX in the overlay callbacks and no caller
// reads it; only the entries with a used return value are compared.
bool returns_a_used_value(Address entry){
    switch(entry){
    case 0x496fe8:case 0x49700e:case 0x497036:case 0x497065:case 0x49711f:
    case 0x436077:case 0x4360be:case 0x436100:case 0x43611c:return true;
    default:return false;
    }
}
// These three reach an integrator-owned routine the fixture replaced with a
// stub, so they run through the reconstruction's own API with the same stub.
bool needs_a_stubbed_callee(Address entry){return entry==0x486b8c||entry==0x486bfe||entry==0x4870c6;}
struct Reader {
    const std::vector<std::uint8_t>& bytes;std::size_t cursor=0;
    std::uint32_t word(){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(cursor++))<<(8*i);return value;}
    std::uint8_t byte(){return bytes.at(cursor++);}
};
}
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;
        auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));
        const auto pristine=memory.bytes(region,region_size);
        const auto bytes=fsb::lab::read(argv[2]);
        if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!="FSBCMAPO")throw std::runtime_error("invalid map object fixture");
        Reader in{bytes,8};
        std::vector<ServiceCall> observed;
        bool router_answer=false;
        Map map(memory);
        map_logic::MapObjects objects(memory,map,{
            [&](unsigned cue){observed.push_back({sound_cue_service,{cue}});},
            [&]{observed.push_back({position_router,{}});return router_answer;},
            // The three entries driven through this API never reach these two.
            [](Address){throw std::runtime_error("unexpected despawn on the direct path");},
            [](Address,Address){throw std::runtime_error("unexpected tile conversion on the direct path");},
            [&](std::int32_t x,std::int32_t y){observed.push_back({map_position_event,{std::uint32_t(x),std::uint32_t(y)}});}});
        const auto groups=in.word();
        unsigned calls=0,changed=0,services=0,bridged=0;
        for(unsigned group=0;group<groups;++group){
            std::set<Address> touched;
            router_answer=in.word()!=0;
            const auto seeds=in.word();
            for(unsigned i=0;i<seeds;++i){
                const auto address=in.word(),width=in.word(),value=in.word();
                memory.write(address,value,width);
                for(unsigned b=0;b<width;++b)touched.insert(address+b);
            }
            auto expected=memory.bytes(region,region_size);
            const auto count=in.word();
            for(unsigned call=0;call<count;++call){
                const auto entry=in.word(),args=in.word(),eax=in.word();
                std::vector<std::uint32_t> arguments(args);for(auto& value:arguments)value=in.word();
                const auto changes=in.word();
                for(unsigned i=0;i<changes;++i){
                    const auto address=in.word();const auto value=in.byte();
                    expected.at(address-region)=value;touched.insert(address);
                }
                const auto service_count=in.word();
                std::vector<ServiceCall> wanted(service_count);
                for(auto& record:wanted){
                    record.first=in.word();const auto count=in.word();
                    record.second.resize(count);for(auto& value:record.second)value=in.word();
                }
                observed.clear();
                if(needs_a_stubbed_callee(entry)){
                    objects.tick(entry,arguments.at(0));
                }else{
                    RecoveredBattle code(memory);
                    code.service=[&](Address called,RecoveredBattle& caller){
                        if(called!=sound_cue_service)return false; // 0x45d82d/0x45d91d run for real.
                        observed.push_back({called,{caller.argument(0)}});caller.result(0,4);return true;
                    };
                    const auto actual=code.invoke(entry,arguments);
                    if(returns_a_used_value(entry)&&actual!=eax)
                        throw std::runtime_error("original return value differs at entry "+std::to_string(entry));
                    ++bridged;
                }
                // 0x45d82d is not a stub on either side, so drop it from the log.
                auto drop_unstubbed=[](std::vector<ServiceCall> log){
                    log.erase(std::remove_if(log.begin(),log.end(),[](const ServiceCall& record){
                        return record.first!=sound_cue_service&&record.first!=position_router
                            &&record.first!=map_position_event;}),log.end());
                    return log;
                };
                if(drop_unstubbed(observed)!=drop_unstubbed(wanted))
                    throw std::runtime_error("original substituted call sequence differs at entry "+std::to_string(entry)
                        +" group "+std::to_string(group)+" call "+std::to_string(call));
                const auto actual_data=memory.bytes(region,region_size);
                if(actual_data!=expected){
                    std::size_t at=0;while(at<region_size&&actual_data[at]==expected[at])++at;
                    std::ostringstream message;
                    message<<"original data differs after entry 0x"<<std::hex<<entry<<" at 0x"<<(region+at)
                           <<": expected 0x"<<unsigned(expected.at(at))<<" got 0x"<<unsigned(actual_data.at(at))
                           <<" (group "<<std::dec<<group<<" call "<<call<<")";
                    throw std::runtime_error(message.str());
                }
                ++calls;changed+=changes;services+=service_count;
            }
            for(auto address:touched)memory.write(address,pristine.at(address-region),1);
        }
        if(in.cursor!=bytes.size())throw std::runtime_error("unconsumed map object fixture");
        std::cout<<"original_c_maps_calls="<<calls<<" groups="<<groups<<" through_product_dispatch="<<bridged
                 <<" changed_bytes="<<changed<<" recorded_service_calls="<<services
                 <<" full data region compared\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
