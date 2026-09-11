#include "fsb_core/combat/handler_lifecycle.hpp"

namespace fsb::core::combat {
namespace {
using namespace handler_flow;
template<class F> const F& required(const F& service,Address at) {
    if(!service)throw Fault(at,"battle handler service is not connected");
    return service;
}
}
void HandlerLifecycle::spawn_pending(Address callback,Address failure_message) {
    const auto object=required(services_.spawn,0x45d89c)(callback);
    memory_.write(active_handler,object);
    if(!object)required(services_.report_failure,0x401ad8)(failure_message);
    // Original spawn failure still enters pending; the next poll collapses it.
    memory_.write(completion_latch,pending);
}
bool HandlerLifecycle::start_player(std::uint32_t mode,unsigned action) {
    Address callback=0,message=0;
    switch(static_cast<Mode>(mode)) {
    case Mode::None: memory_.write(completion_latch,completed);return false;
    case Mode::PlayerAttack: callback=memory_.read(player_callbacks+action*player_stride);message=player_failure;break;
    case Mode::SkillOrItem: callback=memory_.read(skill_callbacks+action*skill_stride);message=skill_failure;break;
    case Mode::SpecialItem: callback=memory_.read(special_item_callbacks+action*special_item_stride);message=special_item_failure;break;
    default:return false; // Unknown mode leaves both the object and latch alone.
    }
    if(!callback){memory_.write(completion_latch,completed);return false;}
    spawn_pending(callback,message);return true;
}
bool HandlerLifecycle::start_enemy(unsigned action) {
    if(memory_.read(action_banner_enabled) && !memory_.read(action_banner)) {
        memory_.write(deferred_enemy_action,action);
        required(services_.show_banner,0x464494)(action);
        return true; // Deferral does not rewrite the completion latch.
    }
    const auto callback=memory_.read(enemy_callbacks+action*enemy_stride);
    if(!callback){memory_.write(completion_latch,completed);return false;}
    spawn_pending(callback,enemy_failure);return true;
}
bool HandlerLifecycle::queue_status_delta() {
    if(memory_.read(phase_gate)!=pre_action_gate){memory_.write(completion_latch,completed);return false;}
    spawn_pending(delta_callback,delta_failure);return true;
}
std::uint32_t HandlerLifecycle::poll_completion() {
    const bool banners=memory_.read(action_banner_enabled)!=0;
    const auto banner=memory_.read(action_banner),object=memory_.read(active_handler);
    if(banners && !banner && !object) {
        const auto action=memory_.read(deferred_enemy_action);
        if(signed32(action)>0) {
            // Unlike immediate dispatch, this also spawns a null callback.
            spawn_pending(memory_.read(enemy_callbacks+action*enemy_stride),enemy_failure);
            memory_.write(deferred_enemy_action,no_deferred_action);
            return pending;
        }
    }
    if(banner)return pending;
    if(!object || !memory_.read(object+actor_offset::flags)) {
        memory_.write(active_handler,0);memory_.write(completion_latch,completed);
    }
    return memory_.read(completion_latch); // Preserve noncanonical latch values.
}
void HandlerLifecycle::tick_status_delta(Address controller) {
    const auto actor=globals::actor_objects+memory_.read(active_actor_slot)*layout::actor_size;
    const auto phase=static_cast<DeltaPhase>(signed32(memory_.read(controller+actor_offset::callback_state)));
    switch(phase) {
    case DeltaPhase::RestoreActor:
        memory_.write(actor+actor_state_byte,memory_.read(actor+actor_state_byte,1)|actor_state_bit,1);
        [[fallthrough]];
    case DeltaPhase::Initialize: memory_.write(controller+number_wait_count,0,2);return;
    case DeltaPhase::WaitReady:
        if(memory_.read(controller+actor_offset::callback_tick_count)==ready_tick)
            memory_.write(controller+actor_offset::callback_state,std::uint32_t(DeltaPhase::StartReaction));
        return;
    case DeltaPhase::StartReaction: {
        const auto script=memory_.read(reaction_scripts+memory_.read(actor+actor_offset::facing)*4u);
        required(services_.start_script,0x447841)(actor,script);
        memory_.write(actor+actor_state_byte,memory_.read(actor+actor_state_byte,1)&~actor_state_bit,1);
        required(services_.show_number,0x4646be)(actor,signed32(memory_.read(active_delta)));
        // A nested effect may have changed this phase; add to its current value.
        memory_.write(controller+actor_offset::callback_state,memory_.read(controller+actor_offset::callback_state)+phase_increment);
        memory_.write(controller+number_wait_count,1,2);return;
    }
    case DeltaPhase::WaitNumber:
        if(memory_.read(controller+number_wait_count,2))return;
        {
            const auto tick=memory_.read(controller+actor_offset::callback_tick_count);
            memory_.write(controller+actor_offset::callback_state,std::uint32_t(DeltaPhase::FinalDelay));
            memory_.write(controller+final_tick,tick+finish_delay);
        }
        return;
    case DeltaPhase::FinalDelay:
        if(memory_.read(controller+actor_offset::callback_tick_count)!=memory_.read(controller+final_tick))return;
        memory_.write(active_handler,0);
        required(services_.release,0x45d91d)(controller);return;
    default:return;
    }
}
} // namespace fsb::core::combat
