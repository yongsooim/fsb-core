#include "fsb_core/battle.hpp"
#include "fsb_core/combat/ai.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/original_audio.hpp"
#include "fsb_core/original_runtime.hpp"

namespace fsb::core {
Battle::Battle(Runtime& runtime):rules(runtime.memory),recovered(runtime.memory),runtime_(runtime){recovered.service=[this](Address entry,RecoveredBattle& code){return service(entry,code);};recovered.after_call=[this](Address entry,RecoveredBattle& code){runtime_.field_menu.after_call(entry,code);};}
void Battle::reset_snapshot(){runtime_.memory.write(0x802ca4,0);}
Address Battle::snapshot_actor(unsigned id){
    auto& m=runtime_.memory;const auto actor=lookup_actor(m,id),count=m.read(0x802ca4);
    if(!actor||count>=20)throw Fault(id,"invalid scripted battle snapshot actor/count");
    m.write(0x802ca4,count+1);m.write(0x802200+count*4,m.read(actor));return actor;
}
void Battle::trigger_scripted(unsigned group){
    auto& m=runtime_.memory;m.write(0x77ec54,1);
    if(m.read(globals::current_event_id)==167){m.write(0x77ec58,0);m.write(0x77ec30,(crt_rand(m)&1)+group*2);}
    else{
        m.write(0x77ec30,1);m.write(0x77ec58,5);
        // Actual4337f4 arguments:30fade-out ticks, BGM39(BATTLE0),30fade-in ticks.
        runtime_.audio.transition_bgm(30,0,39,0,30,100);m.write(0x77ec50,1,1);
    }
}
void Battle::begin_scripted(){
    auto& m=runtime_.memory;const auto index=m.read(globals::active_party_index),actor=Actors::slot(index);
    m.write(0x77a4fc,index);m.write(0x77e594,std::uint32_t(signed32(m.read(actor+0x14))/65536));m.write(0x77e59c,std::uint32_t(signed32(m.read(actor+0x18))/65536));m.write(0x77e584,std::uint32_t(signed32(m.read(actor+0x1c))/65536));m.write(0x77ec54,0);
    if(!recovered.invoke_byte(0x44ab67)){m.write(globals::game_mode,3);return;}
    initialize_ui();m.write(globals::game_mode,9);m.write(globals::phase_interval_ms,25);
}
void Battle::tick_intro(){
    auto& m=runtime_.memory;const unsigned ready=m.read(0x77ec58)>=5||m.read(0x74b474)!=0;
    recovered.invoke(0x44c3b6,{ready,ready});
}
void Battle::tick(){
    auto& m=runtime_.memory;if(m.read(0x804a64)==8)return;if(!m.read(0x77e5a0)){tick_intro();return;}
    if(m.read(0x77e5a0)==1)recovered.invoke(0x44d45a);
    else if(m.read(0x77e5a0)==2)recovered.invoke(0x450d7a);
    else if(m.read(0x77e5a0)==3)recovered.invoke(0x44ab62);
    else throw Fault(0x77e5a0,"unknown battle scene phase");
}
void Battle::tick_player(Address actor){recovered.invoke(0x458ed7,{actor});}
void Battle::tick_callback(Address callback,Address actor){recovered.invoke(callback,{actor});}
bool Battle::service(Address entry,RecoveredBattle& code){
    if(runtime_.debug_rewards.service(entry,code))return true;
    if(original_audio::dispatch(entry,code,runtime_.audio))return true;
    if(original_runtime::dispatch(entry,code,runtime_))return true;
    if(runtime_.field_menu.service(entry,code))return true;
    auto& m=runtime_.memory;const auto arg=[&](unsigned i){return code.argument(i);};
    switch(entry){
    case 0x452168:case 0x452255:
        // One implementation for both the product path and the ABI bridge.
        code.result(combat::Ai(m).rasterize_regions(entry==0x452168?std::optional<unsigned>(arg(0)):std::nullopt),
                    entry==0x452168?4:0);
        return true;
    case 0x45733a:{
        // Original writes a rectangular block of map cells. Each cell carries two
        // layers of eight source bytes: the raw attribute dword, then a packed
        // tile id and orientation whose derived attribute comes from the tile
        // template table. Tiles past the table are cleared instead.
        const int columns=signed32(arg(3)),rows=signed32(arg(4));const auto source=arg(5);
        const auto stride=m.read(globals::grid_row_stride);
        auto cell=arg(1)*stride+arg(0);
        for(int row=0;source&&row<rows;++row){
            auto cursor=source+std::uint32_t(row)*std::uint32_t(std::max(columns,0))*16u;
            for(int column=0;column<columns;++column){
                auto attribute=globals::tile_attributes+cell*4,raw=globals::map_raw_attributes+cell*4;
                for(unsigned layer=0;layer<2;++layer){
                    m.write(raw-0x4000,m.read(cursor));
                    const auto packed=m.read(cursor+4);const auto tile=packed&0xffffu;const auto orientation=signed32(packed)>>16;
                    m.write(raw,tile);
                    if(tile>=0x104)m.write(attribute,0);
                    else m.write(attribute,(m.read(tables::tile_attribute_templates+tile*4)&~0x1fu)
                                          |((std::uint32_t(orientation/4)&7u)<<2)|(std::uint32_t(orientation%4)&3u));
                    cursor+=8;raw+=0x8028;attribute+=0x4000;
                }
                ++cell;
            }
            cell+=stride-std::uint32_t(columns);
        }
        code.result(0,24);return true;}
    case 0x412017:code.result(runtime_.arena.activate_event(arg(0),arg(1)),8);return true;
    case 0x4139b6:code.result(runtime_.dialogue.create(arg(0),arg(1),signed32(arg(2))),12);return true;
    case 0x40c161:code.result(runtime_.dialogue.create_at(arg(0),arg(1),arg(2),arg(3)),16);return true;
    case 0x45451c:runtime_.camera.line_focus(arg(0),arg(1));code.result(0,8);return true;
    case 0x453fe3:runtime_.camera.clamp_target(signed32(arg(0)),signed32(arg(1)),(arg(2)&255)!=0);code.result(0,12);return true;
    case 0x8594c0:case 0x4992d0:{
        const auto out=code.format_text(arg(1),code.r[4]+12);
        for(unsigned i=0;i<=out.size();++i)code.write(arg(0)+i,i<out.size()?std::uint8_t(out[i]):0,1);code.result(unsigned(out.size()));return true;
    }

    case 0x464494:show_banner(arg(0));code.result(0,4);return true;
    case 0x401d66:{const auto read_rect=[&](Address at){return Rect{signed32(code.read(at)),signed32(code.read(at+4)),signed32(code.read(at+8)),signed32(code.read(at+12))};};runtime_.transition.zoom(arg(0),read_rect(arg(1)),arg(2),arg(3)?std::optional<Rect>(read_rect(arg(3))):std::nullopt);code.result(0,16);return true;}
    case 0x401980:if(arg(0))throw Fault(entry,"original conditional battle assertion failed");code.result(0);return true;
    case 0x40bb53:m.write(0x766f74,0xffffffffu);code.result(0);return true;
    case 0x45c55c:{const auto result=runtime_.actors.step_motion(arg(0));if(result.sound)runtime_.audio.play_cue(*result.sound);code.result(result.active,4);return true;}
    case 0x45c526:{const auto result=runtime_.actors.tick_default_visual(arg(0));if(result.sound)runtime_.audio.play_cue(*result.sound);code.result(0,4);return true;}
    case 0x45cc1b:if(m.read(globals::game_mode)==9)runtime_.actors.resolve_battle_frame(arg(0));else runtime_.actors.resolve_field_frame(arg(0));code.result(0,4);return true;
    case 0x4544cf:
        if(!arg(3)){m.write(0x7873d8,m.read(0x7873d8)&~1u);m.write(globals::camera_focus_actor_index,arg(1));m.write(0x7873b8,0);}
        else if(arg(3)==1)runtime_.camera.line_focus(arg(0),arg(1),arg(2));code.result(0,16);return true;
    case 0x45d774:set_actor_tile_position(m,arg(0),signed32(arg(1)),signed32(arg(2)),signed32(arg(3)));code.result(0,16);return true;
    case 0x45da86:m.write(0x85712c,0);m.write(0x857270,0);code.result(0);return true;
    case 0x401ad8:throw Fault(entry,"original battle diagnostic/assertion branch reached");
    default:return ui_service(entry,code)||field_service(entry,code);
    }
}

} // namespace fsb::core
