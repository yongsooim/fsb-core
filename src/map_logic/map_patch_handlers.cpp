#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/map_logic/event_flags.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::map_logic {
namespace {
// The animation lookup holds three words per cell; only the first selects the
// descriptor, and -1 there leaves the cell unanimated.
constexpr unsigned animation_lookup_stride=12;
constexpr std::uint8_t airborne_bit=0x80;
}
void MapObjects::seed_tile_animation(unsigned column,unsigned row,std::uint32_t index){
    const auto cell=row*memory_.read(globals::map_layer_width)+column;
    memory_.write(globals::tile_animation_lookup+cell*animation_lookup_stride,index);
}
void MapObjects::tick_patch_and_gate(Address slot,const GateSequence& sequence){
    const EventFlags flags(memory_);
    const auto patch=patch_of(slot),gate=signed32(memory_.read(slot+overlay_offset::gate_flag));
    const auto toggle_patch=[&]{map_.apply_patch(std::uint32_t(patch),!flags.test(patch));};
    if(sequence.cue_before_patch)play(patch_toggle_cue);
    bool before=false;
    if(sequence.snapshot_after_patch){toggle_patch();before=flags.test(gate);}
    else{before=flags.test(gate);toggle_patch();}
    stop_running(slot);
    if(!sequence.cue_before_patch)play(patch_toggle_cue);
    // Every term has to match; the original stops at the first one that does not.
    bool combination=true;
    for(unsigned i=0;i<sequence.count&&combination;++i)
        combination=flags.test(sequence.conditions[i].flag)==sequence.conditions[i].wanted;
    map_.apply_patch(std::uint32_t(gate),combination);
    // The move sound only plays when the gate patch actually changed side.
    if(before!=flags.test(gate))play(patch_moved_cue);
}
void MapObjects::tick_patch_toggle_pair(Address slot){
    const EventFlags flags(memory_);
    const auto patch=patch_of(slot);
    map_.apply_patch(std::uint32_t(patch),!flags.test(patch));
    memory_.write(slot+overlay_offset::ticks,0);
    stop_running(slot);
    play(patch_moved_cue);play(patch_toggle_cue);
}
void MapObjects::tick_event_flag_toggle(Address slot){
    EventFlags flags(memory_);
    const auto id=patch_of(slot);
    if(flags.test(id))flags.clear(id);else flags.set(id);
    play(gate_toggle_cue);
    memory_.write(slot+overlay_offset::ticks,0);
    stop_running(slot);
}
void MapObjects::tick_gated_patch_claim(Address slot){
    const auto gate=signed32(memory_.read(slot+overlay_offset::gate_flag));
    if(gate<=-1)return; // Already claimed; the record stays registered but idle.
    EventFlags flags(memory_);
    if(!flags.test(gate)){memory_.write(slot+overlay_offset::ticks,0);stop_running(slot);return;}
    map_.apply_patch(std::uint32_t(patch_of(slot)),true);
    flags.set(signed32(memory_.read(slot+overlay_offset::scratch)));
    memory_.write(slot+overlay_offset::gate_flag,0xffffffffu);
    play(gate_toggle_cue);
    memory_.write(slot+overlay_offset::ticks,0);
}
void MapObjects::release_pending_event_entry(){
    memory_.write(event_entry_pending,0);
    for(auto slot:event_entry_slots)
        if(memory_.read(slot)==released_event_entry)memory_.write(slot,0xffffffffu);
    memory_.write(event_entry_marker,0xffffffffu);
}
void MapObjects::arm_switch_hold(){
    const auto actor=Actors::slot(memory_.read(globals::active_party_index));
    const auto flags=memory_.read(actor+actor_offset::flags);
    if(!(flags&airborne_bit))return;
    memory_.write(pending_switch_hold,1);
    memory_.write(actor+actor_offset::flags,flags&~std::uint32_t(airborne_bit));
}
} // namespace fsb::core::map_logic
