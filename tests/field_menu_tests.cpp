#include "runtime_assets.hpp"
#include "save_directory.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
#include <limits>
using namespace fsb::core;
namespace {
unsigned checks=0,failed=0;
void check(bool ok,const char* name){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<name<<'\n';}}
}
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;const std::filesystem::path assets=argv[1],output=argv[2];std::filesystem::create_directories(output);
        Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,16);runtime.start_field_fixture(22,16);auto& m=runtime.memory;
        std::map<std::string,std::vector<std::uint8_t>> files;bool fail_write=false;
        runtime.field_menu.read_file=[&](const std::string& name)->std::optional<std::vector<std::uint8_t>>{const auto it=files.find(name);return it==files.end()?std::nullopt:std::optional<std::vector<std::uint8_t>>(it->second);};
        runtime.field_menu.write_file=[&](const std::string& name,const auto& bytes){if(fail_write)return false;files[name]=bytes;return true;};
        runtime.field_menu.remove_file=[&](const std::string& name){return files.erase(name)!=0;};
        check(runtime.field_menu.save(1),"original writer saves a populated field slot");const auto first=files.at("Save1.dat");
        check(first.size()==15232&&runtime.field_menu.valid_save(first),"original fixed prefix and CIMsave/EventData tails validate");
        m.write(0x803a18,777);check(runtime.field_menu.save(1),"same slot can be saved repeatedly");
        check(files.at("Saveold.dat")==first,"previous snapshot is retained in Saveold.dat");const auto second=files.at("Save1.dat");
        auto stale_selection=second;stale_selection[0xc24]=9;
        check(runtime.field_menu.valid_save(stale_selection),"saved visual-selection cache may outlive a larger party");
        auto invalid_selection=stale_selection;invalid_selection[0xc24]=10;check(!runtime.field_menu.valid_save(invalid_selection),"cached selection remains bounded by the original party slot capacity");
        auto invalid_active=second;invalid_active[0xc2c]=10;check(!runtime.field_menu.valid_save(invalid_active),"actual active actor must remain inside the saved party");
        fail_write=true;m.write(0x803a18,888);check(!runtime.field_menu.save(1),"host write failure is propagated instead of reporting success");
        check(files.at("Save1.dat")==second&&!runtime.field_menu.last_error().empty(),"failed overwrite leaves previous slot intact");fail_write=false;
        check(runtime.battle.recovered.invoke(0x46118e,{1})==1&&m.read(0x803a18)==777,"original loader restores saved gold rather than current values");
        for(unsigned mode=0;mode<3;++mode){auto broken=second;if(mode==0)broken.resize(100);else if(mode==1)broken[0x1bfc]=0xff;else broken[0x1c04]^=1;files["Save1.dat"]=broken;
            const auto before=m.bytes(0x4a5000,3882100);check(runtime.battle.recovered.invoke(0x46118e,{1})==0,"invalid save is refused");check(m.bytes(0x4a5000,3882100)==before,"invalid save cannot partially overwrite guest state");}
        files["Save1.dat"]=second;
        const auto settings=runtime.field_menu.settings();m.write(0x772fec,40);m.write(0x77300c,1);const auto changed=runtime.field_menu.settings();
        check(runtime.field_menu.load_settings(settings)&&m.read(0x772fec)==100,"settings restore original option fields");
        check(runtime.field_menu.load_settings(changed)&&m.read(0x772fec)==40&&m.read(0x76e914)==40&&m.read(0x77300c)==1,"loaded settings update audio and display preferences");
        auto bad_settings=changed;bad_settings[8+(12-2)*4]=0;check(!runtime.field_menu.load_settings(bad_settings)&&runtime.field_menu.settings()==changed,"invalid animation divisor is refused without partial settings writes");
        fsb::host::SaveDirectory disk(output/"slots");check(disk.write("Save1.dat",second)&&disk.write("Settings.dat",changed),"host writes original saves and portable preferences");
        fsb::host::SaveDirectory reopened(output/"slots");check(reopened.read("Save1.dat")==std::optional<FieldMenu::Bytes>(second)&&reopened.read("Settings.dat")==std::optional<FieldMenu::Bytes>(changed),"fresh storage instance reads persisted files");
        check(!reopened.read("absent.dat"),"missing file is reported absent");
        {
            Runtime loaded(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster loaded_fonts(loaded.memory);fsb::lab::register_runtime_assets(loaded,loaded_fonts,assets,Runtime::full_campaign);
            loaded.debug_rewards.enable(true); // Existing balances are restored, not earned again.
            unsigned event0_instructions=0,writes=0;
            loaded.environment.trace=[&](Address,const Instruction&,bool after){if(after)++event0_instructions;};
            loaded.field_menu.write_file=[&](const std::string&,const auto&){++writes;return false;};
            auto invalid=second;invalid.resize(100);bool rejected=false;try{loaded.start_saved_game(invalid);}catch(const Fault&){rejected=true;}
            check(rejected&&!loaded.root(),"invalid intro load is rejected before session startup");
            loaded.start_saved_game(stale_selection);
            for(unsigned now=0;now<4000;++now)loaded.advance(now,false);
            check(loaded.memory.read(globals::current_map_id)==22&&loaded.memory.read(0x803a18)==777,"fresh session loads original map and gold without a field fixture");
            check(loaded.memory.read(globals::character_index)==9&&loaded.memory.read(globals::active_party_index)<loaded.memory.read(globals::party_count),"load preserves the stale selection while restoring valid live control");
            check(loaded.memory.read(globals::game_mode)==3&&!loaded.memory.read(0x803a4c),"intro load finishes the original map transition");
            check(!event0_instructions&&loaded.completed_events().empty(),"LOAD executes no Event0 instructions");
            check(!writes,"LOAD does not overwrite any save or backup");
            for(unsigned i=0;i<0x300;++i)check(loaded.memory.read(Actors::slot(i))==i,"fresh load preserves every actor slot identity");
            // 4478ba scans the whole actor pool itself. Two active scripts
            // must each wait one tick, not both advance twice in one frame.
            for(unsigned i:{700u,701u}){
                const auto actor=Actors::slot(i);loaded.memory.write(actor+4,0x20000);
                loaded.memory.write(actor+0x150,0x5f3af8);loaded.memory.write(actor+0x154,10);
            }
            for(unsigned now=4000;now<4100&&loaded.memory.read(Actors::slot(700)+0x154)==10;++now)loaded.advance(now,false);
            check(loaded.memory.read(Actors::slot(700)+0x154)==9&&loaded.memory.read(Actors::slot(701)+0x154)==9,"each active effect script advances once per game frame");
            for(unsigned i:{700u,701u}){loaded.memory.write(Actors::slot(i)+4,0);loaded.memory.write(Actors::slot(i)+0x150,0);}
            loaded.actors.spawn_party(8);loaded.actors.swap_player(8);
            check(loaded.memory.read(globals::active_party_index)==1&&loaded.memory.read(Actors::slot(1)+0x148)==routines::field_actor_movement,"loaded session can transfer field control to another party slot");
            loaded.actors.swap_player(3);
            check(loaded.memory.read(globals::active_party_index)==0&&loaded.memory.read(Actors::slot(0)+0x148)==routines::field_actor_movement&&(loaded.memory.read(Actors::slot(0)+4)&0x80),"returning to the original player restores a live movement callback");
            //45f82d accepts a fresh D press only in457..466. Snapshots are
            //the original two party/position lanes; no host-owned party copy.
            auto& state=loaded.memory;state.write(0x803a3c,0);state.write(0x5d0a40,0xffffffffu);
            state.write(0x803a30+4,2);state.write(0x803a08+4,0);
            state.write(0x803960+40,4);state.write(0x803960+44,9);
            state.write(0x8039d8+16,8);state.write(0x8039dc+16,9);state.write(0x8039e0+16,0);state.write(0x8039e4+16,2);
            state.write(0x8021d8,0);state.write(globals::input_message,0x100);state.write(globals::input_key,'D');
            state.write(globals::input_flags,0x200001);state.write(globals::current_map_id,456);
            loaded.field_menu.field_input();check(state.read(0x803a3c)==0,"D outside paired tower does not switch party");
            state.write(globals::current_map_id,457);state.write(globals::input_flags,0x40200001);
            loaded.field_menu.field_input();check(state.read(0x803a3c)==0,"held D repeat does not switch party");
            state.write(globals::input_flags,0x200001);loaded.field_menu.field_input();
            check(state.read(0x803a3c)==1&&state.read(globals::party_count)==2&&state.read(globals::party_actor_ids)==4&&state.read(globals::party_actor_ids+4)==9,"fresh D restores the other original party lane");
            check(state.read(Actors::slot(0)+0x128)==8&&state.read(Actors::slot(0)+0x12c)==9&&state.read(Actors::slot(0)+0x110)==2,"D restores saved position and direction");
            loaded.field_menu.field_input();
            check(state.read(0x803a3c)==0&&state.read(globals::party_actor_ids)==3&&state.read(globals::party_actor_ids+4)==8,"second fresh D restores the preserved first party");
        }
        for(double value:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),9223372036854775808.0})check(RecoveredBattle::x87_truncate(value)==0x8000000000000000ull,"masked x87 invalid conversions match original FISTP indefinite result");
        check(RecoveredBattle::x87_truncate(-1.9)==0xffffffffffffffffull&&RecoveredBattle::x87_truncate(1.9)==1,"x87 conversion truncates toward zero");
        const auto format=m.allocate_zeroed(40),args=m.allocate_zeroed(12);const std::string pattern="Lv %-3d %03d:%02d";for(unsigned i=0;i<pattern.size();++i)m.write(format+i,std::uint8_t(pattern[i]),1);m.write(args,1);m.write(args+4,2);m.write(args+8,3);
        check(runtime.battle.recovered.format_text(format,args)=="Lv 1   002:03","save preview supports original left alignment and zero-padded time");
        std::cout<<"field_menu_checks="<<checks<<" failures="<<failed<<'\n';return failed?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
