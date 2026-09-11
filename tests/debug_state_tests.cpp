#include "runtime_assets.hpp"
#include "fsb_core/debug_state.hpp"
#include "fsb_core/symbols.hpp"
#include "../tools/debug_stat_fixture.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::uint64_t hash(const Runtime& runtime){std::uint64_t value=1469598103934665603ull;for(const auto& r:runtime.memory.snapshot_regions())for(auto b:r.bytes)value=(value^b)*1099511628211ull;for(auto b:runtime.frame().pixels)value=(value^b)*1099511628211ull;return value;}
}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;const std::filesystem::path assets=argv[1],out=argv[2];std::filesystem::create_directories(out);
    Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,16);runtime.start_field_fixture(22,16);
    for(unsigned now=0;now<1000;++now)runtime.advance(now,false);
    const auto cp949=fsb::lab::read(assets/"fonts/cp949.bin");runtime.post_input(keyboard_message(27,1,true));
    runtime.environment.diagnostics.emplace_back(0x123,"quote \" backslash \\ newline\ncontrol\t");
    runtime.environment.diagnostics.emplace_back(0x124,std::string("raw ")+char(0xff));
    const auto before=hash(runtime);const auto pending=runtime.pending_input_count(),audio=runtime.audio.events().size();
    const auto state=inspect_runtime(runtime,cp949),again=inspect_runtime(runtime,cp949);const auto json=debug_json(state);
    check(before==hash(runtime),"inspecting changed guest memory or frame pixels");
    check(runtime.pending_input_count()==pending&&runtime.audio.events().size()==audio,"inspecting consumed input or changed sound events");
    check(debug_json(again)==json,"same state did not produce identical snapshot");
    check(json.find("다섯손가락마을")!=std::string::npos&&json.find("미로")!=std::string::npos,"original CP949 map/party names were not decoded");
    check(json.find("HP 25/25")!=std::string::npos,"party vitality was not inspected");
    check(json.find("\\\"")!=std::string::npos&&json.find("\\u000a")!=std::string::npos,"JSON diagnostic escaping failed");
    check(json.find("\\u00ff")!=std::string::npos,"non-UTF8 diagnostic bytes made invalid JSON");
    std::ofstream(out/"field.json")<<json;
    for(unsigned now=1000;now<2200;++now)runtime.advance(now,false);
    const auto menu=inspect_runtime(runtime,cp949);check(debug_json(menu).find("필드 메뉴 (11)")!=std::string::npos,"menu mode not observed through actual input");std::ofstream(out/"menu.json")<<debug_json(menu);
    Runtime event(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster event_fonts(event.memory);fsb::lab::register_runtime_assets(event,event_fonts,assets,0);event.start_event0();
    for(unsigned now=0;now<1000;++now)event.advance(now,false);
    const auto event_before=hash(event);const auto event_json=debug_json(inspect_runtime(event,cp949));check(event_before==hash(event),"event inspection mutated the simulation");check(event_json.find("스크립트")!=std::string::npos,"live VM scripts not listed");std::ofstream(out/"event.json")<<event_json;
    fsb::lab::DebugStatFixture grant;auto& m=runtime.memory;m.write(globals::game_mode,4);
    const auto unmodified=hash(runtime);check(grant.apply(runtime).empty()&&hash(runtime)==unmodified,"disabled stat fixture changed the game");
    grant.enabled=true;m.write(globals::game_mode,3);const auto field_hash=hash(runtime);
    check(grant.apply(runtime).empty()&&hash(runtime)==field_hash,"stat fixture ran before battle entry");
    m.write(globals::game_mode,4);const auto other=m.bytes(BattleRules::party_record(1),188),inventory=m.bytes(0x806e30,360*4),events=m.bytes(0x768688,168*4);
    const auto changes=grant.apply(runtime);check(changes.size()==1&&changes[0].character==3,"stat fixture did not limit its grant to active party members");
    const auto record=BattleRules::party_record(3);check(m.read(record+0x18)==250&&m.read(record+0x1c)==250,"stat fixture did not increase/refill living character HP");
    check(m.bytes(BattleRules::party_record(1),188)==other&&m.bytes(0x806e30,360*4)==inventory&&m.bytes(0x768688,168*4)==events,"stat fixture changed another character, inventory or event progress");
    const auto granted=hash(runtime);check(grant.apply(runtime).empty()&&hash(runtime)==granted,"stat fixture compounded its grant on the next tick");
    std::cout<<"debug snapshot and explicit one-time battle stat fixture passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
