#include "runtime_assets.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=4)return 2;const std::filesystem::path assets=argv[1],out=argv[2];const std::string mode=argv[3];std::filesystem::create_directories(out);
        Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,16);fonts.set_enhanced(true);runtime.start_field_fixture(22,16);
        unsigned now=0;auto& m=runtime.memory;std::map<std::string,std::vector<std::uint8_t>> files;
        std::ofstream inputs(out/"inputs.tsv");inputs<<"ms kind key flags shift control\n";
        runtime.field_menu.read_file=[&](const std::string& name)->std::optional<std::vector<std::uint8_t>>{const auto i=files.find(name);return i==files.end()?std::nullopt:std::optional<std::vector<std::uint8_t>>(i->second);};
        runtime.field_menu.write_file=[&](const std::string& name,const std::vector<std::uint8_t>& bytes){files[name]=bytes;std::ofstream f(out/name,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());return bool(f);};
        runtime.field_menu.remove_file=[&](const std::string& name){return files.erase(name)!=0;};
        const auto advance=[&](unsigned count){for(unsigned i=0;i<count;++i)runtime.advance(now++,false);};
        const auto post=[&](InputMessage message){inputs<<now<<' '<<message.kind<<' '<<message.key<<' '<<message.flags<<' '<<message.shift<<' '<<message.control<<'\n';runtime.post_input(message);};
        const auto press=[&](unsigned key){unsigned scan=key==27?1:key==13?0x1c:key==38?0xc8:key==40?0xd0:key==37?0xcb:0xcd;post(keyboard_message(key,scan,true));advance(48);post(keyboard_message(key,scan,false));advance(160);};
        const auto active=[&](unsigned slot){const auto p=m.read(0x7735e8+slot*4);return p&&m.read(p+0x20)==20;};
        const auto wait=[&](auto condition){const auto end=now+10000;while(!condition()){if(now>=end)throw std::runtime_error("menu state timeout");advance(1);}};
        const auto shot=[&](const char* name){fsb::lab::bmp(runtime.frame(),out/(std::string(name)+".bmp"));fsb::lab::bmp(present_image(runtime.frame(),1280,960),out/(std::string(name)+"-enhanced.bmp"));std::cout<<name<<" ms="<<now<<" mode="<<m.read(globals::game_mode)<<" slots:";for(unsigned i=0;i<18;++i)if(const auto p=m.read(0x7735e8+i*4))std::cout<<' '<<i<<'='<<m.read(p+0x20);std::cout<<'\n';};
        advance(1000);
        const auto member=m.read(globals::party_actor_ids),record=0x607a08+member*0xbc;
        // Explicit functional-test preparation, not gameplay runtime changes.
        if(mode=="use-item"){m.write(record+0x1c,m.read(record+0x18)-10);m.write(0x806e30+299*4,1);}
        if(mode=="skill")m.write(record+0x1c,m.read(record+0x18)-10);
        if(mode=="codec"){
            fsb::lab::guest_snapshot(m,out/"save-before.bin");
            if(!runtime.field_menu.save(1))throw std::runtime_error("save failed");
            std::cout<<"save bytes="<<files["Save1.dat"].size()<<" valid="<<runtime.field_menu.valid_save(files["Save1.dat"])<<'\n';
            if(!runtime.field_menu.valid_save(files["Save1.dat"]))throw std::runtime_error("save validation failed");
            if(!runtime.battle.recovered.invoke(0x46118e,{1}))throw std::runtime_error("load failed");return 0;
        }
        press(27);wait([&]{return active(0);});shot("root");
        unsigned row=mode=="item"?1:mode=="save"?2:mode=="system"?4:mode=="exit"?5:0;
        for(unsigned tries=0;tries<8&&m.read(m.read(0x7735e8)+0x1a0)!=row;++tries)press(m.read(m.read(0x7735e8)+0x1a0)<row?40:38);
        press(13);advance(1800);shot(mode.c_str());
        if(mode=="save"){
            const auto actor=Actors::slot(m.read(globals::active_party_index));const auto saved_x=m.read(actor+0x128),saved_y=m.read(actor+0x12c),saved_gold=m.read(0x803a18);
            press(13);advance(1000);shot("save-confirm");press(37);press(13);advance(1000);shot("saved");std::cout<<"files="<<files.size()<<'\n';
            if(!files.count("Save1.dat"))throw std::runtime_error("confirmed save did not write slot1");
            // Round-trip test mutation only: loading must restore data, not
            // merely return to an unchanged field and report success.
            m.write(0x803a18,saved_gold+123);set_actor_tile_position(m,actor,signed32(saved_x)+1,signed32(saved_y),signed32(m.read(actor+0x1c))/65536);
            if(!active(0))press(27);wait([&]{return active(0);});press(40);press(13);advance(1000);shot("load");press(13);advance(800);shot("load-confirm");press(37);press(13);advance(4000);shot("loaded");
            if(m.read(globals::game_mode)!=3||m.read(globals::current_map_id)!=22||m.read(0x803a18)!=saved_gold||m.read(actor+0x128)!=saved_x||m.read(actor+0x12c)!=saved_y)throw std::runtime_error("load did not restore map, gold and actor position");return 0;
        }
        if(mode=="system"){
            // Every visible setting is exercised through the original panel.
            // Volume starts at100, so decrease those two rows instead.
            for(unsigned row=0;row<10;++row){
                if(row)press(40);const auto before=runtime.field_menu.settings();press(row==3||row==4?37:39);advance(100);
                if(runtime.field_menu.settings()==before)throw std::runtime_error("system row "+std::to_string(row)+" did not change its option");
            }
            shot("system-changed");
        }
        if(mode=="party"){press(13);advance(1500);shot("party-selected");}
        if(mode=="equipment"){
            const auto equipment=m.read(record+0x74+1*4),quantity=m.read(0x806e30+equipment*4);
            press(40);press(40);press(13);advance(1000);shot("equipment-active");press(40);press(13);advance(1000);shot("equipment-picker");
            const auto select_item=[&](unsigned item){for(unsigned i=0;i<370;++i){const auto object=m.read(0x7735f8),selected=m.read(object+0x1a0);if(m.read(0x773030+selected*4)==item){press(13);advance(1000);return;}press(40);}throw std::runtime_error("equipment item not in picker");};
            select_item(0x80000000u);shot("unequipped");if(m.read(record+0x74+1*4)!=0xffffffffu||m.read(0x806e30+equipment*4)!=quantity+1)throw std::runtime_error("unequip did not return body equipment");
            press(13);advance(800);select_item(equipment);shot("reequipped");if(m.read(record+0x74+1*4)!=equipment||m.read(0x806e30+equipment*4)!=quantity)throw std::runtime_error("equip did not consume body equipment");
        }
        if(mode=="use-item"){
            press(13);advance(900);shot("party-item");press(13);advance(1200);shot("item-used");
            if(m.read(0x806e30+299*4)!=0||m.read(record+0x1c)!=m.read(record+0x18))throw std::runtime_error("field item did not heal and consume");
        }
        if(mode=="skill"){
            const auto mp=m.read(record+0x24);press(40);press(13);advance(1000);shot("skill-list");press(13);advance(1000);shot("skill-target");press(13);advance(1200);shot("skill-used");
            if(m.read(record+0x1c)!=m.read(record+0x18)||m.read(record+0x24)>=mp)throw std::runtime_error("field healing skill did not heal and consume MP");
        }
        if(mode=="exit"){
            press(27);advance(800);if(runtime.quit_requested())throw std::runtime_error("cancel quit exited game");wait([&]{return active(0);});press(13);advance(800);press(37);press(13);wait([&]{return runtime.quit_requested();});
            if(!runtime.quit_requested())throw std::runtime_error("confirmed quit not delivered");shot("quit");return 0;
        }
        press(27);advance(1600);shot("cancel");for(unsigned i=0;i<4&&m.read(globals::game_mode)==11;++i){press(27);advance(1600);}shot("closed");if(m.read(globals::game_mode)!=3)throw std::runtime_error("menu did not return to field");
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
