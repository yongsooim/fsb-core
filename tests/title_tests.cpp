#include "runtime_assets.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
using namespace fsb::core;
namespace {void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;const std::filesystem::path assets=argv[1],out=argv[2];std::filesystem::create_directories(out);
    std::vector<std::uint8_t> save;
    {Runtime source(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster font(source.memory);fsb::lab::register_runtime_assets(source,font,assets,16);source.start_field_fixture(22,16);
        source.field_menu.write_file=[&](const std::string&,const auto& bytes){save=bytes;return true;};check(source.field_menu.save(1),"fixture original writer failed");}
    for(unsigned scenario=0;scenario<5;++scenario){
        Runtime game(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(game.memory);fsb::lab::register_runtime_assets(game,fonts,assets,Runtime::full_campaign);
        unsigned writes=0,event_instructions=0;
        game.field_menu.read_file=[&](const std::string& name)->std::optional<FieldMenu::Bytes>{if(scenario&&name=="Save1.dat")return save;return {};};
        game.field_menu.write_file=[&](const std::string&,const auto&){++writes;return false;};
        game.environment.trace=[&](Address,const Instruction&,bool after){if(after)++event_instructions;};
        game.start_intro();unsigned now=0;
        const auto advance=[&](unsigned ms){for(unsigned end=now+ms;now<end;++now)game.advance(now,false);};
        const auto press=[&](unsigned key,unsigned scan){game.post_input(keyboard_message(key,scan,true));advance(48);game.post_input(keyboard_message(key,scan,false));advance(100);};
        advance(2000);check(game.intro_active()&&!game.root()&&!event_instructions,"title must not execute Event0");
        check(game.memory.read(0x771b44)==2,"original title fade must reach interactive state");
        check(game.memory.read(0x5b14ec)==(scenario?0u:1u),"original default selection depends on actual saves");
        if(scenario==0){
            fsb::lab::bmp(game.frame(),out/"title-start.bmp");fsb::lab::guest_snapshot(game.memory,out/"title-state.bin");
            press(13,0x1c);advance(2500);check(!game.intro_active()&&game.current_event()==0&&event_instructions,"START must enter real Event0");
            fsb::lab::bmp(game.frame(),out/"event0.bmp");
        }else if(scenario==1){
            fsb::lab::bmp(game.frame(),out/"title-load.bmp");press(13,0x1c);advance(2000);
            check(game.intro_active()&&game.memory.read(0x804a64)==3,"LOAD must open original core slot8");fsb::lab::bmp(game.frame(),out/"load-slots.bmp");
            press(27,1);advance(2500);check(game.intro_active()&&game.memory.read(0x804a64)==2,"load cancel must return to original title");
        }else if(scenario==2){
            press(38,0xc8);check(game.memory.read(0x5b14ec)==2,"up from LOAD selects EXIT");press(13,0x1c);advance(2000);check(game.quit_requested()&&!event_instructions,"EXIT must quit without Event0");
        }else if(scenario==3){
            press(13,0x1c);advance(2000);press(13,0x1c);advance(800);fsb::lab::bmp(game.frame(),out/"load-confirm.bmp");press(37,0xcb);press(13,0x1c);advance(4000);
            check(!game.intro_active()&&game.memory.read(globals::current_map_id)==22&&game.memory.read(globals::game_mode)==3,"load selection must restore the saved field");
            check(!event_instructions&&!writes,"LOAD must not execute Event0 or write files");fsb::lab::bmp(game.frame(),out/"loaded.bmp");
        }else{
            game.post_input(pointer_button_message(0,true,500,390));advance(48);game.post_input(pointer_button_message(0,false,-20,390));advance(100);
            check(game.intro_active()&&game.memory.read(0x771b44)==2,"release outside the canvas must cancel title activation");
            game.post_input(pointer_button_message(0,true,500,390));advance(48);game.post_input(pointer_button_message(0,false,500,390));advance(2500);
            check(!game.intro_active()&&game.current_event()==0&&event_instructions,"logical pointer START must use the original title transition");
        }
    }
    {   // 44d4cc writes title flow8 on the party-defeated precheck. Drive that
        // same handoff through the SGO splash and back into the title menu.
        Runtime game(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(game.memory);fsb::lab::register_runtime_assets(game,fonts,assets,Runtime::full_campaign);
        unsigned writes=0;
        game.field_menu.read_file=[&](const std::string& name)->std::optional<FieldMenu::Bytes>{if(name=="Save1.dat")return save;return {};};
        game.field_menu.write_file=[&](const std::string&,const auto&){++writes;return false;};
        game.start_field_fixture(22,Runtime::full_campaign);unsigned now=0;
        const auto advance=[&](unsigned ms){for(unsigned end=now+ms;now<end;++now)game.advance(now,false);};
        const auto press=[&](unsigned key,unsigned scan){game.post_input(keyboard_message(key,scan,true));advance(48);game.post_input(keyboard_message(key,scan,false));advance(100);};
        advance(500);game.memory.write(0x804a64,8);advance(4000);
        check(game.memory.read(0x804a70)==2&&game.game_over(),"defeat must reach the original SGO splash");
        check(!game.intro_active(),"the splash itself is not the title screen");fsb::lab::bmp(game.frame(),out/"game-over.bmp");
        press(13,0x1c);advance(3000);
        check(game.intro_active()&&game.memory.read(0x804a64)==9,"a key on the splash must enter the post-defeat title");
        check(game.memory.read(0x771b44)==2,"post-defeat title must reach its interactive state");
        check(game.memory.read(globals::current_map_id)==0xffffffffu,"460ba3 drops the defeated map before the menu");
        fsb::lab::bmp(game.frame(),out/"game-over-title.bmp");
        while(game.memory.read(0x5b14ec))press(38,0xc8);
        press(13,0x1c);advance(2000);check(game.memory.read(0x804a64)==10,"post-defeat LOAD must open original core slot8");
        press(13,0x1c);advance(800);press(37,0xcb);press(13,0x1c);advance(4000);
        check(!game.intro_active()&&game.memory.read(0x804a64)==7,"post-defeat load must return to the in-game dispatcher");
        check(game.memory.read(globals::current_map_id)==22&&game.memory.read(globals::game_mode)==3&&!writes,"post-defeat load must restore the saved field without writing files");
        fsb::lab::bmp(game.frame(),out/"game-over-loaded.bmp");
    }
    std::cout<<"core title: original art/fades/default selection, START, LOAD/cancel/restore, EXIT, game-over return passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
