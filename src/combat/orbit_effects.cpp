#include "fsb_core/combat/anchor_effects.hpp"
#include "fsb_core/symbols.hpp"
namespace fsb::core::combat {
using namespace anchor_effects;
void AnchorEffects::tick_orbit(Address object) {
    const auto state=memory_.read(object+actor_offset::callback_state),owner=memory_.read(object+anchor);
    if(state!=unsigned(active))return;
    memory_.write(object+motion,mark_motion);services_.oscillate(object);
    memory_.write(object+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
    memory_.write(object+offset_x,memory_.read(owner+actor_offset::world_x));
    const auto y=memory_.read(owner+actor_offset::world_y);
    auto phase=memory_.read(object+angle_y)+orbit_step;
    memory_.write(object+angle_y,phase);memory_.write(object+offset_y,y);
    if(signed32(phase)>=int(turn)){phase-=turn;memory_.write(object+angle_y,phase);}
    memory_.write(object+angle_x,phase);memory_.write(object+motion,orbit_motion);services_.oscillate(object);
}
void AnchorEffects::orbit_ring(Address object,bool scripted) {
    const auto owner=memory_.read(object+anchor);
    const auto state=signed32(memory_.read(object+actor_offset::callback_state));
    if(state==cleanup) {
        for(unsigned i=0;i<ring_count;++i)if(const auto child=memory_.read(object+children+i*4))services_.release(child);
        return; // Original retains these handles until the parent itself is wiped.
    }
    if(state!=active)return;
    for(unsigned i=0;i<ring_count;++i) {
        const auto phase=i*ring_spacing,child=services_.spawn(orbit_callback);
        memory_.write(child+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
        memory_.write(child+actor_offset::elevation,scripted?script_ring_height:ring_height);
        std::int32_t speed=0;
        if(!scripted) {
            memory_.write(child+target_z,ring_target);speed=random_remainder(ring_speed_variants);
            memory_.write(child+actor_offset::flags,memory_.read(child+actor_offset::flags)|visible);
        }
        memory_.write(child+radius_x,ring_radius_x);memory_.write(child+radius_y,scripted?script_ring_radius_y:ring_radius_y);
        memory_.write(child+angle_x,phase);memory_.write(child+angle_y,phase);memory_.write(child+anchor,owner);
        if(scripted)services_.start_script(child,ring_script);
        else {
            memory_.write(child+actor_offset::sprite_selector,ring_sprite);
            memory_.write(child+step_z,std::uint32_t(signed32(std::uint32_t(speed)<<16)/ring_speed_scale)+ring_speed_base);
            const auto frame=random_remainder(ring_frame_variants);
            memory_.write(child+actor_offset::flags,memory_.read(child+actor_offset::flags)|visible);
            memory_.write(child+actor_offset::sprite_frame,std::uint32_t(frame)+ring_frame_base);
        }
        memory_.write(object+children+i*4,child);
    }
    memory_.write(object+actor_offset::callback_state,memory_.read(object+actor_offset::callback_state)+phase_step);
}
}
