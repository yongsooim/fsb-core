#include "runtime_assets.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;const std::filesystem::path assets=argv[1];
        Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);
        fsb::lab::register_runtime_assets(runtime,fonts,assets,Runtime::full_campaign);
        runtime.start_field_fixture(22,Runtime::full_campaign);
        for(unsigned now=0;now<1000;++now)runtime.advance(now,false);
        auto& memory=runtime.memory;const auto counters=memory.bytes(0x768688,168*4);
        //Execute the real ending E2/999 instruction in an isolated tail test.
        const auto handle=runtime.arena.clone_event(0x6beb46,0),object=*resolve_compact(memory,handle);
        memory.write(object+vm_offset::pc,0x6bf680);memory.write(0x4a5044,0x460872);
        Vm(memory,runtime.messages,runtime.environment,object).step();
        const auto check=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
        check(memory.read(object+vm_offset::pc)==0x6bf689&&memory.read(globals::shutdown_drain_requested)==1&&memory.read(0x4a5044)==0,"actual ending instruction did not request shutdown");
        check(!runtime.quit_requested(),"shutdown skipped the original object-drain phase");
        for(unsigned now=1000;now<3000&&!runtime.quit_requested();++now)runtime.advance(now,false);
        check(runtime.quit_requested()&&!runtime.game_over(),"ending close was not delivered to the host");
        check(memory.read(0x6da558)==1&&memory.read(0x6d66a0)==1&&memory.read(globals::shutdown_idle_frames)>0,"original close latches were not advanced");
        check(memory.read(globals::compact_active_count)==0,"shutdown left active compact objects");
        for(unsigned group=0;group<capacity::object_groups;++group)check(runtime.arena.members(group).empty(),"shutdown left an object group linked");
        check(memory.read(globals::current_map_id)==22&&memory.bytes(0x768688,168*4)==counters,"shutdown invented map/event progress");
        check(runtime.completed_events().empty(),"cancelled ending tail was recorded as a normal script completion");
        std::cout<<"original E2/999, compact drain, and host-independent close passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
