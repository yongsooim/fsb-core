#include "fsb_core/battle.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
void Battle::tick_map_transition(){auto& m=runtime_.memory;recovered.invoke(0x46018b,{m.read(0x5d22a0),m.read(0x804a68),3});}
void Battle::tick_worldmap(){
    auto& m=runtime_.memory;if(!worldmap_result_)worldmap_result_=m.allocate_zeroed(8);
    if(!recovered.invoke(0x435740,{worldmap_result_,worldmap_result_+4}))return;
    const auto map=m.read(worldmap_result_),pcpos=m.read(worldmap_result_+4);m.release_allocation(worldmap_result_);worldmap_result_=0;
    if(recovered.invoke_byte(0x412ff8,{0xffffffffu,map})==1){m.write(0x803a4c,0);m.write(globals::game_mode,3);}
    else{m.write(globals::current_map_id,0xffffffffu);m.write(0x5d22a0,map);m.write(0x803a28,pcpos);m.write(globals::game_mode,3);m.write(0x803a4c,3);}
}
bool Battle::field_service(Address entry,RecoveredBattle& code){
    auto& m=runtime_.memory;const auto arg=[&](unsigned i){return code.argument(i);};
    switch(entry){
    case 0x411d12:code.result(0);return true; // Original33C0C3: xor eax,eax; ret.
    case 0x401830:{const auto width=arg(1);if(width!=1&&width!=2&&width!=4)throw Fault(entry,"invalid original typed store width");code.write(arg(0),arg(2),width);code.result(0,12);return true;}
    case 0x41a00b:code.result(runtime_.arena.clone_event(arg(0),arg(1)),8);return true;
    case 0x42feb9:code.result(lookup_actor(m,arg(0)),4);return true;
    case 0x4302f8:code.result(runtime_.actors.spawn_sequence(arg(0)),4);return true;
    case 0x4307e3:runtime_.actors.set_party_mask(arg(0),arg(1)!=0);code.result(std::popcount(arg(0)&0x3ffu),8);return true;
    case 0x430958:runtime_.actors.cleanup_event(runtime_.dialogue);code.result(0);return true;
    case 0x4309c7:runtime_.actors.release_extra_party(runtime_.dialogue);code.result(0);return true;
    case 0x430a08:runtime_.actors.rebuild_extra_party();code.result(0);return true;
    case 0x430d46:code.result(runtime_.actors.start_turn(arg(0),arg(1),arg(2)),12);return true;
    case 0x430ee7:code.result(runtime_.camera.spawn_followup(arg(0),arg(1),arg(2),arg(3)),16);return true;
    case 0x431407:code.result(runtime_.actors.tween_raw(arg(0),arg(1),arg(2),arg(3),m.read(globals::shift_key_state)!=0),16);return true;
    case 0x4545b7:runtime_.camera.focus_actor(arg(0));code.result(0,4);return true;
    case 0x45595d:code.result(runtime_.actors.step_attribute(arg(0),arg(1)),8);return true;
    case 0x4600d8:runtime_.map.switch_map(arg(0),arg(1)&255);code.result(0,8);return true;
    case 0x42ff9b:if(arg(1)>1)throw Fault(entry,"actor visibility mode must be zero or one");runtime_.actors.visible(arg(0),arg(1)!=0);code.result(0,8);return true;
    case 0x43113b:code.result(runtime_.actors.tween_tiles(arg(0),arg(1),arg(2),arg(3),arg(4),arg(5),arg(6)==0,m.read(globals::shift_key_state)!=0),28);return true;
    case 0x430d1c:code.result(runtime_.actors.attach_child(arg(0),arg(1)),8);return true;
    case 0x430e02:code.result(runtime_.actors.follow_path(arg(0),signed32(arg(1)),signed32(arg(2)),arg(3),arg(4),arg(5)!=0,arg(6)!=0,arg(7)!=0),32);return true;
    case 0x457b1b:{const auto p=arg(0);code.result(runtime_.map.register_overlay({signed32(code.read(p)),signed32(code.read(p+4)),signed32(code.read(p+8)),signed32(code.read(p+12))},arg(1),arg(2)),12);return true;}
    case 0x430059:case 0x4300c7:set_actor_tile_state(m,arg(0),arg(1));code.result(0,8);return true;
    case 0x496bee:code.result(code.r[0]);return true; // Original entry is one byte: C3 (ret).
    case 0x406a8b:{const auto at=arg(0);runtime_.viewport.set_rect({signed32(code.read(at)),signed32(code.read(at+4)),signed32(code.read(at+8)),signed32(code.read(at+12))},arg(1)!=0,arg(2)!=0);code.result(0,12);return true;}
    case 0x404bb0:runtime_.palette.upload(arg(0),arg(1),arg(2));code.result(0,12);return true;
    case 0x404b0c:code.result(runtime_.palette.gradient(arg(0),arg(1),arg(2),arg(3),arg(4)),20);return true;
    case 0x404c1c:runtime_.palette.upload(arg(0),arg(1),arg(2),true);code.result(0,12);return true;
    case 0x457458:runtime_.map.load_registered(arg(0));code.result(1,4);return true;
    case 0x457e1c:runtime_.actors.spawn_collected();code.result(0);return true;
    case 0x457191:runtime_.map.rebuild_animation_lookup();code.result(0);return true;
    case 0x497065:runtime_.map.apply_patch(arg(0),arg(1)!=0);code.result(0,8);return true;
    case 0x43208d:{
        runtime_.graphics.clear_direct_text();const auto clamp=[](int value,int low,int high){return value<=low?low:value>=high?high:value;};
        const int width=clamp(signed32(m.read(0x5ab8f8)),640,signed32(m.read(globals::framebuffer_width))),height=clamp(signed32(m.read(0x5ab8fc)),480,signed32(m.read(globals::framebuffer_height)));
        m.write(0x5ab8f8,unsigned(width));m.write(0x5ab8fc,unsigned(height));if(arg(0))runtime_.viewport.center(width,height);else runtime_.viewport.tween_center(30,width,height);m.write(0x769440,0);code.result(0,4);return true;
    }
    case 0x4320f6:runtime_.viewport.event_view(arg(0)?3:2);code.result(0,4);return true;
    case 0x432148:runtime_.viewport.event_view(4);runtime_.graphics.show_direct_text(arg(0));code.result(0,4);return true;
    default:if(runtime_.map.setup_field_overlays(entry)){code.result(0);return true;}return false;
    }
}
} // namespace fsb::core
