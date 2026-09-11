#include "runtime_assets.hpp"
#include "input_trace.hpp"
#include "battle_input_fixture.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=5)return 2;const std::filesystem::path assets=argv[1],out=argv[2];const std::string mode=argv[4];if(mode!="idle"&&mode!="skill"&&mode!="item"&&mode!="attack")throw std::runtime_error("probe mode needs idle, skill, item or attack");std::filesystem::create_directories(out);
        Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,11);fonts.set_enhanced(true);runtime.start_field_fixture(30,11);
        const auto prefix=fsb::lab::read_input_trace(argv[3]);std::size_t cursor=0;auto& m=runtime.memory;fsb::lab::BattleInputFixture combat;
        bool range=false,radial=false,panel=false,target=false;unsigned range_time=0,radial_time=0,release=0,held=0;std::ofstream trace(out/"inputs.tsv");trace<<"ms kind key flags shift control\n";
        const auto post=[&](unsigned now,InputMessage msg){runtime.post_input(msg);trace<<now<<' '<<msg.kind<<' '<<msg.key<<' '<<msg.flags<<' '<<msg.shift<<' '<<msg.control<<'\n';};
        const auto press=[&](unsigned now,unsigned key){held=key;release=now+48;post(now,keyboard_message(key,key==90?0x2c:key==38?0xc8:0xcb,true));};
        const auto capture=[&](unsigned now,const char* label){fsb::lab::bmp(runtime.frame(),out/(std::string(label)+".bmp"));fsb::lab::bmp(present_image(runtime.frame(),1280,960),out/(std::string(label)+"-enhanced.bmp"));std::cout<<label<<" ms="<<now<<" effects="<<m.read(globals::battle_cursor_effect_flags)<<" sub="<<m.read(globals::battle_command_substate)<<'\n';};
        for(unsigned now=0;now<180000;++now){
            while(cursor<prefix.size()&&prefix[cursor].ms<=now)post(now,prefix[cursor++].message);
            if(held&&now>=release){post(now,keyboard_message(held,held==90?0x2c:held==38?0xc8:0xcb,false));held=0;}
            if(range&&mode=="attack"){if(const auto msg=combat.next(runtime,now))post(now,*msg);}
            else if(range&&!radial&&range_time&&now==range_time+500)press(now,90);
            else if(radial&&!panel&&now==radial_time+500)press(now,mode=="item"?37:38);
            const auto step=runtime.advance(now,false);if(!step.rendered)continue;
            const auto effects=m.read(globals::battle_cursor_effect_flags);
            if(!range&&m.read(globals::game_mode)==9&&m.read(globals::battle_command_substate)==4&&effects==3&&!m.read(globals::battle_command_result_phase)){
                unsigned commands=0,tinted=0;for(unsigned i=0;i<m.read(globals::draw_queue_count);++i){const auto c=m.read(globals::draw_queue+i*4);if(m.read(c+draw_offset::type)==4&&m.read(c+draw_offset::checker_fill)>=224)++commands;}
                for(auto pixel:runtime.frame().pixels)if(pixel>=224&&pixel<230)++tinted;
                if(!commands||tinted<1024)throw std::runtime_error("movement range state exists but its checker pixels are missing");
                for(unsigned i=0;i<6;++i){const auto color=runtime.palette.colors()[224+i];const auto expected=m.read(globals::battle_range_palette+i*4);if(color.r!=(expected&255)||color.g!=((expected>>8)&255)||color.b!=((expected>>16)&255))throw std::runtime_error("battle range palette is missing");}
                range=true;range_time=now;capture(now,"move-range");
                if(mode=="idle"){
                    const auto dump=[&](const char* name){const auto bytes=m.bytes(0x4a5000,3882100);std::ofstream f(out/name,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
                    runtime.draw.reset();dump("background-before.bin");runtime.draw.background(runtime.map.background_commands(0,effects),0x804d10);dump("background-after.bin");
                    runtime.draw.reset();for(unsigned i=0;Actors::slot(i)+4<0x85712d;++i)if(m.read(Actors::slot(i)+4)&64)runtime.draw.actor(i);runtime.draw.shadows(1,m.read(0x5cd3f0));dump("foreground-before.bin");runtime.draw.foreground(0,true);dump("foreground-after.bin");
                    return 0;
                }
            }
            if(mode=="attack"&&range){
                unsigned red=0;for(unsigned i=0;i<m.read(globals::draw_queue_count);++i){const auto c=m.read(globals::draw_queue+i*4);if(m.read(c+draw_offset::type)==4&&m.read(c+draw_offset::checker_fill)>=227)++red;}
                if(red){unsigned tinted=0;for(auto pixel:runtime.frame().pixels)if(pixel>=227&&pixel<230)++tinted;if(tinted<16)throw std::runtime_error("attack target pixels are missing");capture(now,"attack-range");target=true;break;}
            }
            const auto selector=m.read(0x773624);if(!radial&&range&&selector&&m.read(selector+0x20)==20){radial=true;radial_time=now;capture(now,"command-menu");}
            const auto child=m.read(mode=="item"?0x7735f8:0x7735fc);if(radial&&child&&m.read(child+0x20)==20&&now>radial_time+900){panel=true;capture(now,mode=="item"?"item-menu":"skill-menu");break;}
        }
        return mode=="attack"?(target?0:3):(panel?0:3);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
