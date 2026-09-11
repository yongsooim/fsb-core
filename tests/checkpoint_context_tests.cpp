#include "fsb_core/checkpoint_context.hpp"
#include "fsb_core/actor_core/map_transition.hpp"
#include <iostream>
using namespace fsb::core;
int main(){
 try{
    unsigned checks=0;
    for(unsigned map:{166u,167u,424u,442u,248u})for(unsigned origin:{424u,442u,0u,0xffffffffu}){
        Memory m;m.map(0x5d2298,std::vector<std::uint8_t>(12),true);
        m.write(0x5d2298,123);m.write(actor_core::loaded_map,map);
        const bool expected=(map==166||map==167)&&(origin==424||origin==442);
        if(restore_secret_arena_return(m,origin)!=expected||m.read(0x5d2298)!=(expected?origin:123))throw std::runtime_error("return context validation/write mismatch");
        std::vector<std::uint8_t> bytes(15232);for(unsigned i=0;i<4;++i)bytes[0xbf0+i]=std::uint8_t(map>>(i*8));
        if(can_restore_secret_arena_return(bytes,origin)!=expected)throw std::runtime_error("save context validation mismatch");
        ++checks;
    }
    if(can_restore_secret_arena_return({},424))throw std::runtime_error("truncated save accepted");
    std::cout<<checks<<" checkpoint context combinations passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
