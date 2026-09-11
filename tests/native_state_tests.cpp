#include "fsb_core/primitives.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void same(const Memory& memory,const std::vector<std::uint8_t>& expected){check(memory.bytes(0x4a5000,expected.size())==expected,"serialized original layout differs");}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)return 2;
        auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));const auto initial=memory.bytes(0x4a5000,3882100);
        // Computed from the original PE section bytes plus zero-filled virtual
        // tails, independently of Memory. See native-state-layout.json.
        std::uint64_t initial_hash=14695981039346656037ull;
        for(const auto byte:initial)initial_hash=(initial_hash^byte)*1099511628211ull;
        check(initial_hash==0x89fd9782895257beull,"original initial data image changed");
        check(memory.random_state().crt_seed==1&&memory.random_state().sequence_seed==1,"original initial seeds were not imported");
        const auto usage=memory.storage_usage();
        check(usage.native_bytes==32&&usage.raw_bytes+32==usage.logical_bytes,"native state bytes still have raw backing");
        auto expected=initial;
        const auto put=[&](Address address,unsigned value,unsigned width){for(unsigned i=0;i<width;++i)expected.at(address-0x4a5000+i)=std::uint8_t(value>>(8*i));};
        struct SceneField { Address address; std::uint32_t SceneState::* member; };
        const SceneField scene_fields[]{
            {0x57fd1c,&SceneState::current_event},{0x57fd20,&SceneState::previous_event},
            {0x57fd24,&SceneState::pending_event},{0x57fd28,&SceneState::resume_event},
            {0x5d229c,&SceneState::current_map},{0x80465c,&SceneState::game_mode}};
        for (const auto [address, member] : scene_fields) {
            check(memory.scene_state().*member==memory.read(address),"scene field import mismatch");
            memory.scene_state().*member=0x89abcdefu;put(address,0x89abcdefu,4);same(memory,expected);
            memory.write(address+1,0x1234,2);put(address+1,0x1234,2);
            check(memory.scene_state().*member==0x891234efu,"scene halfword write missed native store");
            // Includes writes across two adjacent event fields and raw/native edges.
            memory.write(address-1,0x76543210u);put(address-1,0x76543210u,4);same(memory,expected);
            if constexpr(std::endian::native==std::endian::little) {
                auto span=memory.span(address,4);span[3]=0x42;put(address+3,0x42,1);
                check((memory.scene_state().*member>>24)==0x42,"scene field span is a copy");
            }
            auto independent=memory;independent.scene_state().*member=7;
            check(memory.scene_state().*member!=7&&independent.read(address)==7,"scene copy aliases original");
        }
        same(memory,expected);
        memory.random_state().crt_seed=0x12345678;put(0x6d1bf0,0x12345678,4);same(memory,expected);
        memory.write(0x6d1bf1,0xabcd,2);put(0x6d1bf1,0xabcd,2);
        check(memory.random_state().crt_seed==0x12abcd78,"legacy halfword did not modify native seed");same(memory,expected);
        // Unaligned writes crossing raw/native boundaries must keep byte order.
        for(const auto address:{0x6d1befu,0x6d1bf2u,0x5aa407u,0x5aa40au}){
            memory.write(address,0xfedcba98);put(address,0xfedcba98,4);same(memory,expected);
            check(memory.read(address)==0xfedcba98,"cross-storage scalar read mismatch");
        }
        if constexpr(std::endian::native==std::endian::little){
            auto borrowed=memory.span(0x6d1bf0,4);borrowed[0]=0x42;put(0x6d1bf0,0x42,1);
            check((memory.random_state().crt_seed&255)==0x42,"native span is a copy");same(memory,expected);
            memory.random_state().crt_seed=0x12345678;put(0x6d1bf0,0x12345678,4);
            check(borrowed[0]==0x78,"borrowed span did not observe native mutation");same(memory,expected);
        }
        bool rejected=false;try{memory.span(0x6d1bef,8);}catch(const Fault&){rejected=true;}
        check(rejected,"noncontiguous range falsely offered a mutable span");
        auto copied=memory;copied.random_state().crt_seed=7;
        check(memory.random_state().crt_seed!=7&&copied.read(0x6d1bf0)==7,"Memory copy shares native storage");
        Memory assigned;assigned=memory;assigned.write(0x5aa408,9);
        check(memory.random_state().sequence_seed!=9&&assigned.random_state().sequence_seed==9,"Memory assignment retains stale field bindings");
        auto moved=std::move(assigned);check(moved.read(0x5aa408)==9,"Memory move lost native state");
        for (const auto [address, member] : scene_fields)
            check(moved.scene_state().*member==memory.scene_state().*member &&
                  moved.read(address)==memory.scene_state().*member,
                  "assignment or move lost a scene field or retained stale bindings");
        // regions() is an explicit owning snapshot. It must not be a live cache.
        const auto snapshot=memory.snapshot_regions();memory.random_state().crt_seed=99;
        Memory restored;for(const auto& region:snapshot)restored.map(region.base,region.bytes,region.writable);
        same(restored,expected);check(restored.storage_usage().native_bytes==32,"snapshot restoration returned to raw seed storage");
        restored.random_state().crt_seed=77;check(memory.random_state().crt_seed==99,"restored state aliases source");
        auto fresh=Memory::from_pe32(fsb::lab::read(argv[1]));same(fresh,initial);
        // Partial mappings also reference the one field; the mapped bytes alone
        // establish their values and still enforce the logical mapping extent.
        Memory partial;partial.map(0x6d1bf1,{0xaa,0xbb},true);
        check(partial.random_state().crt_seed==0x00bbaa00&&partial.storage_usage().raw_bytes==0,"partial field kept a duplicate raw region");
        partial.write(0x6d1bf1,0xcc,1);check(partial.random_state().crt_seed==0x00bbcc00,"partial write missed native field");
        rejected=false;try{partial.read(0x6d1bf0);}catch(const Fault&){rejected=true;}check(rejected,"partial mapping permitted an unmapped access");
        Memory partial_scene;partial_scene.map(0x57fd1f,{0x12,0x34},true);
        check(partial_scene.scene_state().current_event==0x12000000u &&
              partial_scene.scene_state().previous_event==0x34u &&
              partial_scene.storage_usage().raw_bytes==0,"adjacent partial scene fields have wrong backing");
        check(partial_scene.read(0x57fd1f,2)==0x3412,"cross-field partial read lost byte order");
        std::cout<<"native random and scene state: storage removal, aliases, copies, moves and snapshot restore passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
