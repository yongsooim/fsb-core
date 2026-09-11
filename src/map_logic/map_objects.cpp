#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/map_logic/event_flags.hpp"
#include "fsb_core/map_logic/map_patch_table.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::map_logic {
namespace {
constexpr std::int32_t always_open=-1,consumed=-2;
constexpr std::int32_t no_sound=-1,door_open_cue=0x14f,door_shut_marker=0x15d,door_shut_cue=0x15e;
constexpr std::int32_t toggle_pair_marker=0x15b,toggle_pair_off_cue=0x15c,lowest_playable_cue=0x64;
constexpr std::int32_t hold_frames=8; // Both door variants hold the patch for eight frames.
}
bool MapObjects::gate_open(Address slot)const{
    const auto gate=signed32(memory_.read(slot+overlay_offset::gate_flag));
    if(gate>=0)return EventFlags(memory_).test(gate);
    return gate==always_open;
}
std::int32_t MapObjects::patch_of(Address slot)const{return signed32(memory_.read(slot+overlay_offset::patch_id));}
std::int32_t MapObjects::patch_sound(std::int32_t patch)const{
    return signed32(memory_.read(tables::map_patches+std::uint32_t(patch)*map_patch_stride+map_patch_offset::sound_cue));
}
void MapObjects::stop_running(Address slot){
    const auto at=slot+overlay_flag_byte;
    memory_.write(at,memory_.read(at,1)&~std::uint32_t(overlay_running_bit),1);
}
void MapObjects::play(unsigned cue)const{
    if(!services_.play_cue)throw Fault(0x435373,"map object sound cue is not connected");
    services_.play_cue(cue);
}
void MapObjects::move_party_to_its_own_tile(){
    // The router moved the party in world units; snap the tile fields to match
    // and drop the pending motion so the next frame starts from the new cell.
    const auto actor=Actors::slot(memory_.read(globals::active_party_index));
    memory_.write(actor+actor_offset::motion_state,0);
    set_actor_tile_position(memory_,actor,
        signed32(memory_.read(actor+actor_offset::world_x))/units::q16_one/64,
        signed32(memory_.read(actor+actor_offset::world_y))/units::q16_one/48,
        signed32(memory_.read(actor+actor_offset::layer_q16))/units::q16_one);
}
void MapObjects::clear_entry_on_event(Address slot){
    if(!memory_.read(field_marker_events_enabled))return;
    if(!services_.position_event_trigger)throw Fault(0x412181,"field position router is not connected");
    if(services_.position_event_trigger())move_party_to_its_own_tile();
    //457b7c zeroes the whole record, so the slot is free again immediately.
    for(unsigned offset=0;offset<overlay_stride;++offset)memory_.write(slot+offset,0,1);
}
void MapObjects::stop_running_on_event(Address slot){
    if(!memory_.read(field_marker_events_enabled))return;
    if(!services_.position_event_trigger)throw Fault(0x412181,"field position router is not connected");
    if(services_.position_event_trigger())move_party_to_its_own_tile();
    stop_running(slot); // The record stays registered, so the marker can fire again.
}
void MapObjects::tick_patch_toggle(Address slot){
    if(!gate_open(slot))return;
    // Held-down doors need eight consecutive presented frames, not eight ticks.
    const auto frame=memory_.read(globals::presented_frame_counter),held=memory_.read(slot+overlay_offset::scratch);
    if(!held||memory_.read(slot+overlay_offset::scratch_frame)+1==frame){
        memory_.write(slot+overlay_offset::scratch,held+1);
        memory_.write(slot+overlay_offset::scratch_frame,frame);
    }else{
        memory_.write(slot+overlay_offset::scratch,0);
        memory_.write(slot+overlay_offset::scratch_frame,0);
    }
    if(signed32(memory_.read(slot+overlay_offset::scratch))>=hold_frames){
        const auto patch=patch_of(slot);
        map_.apply_patch(std::uint32_t(patch),true);
        memory_.write(slot+overlay_offset::scratch,0);
        const auto cue=patch_sound(patch);
        // The toggle variant keeps the descriptor marker; it does not swap in
        //15e the way the open/close pair does.
        if(cue==door_open_cue||cue==door_shut_marker)play(unsigned(cue));
    }
    stop_running(slot);
}
void MapObjects::tick_patch_open(Address slot){
    if(!gate_open(slot))return;
    const auto patch=patch_of(slot);
    if(!memory_.read(slot+overlay_offset::ticks))map_.apply_patch(std::uint32_t(patch),true);
    if(memory_.read(slot+overlay_offset::ticks)>std::uint32_t(hold_frames)){
        map_.apply_patch(std::uint32_t(patch),false);
        memory_.write(slot+overlay_offset::ticks,0);
        stop_running(slot);
        const auto cue=patch_sound(patch);
        if(cue==door_open_cue)play(door_open_cue);
        else if(cue==door_shut_marker)play(door_shut_cue);
    }
}
void MapObjects::tick_patch_close(Address slot){
    if(!gate_open(slot)||!memory_.read(slot+overlay_offset::ticks))return;
    const auto patch=patch_of(slot);
    map_.apply_patch(std::uint32_t(patch),false);
    stop_running(slot);
    const auto cue=patch_sound(patch);
    if(cue==door_open_cue)play(door_open_cue);
    else if(cue==door_shut_marker)play(door_shut_cue);
}
void MapObjects::tick_patch_trigger(Address slot){
    // Switches whose patch flag is its own memory: +36 keeps the record armed
    // after it fires, so stepping off and on again toggles the patch back.
    const auto patch=patch_of(slot);
    const EventFlags flags(memory_);
    const bool stays_armed=memory_.read(slot+overlay_offset::scratch)!=0;
    if(!gate_open(slot)){
        if(stays_armed&&flags.test(patch))return;
        memory_.write(slot+overlay_offset::ticks,0);
        stop_running(slot);
        return;
    }
    if(!stays_armed||!flags.test(patch)){
        map_.apply_patch(std::uint32_t(patch),!flags.test(patch));
        const auto cue=patch_sound(patch);
        if(cue!=no_sound){
            // The paired marker picks its cue from the patch state it just wrote.
            if(cue==toggle_pair_marker)play(unsigned(flags.test(patch)?toggle_pair_marker:toggle_pair_off_cue));
            else if(cue>lowest_playable_cue)play(unsigned(cue));
        }
        // A one-shot gate id is spent here, not cleared, so it can never re-arm.
        if(signed32(memory_.read(slot+overlay_offset::gate_flag))>always_open)
            memory_.write(slot+overlay_offset::gate_flag,std::uint32_t(consumed));
    }
    memory_.write(slot+overlay_offset::scratch_frame,1);
    if(!stays_armed)stop_running(slot);
}
void MapObjects::tick_platform_lower_track(Address slot){
    // +32 is the platform actor index here, not a patch id. The track drives the
    // rider's motion frame for four ticks and then releases the record.
    const auto platform=Actors::slot(memory_.read(slot+overlay_offset::patch_id));
    const auto ticks=memory_.read(slot+overlay_offset::ticks);
    memory_.write(platform+actor_offset::motion_frame,ticks>=1&&ticks<=4?ticks-1:0xffffffffu);
    if(ticks>std::uint32_t(hold_frames)){memory_.write(slot+overlay_offset::ticks,0);stop_running(slot);}
}
void MapObjects::tick_platform_upper_track(Address slot){
    const auto platform=Actors::slot(memory_.read(slot+overlay_offset::patch_id));
    const auto ticks=memory_.read(slot+overlay_offset::ticks);
    memory_.write(platform+actor_offset::motion_frame,ticks>=3&&ticks<=6?6-ticks:0xffffffffu);
    if(ticks>std::uint32_t(hold_frames)){memory_.write(slot+overlay_offset::ticks,0);stop_running(slot);}
}
void MapObjects::set_step_target(Address object,std::uint32_t x,std::uint32_t y){
    // Writes one step command in the object's path buffer: the tile it stands on
    // followed by the tile it should walk to, all as16-bit tile coordinates.
    const auto step=object+actor_offset::path_commands+2;
    memory_.write(object+actor_offset::motion_frame,0);
    memory_.write(step,memory_.read(object+actor_offset::tile_x,2),2);
    memory_.write(step+2,memory_.read(object+actor_offset::tile_y,2),2);
    memory_.write(step+4,x&0xffffu,2);
    memory_.write(object+actor_offset::motion_state,1);
    memory_.write(step+6,y&0xffffu,2);
}
void MapObjects::tick_block_switch(Address slot){
    // The trigger tile holds the switch id; the block itself is a linked object
    // that walks between the record's closed and open tiles.
    const auto linked=Actors::slot(memory_.read(slot+overlay_offset::scratch));
    const auto tile_x=memory_.read(slot+overlay_offset::left),tile_y=memory_.read(slot+overlay_offset::top);
    if(memory_.read(linked+actor_offset::motion_state))return; // Still walking; try again next frame.
    const auto id=memory_.read(slot+overlay_offset::patch_id),bit=1u<<(id&31);
    const auto raised=(memory_.read(switch_raised_bits)&bit)!=0;
    if(!raised&&signed32(memory_.read(lifted_block_count))<=0){stop_running(slot);return;}
    // The marker tile one row above the trigger mirrors the lifted block count.
    const auto marker=globals::map_tile_codes+((tile_y-1)*memory_.read(globals::map_layer_width)+tile_x)*4;
    memory_.write(marker,memory_.read(marker)+(raised?0xffffffffu:1));
    memory_.write(lifted_block_count,memory_.read(lifted_block_count)+(raised?1:0xffffffffu));
    memory_.write(switch_raised_bits,raised?memory_.read(switch_raised_bits)&~bit:memory_.read(switch_raised_bits)|bit);
    play(switch_toggle_cue);
    const auto record=switch_records+id*switch_record_stride;
    const auto target=record+(raised?switch_record_offset::closed_x:switch_record_offset::open_x);
    set_step_target(linked,memory_.read(target),memory_.read(target+4));
    stop_running(slot);
}
void MapObjects::tick_falling_floor(Address slot){
    const auto ticks=memory_.read(slot+overlay_offset::ticks);
    if(ticks>7)return; // The record stays registered but idle once it has fallen.
    const auto actor=Actors::slot(memory_.read(globals::active_party_index));
    constexpr std::uint8_t airborne=0x80;
    if(!ticks){
        memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags,1)&~std::uint32_t(airborne),1);
        map_.apply_patch(std::uint32_t(patch_of(slot)),true);
    }
    // Eight frames of a quarter tile carry the party down one map layer.
    memory_.write(actor+actor_offset::tile_y_q16,memory_.read(actor+actor_offset::tile_y_q16)+0x4000);
    if(ticks==7){
        memory_.write(actor+actor_offset::motion_state,0);
        memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags,1)|airborne,1);
        stop_running(slot);
        memory_.write(slot+overlay_offset::ticks,0);
        memory_.write(globals::field_transition_phase,0);
    }
}
void MapObjects::restore_active_party_vitals(){
    const auto count=signed32(memory_.read(globals::party_count));
    for(std::int32_t member=0;member<count;++member){
        const auto record=party_stat_records+memory_.read(globals::party_actor_ids+member*4)*party_stat_stride;
        memory_.write(record+party_stat_offset::health,memory_.read(record+party_stat_offset::max_health));
        memory_.write(record+party_stat_offset::resource,memory_.read(record+party_stat_offset::max_resource));
    }
}
bool MapObjects::tick(Address callback,Address slot){
    // Each gate handler carries the flag combination the original spells out.
    static constexpr GateCondition tmo8_gate[]={{0x11d,true},{0x11e,true},{0x11f,false},{0x120,true}};
    static constexpr GateCondition column_gate[]={{0x5a,false},{0x5b,false},{0x5c,true},{0x5d,false},{0x5e,false},{0x5f,false}};
    static constexpr GateCondition ttd9_gate[]={{0xd7,true},{0xd8,false},{0xd9,false},{0xda,true}};
    switch(callback){
    case 0x493e8e:tick_patch_and_gate(slot,{tmo8_gate,4,true,false});return true;
    case 0x48bd7d:tick_patch_and_gate(slot,{column_gate,6,false,false});return true;
    case 0x491b53:tick_patch_and_gate(slot,{ttd9_gate,4,false,true});return true;
    case 0x494066:tick_patch_toggle_pair(slot);return true;
    case 0x49356f:tick_event_flag_toggle(slot);return true;
    case 0x48bea6:tick_gated_patch_claim(slot);return true;
    case 0x486b8c:clear_entry_on_event(slot);return true;
    case 0x486bfe:stop_running_on_event(slot);return true;
    case 0x486c6f:tick_patch_toggle(slot);return true;
    case 0x486cf9:tick_patch_open(slot);return true;
    case 0x486d71:tick_patch_close(slot);return true;
    case 0x486dfb:tick_patch_trigger(slot);return true;
    case 0x488631:tick_platform_lower_track(slot);return true;
    case 0x4886b4:tick_platform_upper_track(slot);return true;
    case 0x487122:tick_block_switch(slot);return true;
    case 0x493a26:tick_falling_floor(slot);return true;
    case 0x4870c6:trigger_position_event(slot);return true;
    default:return false;
    }
}
} // namespace fsb::core::map_logic
