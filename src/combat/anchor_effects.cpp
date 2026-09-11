#include "fsb_core/combat/anchor_effects.hpp"
#include "fsb_core/symbols.hpp"
#include <limits>
namespace fsb::core::combat {
using namespace anchor_effects;
std::int32_t AnchorEffects::random_remainder(std::int32_t divisor) {
    return signed32(services_.random())%divisor; // All callers here use positive fixed divisors.
}
void AnchorEffects::anchor_position(Address object,Address owner) {
    memory_.write(object+actor_offset::world_x,memory_.read(object+offset_x)+memory_.read(owner+actor_offset::world_x));
    memory_.write(object+actor_offset::world_y,memory_.read(object+offset_y)+memory_.read(owner+actor_offset::world_y));
}
void AnchorEffects::tick_exploding_mark(Address object) {
    const auto state=signed32(memory_.read(object+actor_offset::callback_state));
    const auto owner=memory_.read(object+anchor);
    if(state==active) {
        anchor_position(object,owner);
        if(!(memory_.read(object+actor_offset::flags+2,1)&(script_active>>16))) {
            services_.start_script(object,explosion_script);
            memory_.write(object+actor_offset::callback_state,memory_.read(object+actor_offset::callback_state)+phase_step);
        }
    } else if(state==looping) {
        memory_.write(object+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
        anchor_position(object,owner);services_.oscillate(object);
        if(signed32(memory_.read(object+actor_offset::elevation))<signed32(memory_.read(object+target_z)))return;
        // Random is drawn before the height is read again. A callback may change it.
        const auto random=signed32(services_.random());
        const auto height=signed32(memory_.read(object+actor_offset::elevation))/int(turn);
        if(!height||(random==std::numeric_limits<std::int32_t>::min()&&height==-1))
            throw Fault(0x462515,"anchor explosion division fault");
        const auto remainder=random%height;
        if(remainder<=signed32(memory_.read(object+target_z))/int(turn))return;
        services_.spark_cluster(object);services_.release(object);
    }
}
void AnchorEffects::emit_floating_marks(Address object) {
    const auto state=signed32(memory_.read(object+actor_offset::callback_state));
    const auto owner=memory_.read(object+anchor);
    if(state==initialize){memory_.write(object+next_mark,mark_interval);return;}
    if(state!=active||memory_.read(object+actor_offset::callback_tick_count)!=memory_.read(object+next_mark))return;
    const auto child=services_.spawn(mark_callback);
    memory_.write(child+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
    memory_.write(child+actor_offset::world_x,memory_.read(owner+actor_offset::world_x));
    const auto y=memory_.read(owner+actor_offset::world_y);
    memory_.write(child+actor_offset::elevation,0);memory_.write(child+actor_offset::world_y,y);
    memory_.write(child+offset_x,std::uint32_t(random_remainder(mark_x_variants)-mark_x_center)<<16);
    memory_.write(child+offset_y,std::uint32_t(random_remainder(mark_y_variants)-mark_y_center)<<16);
    anchor_position(object,owner);
    memory_.write(child+motion,mark_motion);
    const auto height=random_remainder(mark_z_variants);
    memory_.write(child+velocity_z,mark_velocity);memory_.write(child+target_z,std::uint32_t(height+mark_z_base)<<16);
    services_.start_script(child,mark_script);
    memory_.write(child+anchor,owner); // Original publishes the owner only after script start.
    memory_.write(object+next_mark,memory_.read(object+next_mark)+mark_interval);
}
void AnchorEffects::spawn_rising(Address source) {
    const auto child=services_.spawn(rising_callback);
    memory_.write(child+actor_offset::layer_q16,memory_.read(source+actor_offset::layer_q16));
    memory_.write(child+actor_offset::world_x,memory_.read(source+actor_offset::world_x)+rising_x_bias);
    memory_.write(child+actor_offset::world_y,memory_.read(source+actor_offset::world_y)+rising_y_bias);
    const auto height=memory_.read(source+actor_offset::elevation)+rising_z_bias;
    memory_.write(child+motion,rising_motion);memory_.write(child+actor_offset::elevation,height);
    memory_.write(child+radius_x,1);memory_.write(child+radius_z,1);
    // Keep the two signed divisions after the32-bit shift in the original.
    const auto scaled=signed32(std::uint32_t(random_remainder(rising_phase_variants))<<16);
    const auto phase=std::uint32_t((scaled/rising_phase_variants)/16)+unsigned(rising_phase_variants);
    memory_.write(child+angle_x,phase);memory_.write(child+angle_z,phase);
    services_.oscillate(child);
    memory_.write(child+actor_offset::flags,memory_.read(child+actor_offset::flags)|visible);
    memory_.write(child+actor_offset::sprite_selector,rising_sprite);memory_.write(child+actor_offset::sprite_frame,rising_frame);
}
void AnchorEffects::emit_rising(Address object) {
    const auto state=signed32(memory_.read(object+actor_offset::callback_state));
    const auto owner=memory_.read(object+anchor);
    if(state==initialize){memory_.write(object+next_rising,first_rising);return;}
    if(state!=active)return;
    const auto tick=memory_.read(object+actor_offset::callback_tick_count);
    if(tick!=memory_.read(object+next_rising))return;
    memory_.write(object+next_rising,tick+rising_interval);services_.spawn_rising(owner);
}
}
