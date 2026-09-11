#include "fsb_core/actors.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/field.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/viewport.hpp"
#include "fsb_core/vm.hpp"
#include "fsb_core/runtime.hpp"
#include "../tools/lab_io.hpp"
#include "../tools/sprite_assets.hpp"
#include "../tools/field_step.hpp"
#include <iostream>

using namespace fsb::core;
namespace {
unsigned checks=0,failures=0;
void check(bool condition,const char* name){++checks;if(!condition){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
}
int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;
        const std::filesystem::path assets=argv[1];auto memory=Memory::from_pe32(fsb::lab::read(assets/"FLYINGSB.EXE"));
        Arena arena(memory);arena.initialize();Actors actors(memory);actors.bootstrap_new_game_actors();
        Map map(memory);MapAssets bundle;const auto names=Map::asset_names(memory,476);
        for(unsigned i=0;i<6;++i)bundle.files[i]=fsb::lab::read(assets/"MAPSET"/names[i]);
        map.register_assets(476,std::move(bundle));map.switch_map(476,2);
        check(memory.read(0x7ab558)==4,"original map476 owns four static pickup markers");
        const unsigned xs[]={16,16,6,4},ys[]={19,20,19,20};
        for(unsigned i=0;i<4;++i){
            const auto object=memory.read(0x7ab2dc+i*8);
            check(object==Actors::slot(90+i)&&memory.read(0x7ab2d8+i*8)==1018+i,"static markers retain original definition index and extra actor slot");
            check(memory.read(object+0x148)==0x4874a7&&memory.read(object+0x160)==476&&memory.read(object+0x14c)==0,"marker initializer captures owner map then enters state0");
            check(memory.read(object+0x128)==xs[i]&&memory.read(object+0x12c)==ys[i]&&memory.read(object+0x138)==2,"marker position and uncollected chest frame match original table");
        }
        map.update_occupancy();const auto stride=memory.read(0x7760c4);
        check(memory.read(0x7abca0+(19*stride+16)*4)==0x1005a,"static marker stamps occupancy with actor90");
        const auto marker=memory.read(0x7ab2dc);set_actor_tile_position(memory,marker,1,1,0);map.update_occupancy();
        check(memory.read(0x7abca0+(19*stride+16)*4)==0x1005a,"static occupancy follows definition, not displaced marker position");
        memory.write(0x5d229c,22);actors.tick_map_marker(marker);
        check(memory.read(marker)==90&&memory.read(marker+4)==0&&memory.read(marker+0x148)==0,"original marker callback releases on owner-map change");
        memory.write(0x5d229c,476);
        Viewport viewport(memory);viewport.configure_framebuffer(640,480);HsmQueue messages;Dialogue dialogue(memory,messages);
        VmEnvironment env;env.viewport=&viewport;env.dialogue=&dialogue;
        const auto handle=arena.clone_event(0x6251be,0),object=*resolve_compact(memory,handle);Vm vm(memory,messages,env,object);
        memory.write(0x772fe0,0xffffffffu);memory.write(0x772fe4,120);memory.write(0x772fe8,32);vm.step();
        check(vm.pc()==0x6251c7&&memory.read(0x76847c)==0&&memory.read(0x768464)==50&&memory.read(0x768478)==16,"actual Event9 E2/1001 clamps and republishes dialogue options");
        memory.write(0x5aff98,0x110);memory.write(0x5aff98+84,0x118);vm.step();vm.step();
        check(memory.read(0x5aff98)==0x116&&memory.read(0x5aff98+84)==0x11e,"Event9 unlocks town and forest slots using84-byte city stride and preserves other bits");
        memory.write(object+0x30,0x6251ec);memory.write(0x803a40,0x20);vm.step();
        check(memory.read(0x803a40)==0x28,"E8/14 publishes saved party panel bit3");
        memory.write(0x768a98,0);memory.write(0x57fd24,0xffffffffu);
        memory.write(0x5ab8f8,900);memory.write(0x5ab8fc,200);viewport.event_view(1);
        check(memory.read(0x5ab8f8)==640&&memory.read(0x5ab8fc)==480&&memory.read(0x769440)==0,"table viewport restores clamped saved dimensions");
        memory.write(0x57fd24,11);viewport.center(500,300,false,false);const auto before=memory.read(0x6e12b0);viewport.event_view(0);
        check(memory.read(0x6e12b0)==before,"pending event prevents premature map viewport restoration");
        MapAssets mountain;const auto mountain_names=Map::asset_names(memory,30);
        for(unsigned i=0;i<6;++i)mountain.files[i]=fsb::lab::read(assets/"MAPSET"/mountain_names[i]);
        map.load_assets(30,mountain);
        check(memory.read(0x7757dc)==2&&memory.read(0x7744b8)==0xffffffffu&&memory.read(0x7744b8+0x220)==0xffffffffu,"original mountain MFO parses two battleset records without inventing event IDs");
        check(memory.read(0x7744c4+0x220)==1&&memory.read(0x7744c8+0x220)==1&&memory.read(0x7744cc+0x220)==26&&memory.read(0x7744d0+0x220)==27,"scripted battle preserves its four explicit area fields");
        check(memory.read(0x7744dc+0x220)==17&&memory.read(0x7744e0+0x220)==12&&memory.read(0x7744dc+0x220+16)==17&&memory.read(0x7744e0+0x220+16)==11,"scripted battle MFO records MIRO and MASK starting tiles");
        check(memory.read(0x77452c+0x220)==189&&memory.read(0x77452c+0x220+20)==135&&memory.read(0x77452c+0x220+40)==132&&memory.read(0x77452c+0x220+60)==135&&memory.read(0x77452c+0x220+80)==189,"scripted battle roster preserves original enemy ordering");
        check(memory.read(0x7746d0,1)==1&&memory.read(0x7746d0+0x220,1)==0&&memory.read(0x7ab5a0+32)==31,"random battle allows escape, scripted battle forbids it, north jump reaches map31");
        const auto reference=fsb::lab::read(assets.parent_path()/"reference/field-motion-x86.bin");std::size_t cursor=8;
        const auto word=[&](){if(cursor+4>reference.size())throw std::runtime_error("truncated motion fixture");std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(reference[cursor++])<<(i*8);return value;};
        const auto count=word();unsigned mismatches=0;
        for(unsigned i=0;i<count;++i){
            const auto kind=word();const auto actor=Actors::slot(0);
            for(unsigned b=0;b<428;++b)memory.write(actor+b,reference.at(cursor++),1);
            const std::vector<std::uint8_t> expected(reference.begin()+cursor,reference.begin()+cursor+428);cursor+=428;
            const auto active=word(),steps=word(),cleanup=word(),sound=word();
            memory.write(0x803a1c,0);memory.write(0x5d2258,3);memory.write(0x607a12+3*0xbc,0x20,1);
            memory.write(0x804aac,7);memory.write(0x77ece0,0xabc);memory.write(0x80465c,3);memory.write(0x6da2d4,3);
            ActorMotionStep result;if(!kind)result=actors.step_motion(actor);else actors.resolve_field_frame(actor);
            const bool equal=memory.bytes(actor,428)==expected&&memory.read(0x804aac)==steps&&memory.read(0x77ece0)==cleanup&&
                (kind||(result.active==bool(active)&&result.sound.value_or(0xffffffffu)==sound));
            if(!equal&&mismatches++<5){
                std::cerr<<"field motion oracle case "<<i<<" kind "<<kind<<" active "<<result.active<<'/'<<active<<" steps "<<memory.read(0x804aac)<<'/'<<steps<<" cleanup "<<memory.read(0x77ece0)<<'/'<<cleanup<<" sound "<<result.sound.value_or(0xffffffffu)<<'/'<<sound<<" differences:";
                for(unsigned b=0;b<428;b+=4){std::uint32_t value=0;for(unsigned j=0;j<4;++j)value|=std::uint32_t(expected[b+j])<<(j*8);if(memory.read(actor+b)!=value)std::cerr<<" +"<<std::hex<<b<<":"<<memory.read(actor+b)<<"/"<<value<<std::dec;}
                std::cerr<<'\n';
            }
        }
        check(cursor==reference.size()&&count==1552,"field motion fixture is complete");
        check(mismatches==0,"1552original-machine motion and field-frame cases match full actor state, counters and sound requests");
        // Actual Event11 post-victory command waits for its staged actor path.
        memory.write(object+0x30,0x626f58);const auto wait_instruction=Instruction::decode(memory,0x626f58);
        const auto parent_slot=0x768ac0+wait_instruction.operand(memory,0).get(memory,object)*36;
        const auto child=arena.clone_event(0x6ca14f,1);memory.write(parent_slot,child);
        check(vm.step()==Yield::Forced&&vm.pc()==0x626f58&&memory.read(parent_slot)==child,"IFC wait preserves its PC and live actor-path handle");
        arena.release(child);
        check(vm.step()==Yield::Continue&&vm.pc()==wait_instruction.next()&&memory.read(parent_slot)==0,"IFC wait clears stale handle and resumes original post-victory script");
        for(bool extra:{false,true}){
            const auto stem=extra?"extra-party":"party-replace";
            const auto input=fsb::lab::read(assets.parent_path()/"reference"/(std::string(stem)+"-input.bin")),expected=fsb::lab::read(assets.parent_path()/"reference"/(std::string(stem)+"-x86.bin"));std::size_t at=8;
            const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(input.at(at++))<<(i*8);return value;};
            Memory replacement;const auto regions=word();for(unsigned i=0;i<regions;++i){const auto base=word(),size=word();replacement.map(base,{input.begin()+at,input.begin()+at+size},true);at+=size;}
            if(extra)Actors(replacement).set_party_mask(0x30023,true);else Actors(replacement).replace_with_party_actor(1);const auto actual=replacement.bytes(0x4a5000,expected.size());unsigned differences=0;
            for(unsigned i=0;i<expected.size();++i)if(expected[i]!=actual[i]&&differences++<10)std::cerr<<"party replacement 0x"<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<std::dec<<'\n';
            check(differences==0,extra?"Event160extra actors: complete original .data matches4307e3with no substituted calls":"Son joins: complete original .data matches430434with no substituted calls");
        }
        for(unsigned profile=0;profile<3;++profile){
            const bool npc=profile==1,horizontal=profile==2;
            auto m=Memory::from_pe32(fsb::lab::read(assets/"FLYINGSB.EXE"));Actors actors(m);Audio audio(m);audio.initialize();Field field(m,actors,audio);
            const auto bytes=fsb::lab::read(assets.parent_path()/"reference"/(horizontal?"horizontal-stairs-x86.bin":npc?"npc-input-x86.bin":"field-input-x86.bin"));std::size_t cursor=8;
            const auto word=[&](){if(cursor+4>bytes.size())throw std::runtime_error("truncated field input fixture");std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes[cursor++])<<(i*8);return value;};
            const auto count=word(),range_count=word(),observed_count=word();std::vector<std::pair<Address,unsigned>> ranges;std::vector<Address> observed;
            for(unsigned i=0;i<range_count;++i){const auto address=word(),size=word();ranges.push_back({address,size});}
            for(unsigned i=0;i<observed_count;++i)observed.push_back(word());
            unsigned mismatches=0;
            for(unsigned i=0;i<count;++i){
                for(auto range:ranges)for(unsigned b=0;b<range.second;++b)m.write(range.first+b,0,1);
                const auto writes=word();for(unsigned j=0;j<writes;++j){const auto at=word(),value=word();m.write(at,value);}
                const auto actor=ranges.front().first;
                unsigned predicted=0;const auto start_x=m.read(actor+0x128),start_y=m.read(actor+0x12c),start_z=m.read(actor+0x1c);
                if(horizontal){
                    Memory planning=m;Actors steps(planning);set_actor_tile_position(planning,actor,int(start_x),int(start_y),signed32(start_z)/65536);
                    const auto direction=m.read(actor+0x110);
                    check(!(steps.step_attribute(actor,direction)&3),"original script query retains its horizontal connector limitation");
                    predicted=fsb::lab::player_step_for_planning(planning,steps,actor,direction);
                }
                if(npc)actors.tick_npc(actor);else field.tick_actor(actor);bool equal=true;
                for(unsigned b=0;b<428;++b)equal&=m.read(actor+b,1)==bytes.at(cursor++);
                for(auto at:observed)equal&=m.read(at)==word();
                for(unsigned b=0;b<0xc00;++b)equal&=m.read(0x800dc8+b,1)==bytes.at(cursor++);
                if(horizontal){
                    const auto expected_x=start_x+((predicted>>8)==2?0xffffffffu:1u),expected_z=start_z+((predicted&4)?0x10000u:0xffff0000u);
                    check((predicted&1)&&m.read(actor+0x128)==expected_x&&m.read(actor+0x12c)==start_y&&m.read(actor+0x1c)==expected_z,"planning edge agrees with actual original player step");
                }
                if(!equal&&mismatches++<8)std::cerr<<(npc?"NPC":"field input")<<" oracle mismatch case "<<i<<'\n';
            }
            check(cursor==bytes.size()&&count==(horizontal?8u:npc?960u:115u),"field/NPC input fixture is complete");
            check(mismatches==0,horizontal?"eight original horizontal stair steps match actor, global and overlay state":npc?"960original NPC wander/RNG cases match full actor and shared random state":"115original field input/collision/turn/hop/stair cases match actor, global and overlay state");
        }
        for(unsigned id:{22u,28u}){
            Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));fsb::lab::register_sprite_assets(runtime.sprites,assets);
            MapAssets files;const auto names=Map::asset_names(runtime.memory,id);for(unsigned i=0;i<6;++i)files.files[i]=fsb::lab::read(assets/"MAPSET"/names[i]);runtime.map.register_assets(id,std::move(files));
            const auto bgm=runtime.memory.read(0x5c4f58+id*68);runtime.audio.register_wave(true,bgm,fsb::lab::read(assets/"audio"/Audio::resource_name(runtime.memory,true,bgm)));
            runtime.start_field_fixture(id,11);for(unsigned now=0;now<=500;++now)runtime.advance(now);
            check(runtime.memory.read(0x80465c)==3,"town/inn entry step never triggers a disabled encounter (AL return ABI)");
            check(runtime.memory.read(0x803a18)==1000,"new-game field inventory starts with original1000G");
        }
        std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
