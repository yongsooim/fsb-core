#include "fsb_core/combat/anchor_effects.hpp"
#include "fsb_core/symbols.hpp"
namespace fsb::core::combat {
using namespace anchor_effects;
void AnchorEffects::scatter_cluster(Address object) {
    const auto owner=memory_.read(object+anchor);
    const auto state=signed32(memory_.read(object+actor_offset::callback_state));
    if(state==cleanup) {
        for(unsigned i=0;i<scatter_count;++i)if(const auto child=memory_.read(object+children+i*4))services_.release(child);
        return;
    }
    if(state==looping) {
        for(unsigned i=0;i<scatter_count;++i) {
            const auto child=memory_.read(object+children+i*4);
            memory_.write(child+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
            memory_.write(child+offset_x,memory_.read(owner+actor_offset::world_x));
            memory_.write(child+offset_y,memory_.read(owner+actor_offset::world_y));
            services_.oscillate(child);
            const auto x=signed32(memory_.read(child+scatter_velocity_x));
            memory_.write(child+scatter_velocity_x,std::uint32_t(x>scatter_x_speed_cap?scatter_x_speed_cap:x));
            const auto y=signed32(memory_.read(child+scatter_velocity_y));
            memory_.write(child+scatter_velocity_y,std::uint32_t(y>scatter_y_speed_cap?scatter_y_speed_cap:y));
        }
        return;
    }
    if(state!=active)return;
    for(unsigned i=0;i<scatter_count;++i) {
        const auto child=services_.spawn(0);
        memory_.write(child+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
        const bool right=(services_.random()&1)!=0;
        const auto x_roll=random_remainder(scatter_x_variants);
        const auto dx=right?scatter_x_extent+x_roll:-scatter_x_extent-x_roll;
        memory_.write(child+actor_offset::world_x,(std::uint32_t(dx)<<16)+memory_.read(owner+actor_offset::world_x));
        const bool lower=(services_.random()&1)!=0;
        const auto y_roll=random_remainder(scatter_y_variants);
        const auto dy=lower?scatter_y_extent+y_roll:-scatter_y_extent-y_roll;
        const auto y=memory_.read(owner+actor_offset::world_y);
        memory_.write(child+actor_offset::elevation,0);memory_.write(child+motion,scatter_mode);
        memory_.write(child+actor_offset::world_y,(std::uint32_t(dy)<<16)+y+scatter_y_bias);
        memory_.write(child+offset_x,memory_.read(owner+actor_offset::world_x));
        memory_.write(child+offset_y,memory_.read(owner+actor_offset::world_y)+scatter_y_bias);
        const auto ax=signed32(std::uint32_t(random_remainder(scatter_x_speed_variants))<<16)/scatter_speed_scale;
        memory_.write(child+scatter_acceleration_x,std::uint32_t(ax)+scatter_x_acceleration);
        const auto ay=signed32(std::uint32_t(random_remainder(scatter_y_speed_variants))<<16)/scatter_speed_scale;
        memory_.write(child+scatter_acceleration_y,std::uint32_t(ay)+scatter_y_acceleration);
        const auto script=memory_.read(scatter_scripts+std::uint32_t(random_remainder(scatter_script_count))*4);
        services_.start_script(child,script);memory_.write(object+children+i*4,child);
    }
    memory_.write(object+actor_offset::callback_state,memory_.read(object+actor_offset::callback_state)+phase_step);
}
void AnchorEffects::spiral(Address object,bool second) {
    const auto state=signed32(memory_.read(object+actor_offset::callback_state));
    const auto owner=memory_.read(object+anchor);
    if(state==active) {
        memory_.write(object+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
        memory_.write(object+motion,spiral_mode);memory_.write(object+radius_x,ring_radius_x);
        memory_.write(object+radius_y,script_ring_radius_y);memory_.write(object+radius_z,spiral_z_radius);
        memory_.write(object+angle_x,half_turn);memory_.write(object+angle_y,half_turn);
        memory_.write(object+angle_z,second?three_quarters:quarter_turn);
        services_.start_script(object,second?spiral_script_b:spiral_script_a);
        memory_.write(object+actor_offset::callback_state,memory_.read(object+actor_offset::callback_state)+phase_step);
    } else if(state==looping) {
        memory_.write(object+actor_offset::layer_q16,memory_.read(owner+actor_offset::layer_q16));
        memory_.write(object+offset_x,memory_.read(owner+actor_offset::world_x));
        const auto y=memory_.read(owner+actor_offset::world_y);
        auto angle=memory_.read(object+angle_y)+spiral_step;
        memory_.write(object+angle_y,angle);memory_.write(object+offset_y,y);memory_.write(object+target_z,script_ring_height);
        if(signed32(angle)>=int(turn)){angle-=turn;memory_.write(object+angle_y,angle);}
        const auto z_angle=signed32(angle+(second?quarter_turn:three_quarters))%int(turn);
        memory_.write(object+angle_x,angle);memory_.write(object+angle_z,std::uint32_t(z_angle));services_.oscillate(object);
    }
}
}
