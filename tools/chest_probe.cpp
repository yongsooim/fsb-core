#include "runtime_assets.hpp"
#include "field_input_fixture.hpp"
#include "fsb_core/debug_state.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){
    if(argc!=4&&argc!=5)return 2;const std::filesystem::path assets=argv[1],out=argv[2];const auto index=unsigned(std::stoul(argv[3]));const bool walk=argc==5&&std::string(argv[4])=="--walk";std::filesystem::create_directories(out);
    Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,16);auto& m=runtime.memory;const auto def=globals::sparkle_definitions+index*10,map=m.read(def,2);
    unsigned now=0;std::ofstream inputs(out/"inputs.tsv");inputs<<"ms kind key flags shift control\n";
    const auto post=[&](InputMessage msg){runtime.post_input(msg);inputs<<now<<' '<<msg.kind<<' '<<msg.key<<' '<<msg.flags<<' '<<msg.shift<<' '<<msg.control<<'\n';};
    const auto advance=[&](unsigned ticks){for(unsigned i=0;i<ticks;++i)runtime.advance(now++,false);};
    const auto press=[&](unsigned key){const auto scan=key==13?0x1c:key==37?0xcb:key==38?0xc8:key==40?0xd0:0xcd;post(keyboard_message(key,scan,true));advance(48);post(keyboard_message(key,scan,false));advance(200);};
    const auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    try{
        runtime.start_field_fixture(map,16);runtime.map.update_occupancy();
        const int x=int(m.read(def+2,1)),y=int(m.read(def+3,1)),z=int(m.read(def+4,1));const auto actor=Actors::slot(0),item=m.read(def+6,2),quantity=m.read(def+8,1),variant=m.read(def+9,1),owned=0x806e30+item*4,bits=globals::collected_sparkle_bits+(index/32)*4,mask=1u<<(index%32),before=m.read(owned);
        constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};constexpr unsigned keys[]={38,40,37,39};unsigned facing=4;int stand_x=0,stand_y=0;
        const auto width=m.read(globals::grid_row_stride),height=m.read(globals::grid_height);
        for(unsigned d=0;d<4;++d){const int sx=x-dx[d],sy=y-dy[d];if(sx<0||sy<1||sx>=int(width)||sy>=int(height))continue;const auto cell=unsigned(z)*4096+unsigned(sy)*width+unsigned(sx),attr=m.read(globals::tile_attributes+cell*4),occupancy=m.read(globals::tile_occupancy+cell*4);
            if(!(attr&(0x200|(0x20u<<d)))&&(!(occupancy&0x10000)||(occupancy&65535)==0)){facing=d;stand_x=sx;stand_y=sy;break;}}
        check(facing<4,"no clear adjacent tile for chest fixture");
        std::ofstream(out/"fixture.json")<<"{\"definition\":"<<index<<",\"map\":"<<map<<",\"chest\":["<<x<<','<<y<<','<<z<<"],\"stand\":["<<stand_x<<','<<stand_y<<','<<z<<"],\"walk_from_map_entry\":"<<(walk?"true":"false")<<",\"save_restore_test_mutation\":"<<(walk?"false":"true")<<"}\n";
        const auto place=[&]{set_actor_tile_position(m,actor,stand_x,stand_y,z);m.write(actor+actor_offset::facing,facing);m.write(actor+actor_offset::motion_state,0);m.write(actor+actor_offset::motion_frame,0);};
        if(walk){
            fsb::lab::FieldWalkFixture route;route.map=map;route.x=stand_x;route.y=stand_y;
            while(!route.settled()||m.read(actor+actor_offset::motion_state)){check(now<120000,"chest walk timed out");if(const auto message=route.next(runtime,now))post(*message);advance(1);}press(keys[facing]);advance(300);
            check(m.read(globals::current_map_id)==map&&int(m.read(actor+actor_offset::tile_x))==stand_x&&int(m.read(actor+actor_offset::tile_y))==stand_y,"walk did not reach the chest neighbor");
        }else{place();advance(1000);}
        const auto marker=[&]{for(unsigned i=0;i<m.read(globals::sparkle_count);++i)if(m.read(globals::sparkle_active_definitions+i*8)==index)return m.read(globals::sparkle_active_objects+i*8);throw std::runtime_error("chest marker missing after map entry");};
        check(!(m.read(bits)&mask)&&m.read(marker()+actor_offset::sprite_frame)==variant*2,"fixture chest was not initially closed");
        fsb::lab::bmp(runtime.frame(),out/"before.bmp");fsb::lab::guest_snapshot(m,out/"before-state.bin");
        press(13);advance(1500);fsb::lab::bmp(runtime.frame(),out/"opened.bmp");
        check(m.read(owned)==before+quantity&&(m.read(bits)&mask)&&m.read(marker()+actor_offset::sprite_frame)==variant*2+1,"opening did not grant exactly the original item count and open the chest");
        for(unsigned i=0;i<5&&m.read(globals::live_dialogue_count);++i){advance(1200);press(13);}advance(1000);
        check(!m.read(globals::live_dialogue_count)&&!m.read(0x802ca0)&&!m.read(0x802c9c,1),"pickup dialogue did not release field interaction");
        press(13);advance(800);check(m.read(owned)==before+quantity&&!m.read(globals::live_dialogue_count),"reopening awarded duplicate loot or reopened the dialog");
        if(!walk){
            runtime.map.enter_field(map,0);place();advance(500);check(m.read(marker()+actor_offset::sprite_frame)==variant*2+1,"map reentry closed a collected chest");
            press(13);advance(500);check(m.read(owned)==before+quantity,"map reentry duplicated the chest reward");
            check(runtime.field_menu.save(1),"cannot save collected chest");
            // Test only: invalidate both states so the real load UI must restore
            // the saved inventory and collected bit, not just redraw the map.
            m.write(owned,before);m.write(bits,m.read(bits)&~mask);
            runtime.field_menu.open(8);advance(1800);press(13);advance(1000);press(37);press(13);advance(4000);
            check(m.read(globals::game_mode)==3&&m.read(globals::current_map_id)==map&&m.read(owned)==before+quantity&&(m.read(bits)&mask)&&m.read(marker()+actor_offset::sprite_frame)==variant*2+1,"load did not restore chest state, reward and opened sprite");
        }
        fsb::lab::bmp(runtime.frame(),out/"last.bmp");std::ofstream(out/"state.json")<<debug_json(inspect_runtime(runtime,fsb::lab::read(assets/"fonts/cp949.bin")));
        std::ofstream report(out/"result.json");report<<"{\"definition\":"<<index<<",\"map\":"<<map<<",\"item\":"<<item<<",\"quantity\":"<<quantity<<",\"inventory\":"<<m.read(owned)<<",\"opened_frame\":"<<m.read(marker()+actor_offset::sprite_frame)<<",\"collected\":true,\"dialogue_closed\":true,\"duplicate_prevented\":true,\"map_and_save_restore\":"<<(walk?"false":"true")<<",\"walk_from_map_entry\":"<<(walk?"true":"false")<<",\"logical_ms\":"<<now<<"}\n";
        std::cout<<"chest="<<index<<" map="<<map<<" item="<<item<<" inventory="<<m.read(owned)<<" open/close/repeat"<<(walk?" walked":" reenter/save/load")<<" passed; ms="<<now<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"Stopped at "<<now<<" ms: "<<e.what()<<'\n';fsb::lab::guest_snapshot(m,out/"fault-state.bin");std::ofstream(out/"fault.json")<<debug_json(inspect_runtime(runtime,fsb::lab::read(assets/"fonts/cp949.bin")));
        std::cerr<<"mode="<<m.read(globals::game_mode)<<" event="<<signed32(m.read(globals::current_event_id))<<" block="<<m.read(0x802ca0)<<'\n';return 1;}
}
