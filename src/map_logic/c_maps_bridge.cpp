#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/map_logic/event_flags.hpp"
#include "fsb_core/map_logic/map_patch_table.hpp"
#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/map_logic/worldmap.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
// Temporary ABI boundary for map/worldmap/interaction entries whose original
// callers still use the old execution model. Argument reads and the callee stack
// cleanup live here only; no reconstructed routine below uses that model itself.
// The reconstructions reach the routines other sessions still own through the
// same original entry points the legacy callers use.
map_logic::MapObjectServices RecoveredBattle::legacy_map_services(){
    return {[this](unsigned cue){callback(0x435373,{cue});},
            [this]{return callback(0x412181)!=0;},
            [this](Address object){callback(0x45d91d,{object});},
            [this](Address world,Address tile){callback(0x45d82d,{world,tile});},
            [this](std::int32_t x,std::int32_t y){callback(0x413787,{std::uint32_t(x),std::uint32_t(y)});},
            [this](Address entry){return callback(0x45d89c,{entry});},
            [this](Address object,std::uint32_t script){callback(0x447841,{object,script});},
            [this](std::int32_t x,std::int32_t y,std::int32_t layer,std::int32_t variant,std::uint32_t item,std::uint32_t quantity){
                callback(0x4552a3,{std::uint32_t(x),std::uint32_t(y),std::uint32_t(layer),std::uint32_t(variant),item,quantity});}};
}
bool RecoveredBattle::dispatch_c_maps(Address entry){
    const auto signed_argument=[&](unsigned index){return signed32(argument(index));};
    switch(entry){
    case 0x496fe8:{ // stdcall(flag_id); EAX keeps the shifted mask, unused by callers.
        const auto id=signed_argument(0);map_logic::EventFlags(memory_).set(id);
        result(1u<<(std::uint32_t(id%32)&31),4);return true;
    }
    case 0x49700e:{ // stdcall(flag_id); EAX keeps the inverted mask, unused by callers.
        const auto id=signed_argument(0);map_logic::EventFlags(memory_).clear(id);
        result(~(1u<<(std::uint32_t(id%32)&31)),4);return true;
    }
    case 0x497036: result(map_logic::EventFlags(memory_).test(signed_argument(0))?1:0,4);return true;
    case 0x497065:{ // stdcall(patch_id,event_flag); EAX is1 unless the id is out of table range.
        Map(memory_).apply_patch(argument(0),argument(1)!=0);result(1,8);return true;
    }
    case 0x49711f:{
        const auto map_id=argument(0);Map map(memory_);
        map_logic::resync_map_patches(memory_,map,map_id);
        // The original leaves its last table probe, or the1 of the last497065
        // call, in EAX. No caller reads it; publish the same incidental value.
        const auto last=memory_.read(tables::map_patches+(map_logic::map_patch_records-1)*map_logic::map_patch_stride);
        result(last==map_id?1:last,4);return true;
    }
    case 0x486b8c:case 0x486bfe:case 0x486c6f:case 0x486cf9:
    case 0x486d71:case 0x486dfb:case 0x488631:case 0x4886b4:
    case 0x487122:case 0x493a26:case 0x4870c6:
    case 0x493e8e:case 0x48bd7d:case 0x491b53:case 0x494066:case 0x49356f:case 0x48bea6:{
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        objects.tick(entry,argument(0));
        result(0,4);return true; // These callbacks publish state; the tick loop ignores EAX.
    }
    case 0x486eb7:case 0x48745b:{
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        if(entry==0x486eb7)objects.clear_region_animation(argument(0));else objects.tick_chest_timer(argument(0));
        result(0,4);return true;
    }
    case 0x4874df:{ // stdcall(x,y,unused,z,script); the caller keeps the object.
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        result(objects.spawn_marker(signed32(argument(0)),signed32(argument(1)),signed32(argument(3)),signed32(argument(4))),20);
        return true;
    }
    case 0x4874a7:case 0x4897a3:case 0x489a60:case 0x489b53:case 0x48897a:case 0x488aa7:case 0x488737:{
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        const auto object=argument(0);
        switch(entry){
        case 0x4874a7:objects.tick_map_marker(object);break;
        case 0x4897a3:objects.tick_pushable(object);break;
        case 0x489a60:objects.tick_gate_front_piece(object);break;
        case 0x489b53:objects.tick_gate_back_piece(object);break;
        case 0x48897a:objects.tick_conveyor_rider(object);break;
        case 0x488737:objects.tick_platform(object);break;
        default:objects.tick_conveyor_shadow(object);break;
        }
        result(0,4);return true; // The object pump does not read EAX.
    }
    case 0x4870e2:{ // stdcall(object,tile_x,tile_y)
        Map map(memory_);map_logic::MapObjects(memory_,map,{}).set_step_target(argument(0),argument(1),argument(2));
        result(0,12);return true;
    }
    case 0x486b53:{
        Map map(memory_);map_logic::MapObjects(memory_,map,{}).restore_active_party_vitals();
        result(0);return true; // cdecl with no arguments; the caller ignores EAX.
    }
    case 0x435fac: map_logic::Worldmap(memory_).set_destination(argument(0),argument(1),argument(2));result(0,12);return true;
    case 0x435fca: map_logic::Worldmap(memory_).set_script_toggle(argument(0),argument(1)!=0);result(0,8);return true;
    case 0x435ffa: map_logic::Worldmap(memory_).set_display_state(argument(0),argument(1));result(0,8);return true;
    case 0x436060: map_logic::Worldmap(memory_).force_endpoint_label(argument(0));result(0,4);return true;
    case 0x436077: result(map_logic::Worldmap(memory_).endpoint_is_present(argument(0),argument(1))?1:0,8);return true;
    case 0x4360be: result(map_logic::Worldmap(memory_).enter_from(argument(0))?1:0,4);return true;
    case 0x436100: result(map_logic::Worldmap(memory_).select_effect_mode(signed32(argument(0)))?1:0,4);return true;
    case 0x43611c: result(map_logic::Worldmap(memory_).route_cue(argument(0),argument(1)),8);return true;
    case 0x43700a: map_logic::Worldmap(memory_).clear_visited();result(0);return true;
    case 0x4384a0: result(0,4);return true; // The original body is a bare return.
    case 0x48c256:{ // Sluice patch63: the two column-7 cells stop animating.
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        map.apply_patch(0x63,true);
        objects.seed_tile_animation(7,6,0xffffffffu);objects.seed_tile_animation(7,7,0xffffffffu);
        result(0);return true;
    }
    case 0x491d2b:{ // Patch db: the two column-10 cells stop animating.
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        map.apply_patch(0xdb,true);
        objects.seed_tile_animation(10,5,0xffffffffu);objects.seed_tile_animation(10,6,0xffffffffu);
        result(0);return true;
    }
    case 0x496650:{ // Patch191 seeds the transition column with descriptors6 and7.
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        map.apply_patch(0x191,true);
        objects.seed_tile_animation(8,5,6);objects.seed_tile_animation(8,6,7);
        result(0);return true;
    }
    case 0x49667f:{ // Clears the same transition scratch again, patches and cells.
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        for(auto patch:{0x191u,0x192u,0x193u,0x194u})map.apply_patch(patch,false);
        for(auto row:{5u,6u}){objects.seed_tile_animation(8,row,0xffffffffu);objects.seed_tile_animation(14,row,0xffffffffu);}
        for(auto row:{3u,4u,7u,8u})objects.seed_tile_animation(11,row,0xffffffffu);
        result(0);return true;
    }
    case 0x492a85:{
        Map map(memory_);map_logic::MapObjects(memory_,map,{}).release_pending_event_entry();
        result(0);return true;
    }
    case 0x494352:{
        Map map(memory_);map_logic::MapObjects(memory_,map,{}).arm_switch_hold();
        result(0,4);return true;
    }
    case 0x438493: memory_.write(0x7714a4,5);result(0,4);return true; // Route step: hand the screen back.
    case 0x4898d3:{ // stdcall(x,y,z); the caller keeps the spawned object.
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        result(objects.spawn_pushable(signed32(argument(0)),signed32(argument(1)),signed32(argument(2))),12);return true;
    }
    case 0x488bb6:{
        Map map(memory_);map_logic::MapObjects(memory_,map,legacy_map_services()).spawn_conveyor_pair();
        result(0);return true;
    }
    case 0x489c47:{ // stdcall(x,y,z)
        Map map(memory_);map_logic::MapObjects(memory_,map,legacy_map_services())
            .spawn_gate_pair(signed32(argument(0)),signed32(argument(1)),signed32(argument(2)));
        result(0,12);return true;
    }
    case 0x48880e:{
        Map map(memory_);map_logic::MapObjects(memory_,map,legacy_map_services()).spawn_platform_pair();
        result(0);return true;
    }
    case 0x489929:{
        Map map(memory_);map_logic::MapObjects(memory_,map,legacy_map_services()).spawn_switch(argument(0));
        result(0,4);return true;
    }
    case 0x4899f6:case 0x489a18:case 0x489a3c:{
        Map map(memory_);map_logic::MapObjects objects(memory_,map,legacy_map_services());
        const unsigned first=entry==0x4899f6?0:entry==0x489a18?7:12;
        objects.spawn_switch_range(first,entry==0x4899f6?7:entry==0x489a18?12:17);
        result(0);return true;
    }
    case 0x489d3c:case 0x489e7f:case 0x489ed7:case 0x489ef5:{
        Map map(memory_);
        const unsigned crossing=entry==0x489d3c?0:entry==0x489e7f?1:entry==0x489ed7?2:3;
        map_logic::MapObjects(memory_,map,legacy_map_services()).install_gate_crossing(crossing);
        result(0);return true;
    }
    default:return false;
    }
}
} // namespace fsb::core
