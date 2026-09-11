#include "fsb_core/combat/engine.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::combat {
using namespace engine;
namespace {
// The original indexed table uses wrapping32-bit arithmetic, including slot-1.
// Keep this compatibility lookup here until actor references become handles.
Address actor_record(unsigned slot) { return globals::actor_objects+slot*layout::actor_size; }
}
void Engine::set_phase(Phase value) { memory_.write(phase,unsigned(value)); }
void Engine::snapshot_panels(bool active) {
    services_.snapshot_party(active);
    services_.snapshot_enemy(active);
}
void Engine::clear_active_overlay() {
    const auto at=actor_record(memory_.read(turn::active_slot))+actor_offset::flags;
    memory_.write(at,memory_.read(at,1)&~turn::movement_overlay_bit,1);
}
void Engine::refresh_focus() {
    const auto actor=actor_record(memory_.read(focus_slot));
    memory_.write(focus_x,memory_.read(actor+actor_offset::world_x));
    memory_.write(focus_y,memory_.read(actor+actor_offset::world_y));
}
void Engine::precheck() {
    switch(services_.precheck()) {
    case 0:break;
    case 1:memory_.write(results_phase,0);memory_.write(frame_submode,results_submode);return;
    case 2:memory_.write(results_phase,0);memory_.write(frame_submode,results_submode);
        memory_.write(title_flow,game_over_screen);return;
    default:return;
    }
    if(!services_.select_next()) {
        memory_.write(mode,unsigned(Mode::Idle));memory_.write(idle_ticks,idle_delay);return;
    }
    const auto active=memory_.read(turn::active_slot),focus=memory_.read(focus_slot);
    if(active==focus)set_phase(Phase::PrepareContext);
    else {
        set_phase(Phase::WaitFocus);
        services_.move_focus(focus,active,focus_duration,true);
    }
}
void Engine::prepare_context() {
    services_.prepare_vitality();
    if(services_.has_status(memory_.read(turn::active_slot),battle_status::poison)) {
        const auto slot=memory_.read(turn::active_slot);
        set_phase(Phase::WaitStatus);memory_.write(phase_gate,pre_action_gate);
        const auto delta=services_.poison_delta(slot);
        memory_.write(followup::active_amount,delta);
        services_.apply_delta(memory_.read(turn::active_slot),0u-delta);
        services_.queue_status();memory_.write(followup::target_count,0);
    } else {
        services_.activate_context();set_phase(Phase::Command);
    }
    memory_.write(turn::selected_handler,no_handler);
    memory_.write(turn::command_ticks,0);memory_.write(field_phase,0);
}
void Engine::menu_command(Address actor) {
    const auto packed=services_.menu_result();
    const auto command=MenuCommand(packed&0xffffu);
    const auto selection=packed>>16;
    memory_.write(cursor_flags,0);
    switch(command) {
    case MenuCommand::Cancel:
        services_.clear_command();memory_.write(turn::command_phase,0);return;
    case MenuCommand::Skill:
    case MenuCommand::Item: {
        memory_.write(turn::selected_action,selection);
        const auto flag_byte=actor+actor_offset::flags+1;
        memory_.write(flag_byte,memory_.read(flag_byte,1)|turn::targeting_byte_bit,1);
        memory_.write(actor+actor_offset::callback,turn::targeting_callback);
        memory_.write(cursor_flags,cursor_tracking);
        if(command==MenuCommand::Skill) {
            memory_.write(turn::action_mode,2);
            // Read the published ID, as the original does, before table lookup.
            memory_.write(turn::selected_handler,memory_.read(skill_handlers+memory_.read(turn::selected_action)*skill_handler_stride));
        } else {
            unsigned handler=item_handler;
            if(selection==escape_item)handler=escape_handler;
            else if(selection==no_target_item_a||selection==no_target_item_b||selection==no_target_item_c)handler=no_target_handler;
            memory_.write(turn::selected_handler,handler);memory_.write(turn::action_mode,3);
        }
        services_.prepare_handler(memory_.read(turn::selected_handler),memory_.read(actor+actor_offset::facing));
        services_.clear_command();services_.invalidate_panel();memory_.write(turn::command_phase,0);return;
    }
    case MenuCommand::Finish:
        services_.clear_command();services_.invalidate_panel();memory_.write(turn::command_phase,0);
        set_phase(Phase::Cleanup);return;
    default:return;
    }
}
void Engine::enemy_command(Address actor,std::uint32_t x,std::uint32_t y) {
    std::optional<unsigned> marker;
    const auto record=memory_.read(actor+turn::enemy_record_index);
    if(!(memory_.read(BattleRules::enemy_record(record)+turn::enemy_status+3,1)&enemy_auto_marker_block)) {
        for(unsigned i=0;i<marker_count;++i) {
            const auto object=memory_.read(marker_objects+i*4);
            if(object&&memory_.read(object+actor_offset::tile_x)==x&&memory_.read(object+actor_offset::tile_y)==y) {
                marker=i;break;
            }
        }
    }
    if(!marker) {
        const auto action=memory_.read(followup::enemy_action);
        if(signed32(action)<0)set_phase(Phase::Cleanup);
        else {
            const auto handler=memory_.read(enemy_handlers+action*enemy_handler_stride);
            memory_.write(cursor_flags,cursor_tracking);memory_.write(turn::selected_handler,handler);
            services_.track_action(x,y,true);
            if(memory_.read(turn::command_ticks)==ai_confirm_tick) {
                set_phase(Phase::StartAction);services_.select_ai_followup();
            }
        }
        return;
    }
    set_phase(Phase::StartAction);memory_.write(cursor_flags,0);
    memory_.write(followup::enemy_action,marker_action);memory_.write(phase_gate,post_action_gate);
    memory_.write(followup::target_count,1);memory_.write(followup::result_rows+followup::flags_offset,0);
    memory_.write(followup::result_rows,memory_.read(actor));
    memory_.write(followup::result_rows+followup::amount_offset,marker_damage);
    memory_.write(followup::result_rows+followup::auxiliary_offset,*marker);
    const auto vitality=enemy_vitality+memory_.read(actor+turn::enemy_record_index)*4;
    memory_.write(vitality,memory_.read(vitality)-marker_damage);
    if(signed32(memory_.read(enemy_vitality+memory_.read(actor+turn::enemy_record_index)*4))<1) {
        memory_.write(followup::result_rows+followup::flags_offset,marker_lethal);
        const auto status=BattleRules::enemy_record(memory_.read(actor+turn::enemy_record_index))+turn::enemy_status+2;
        memory_.write(status,memory_.read(status,1)|enemy_down_byte,1);
        memory_.write(BattleRules::enemy_record(memory_.read(actor+turn::enemy_record_index))+enemy_gauge,0);
    }
}
void Engine::command() {
    const auto actor=actor_record(memory_.read(turn::active_slot));
    const auto flags=memory_.read(actor+actor_offset::flags);
    const auto x=memory_.read(actor+actor_offset::tile_x),y=memory_.read(actor+actor_offset::tile_y);
    if(!(flags&turn::party_flag)) {
        if(memory_.read(actor+actor_command_wait)==1) {
            memory_.write(turn::command_ticks,0);memory_.write(cursor_flags,cursor_active);return;
        }
        enemy_command(actor,x,y);
    } else {
        if(!(flags&turn::movement_overlay_bit)) {
            memory_.write(cursor_flags,memory_.read(cursor_flags)&~cursor_active);return;
        }
        const auto result_phase=memory_.read(turn::command_phase);
        if(result_phase) { if(result_phase==1)menu_command(actor);return; }
        if(flags&(turn::targeting_byte_bit<<8)) {
            memory_.write(cursor_flags,0);services_.clear_cursor(x,y,cursor_radius,1);
            if(!memory_.read(actor+actor_offset::motion_state)) {
                memory_.write(cursor_flags,cursor_tracking);services_.track_action(x,y,true);
            }
            return;
        }
        memory_.write(cursor_flags,memory_.read(cursor_flags)|cursor_active);
        services_.clear_cursor(x,y,cursor_radius,1);
        if(memory_.read(actor+actor_offset::motion_state)) { memory_.write(turn::command_ticks,0);return; }
        const auto ticks=signed32(memory_.read(turn::command_ticks));
        memory_.write(turn::action_mode,0);
        if(ticks>turn::input_delay) {
            memory_.write(cursor_flags,memory_.read(cursor_flags)|cursor_effect);
            const auto handler=services_.default_handler(memory_.read(turn::active_slot),false);
            memory_.write(turn::selected_handler,handler);
            services_.prepare_handler(handler,memory_.read(actor+actor_offset::facing));
            services_.track_action(x,y,false);
        }
    }
    memory_.write(turn::command_ticks,memory_.read(turn::command_ticks)+1);
}
void Engine::begin_action(bool counter) {
    if(!counter) {
        memory_.write(frame_parity,memory_.read(frame_parity,1)^1,1);
        clear_active_overlay();memory_.write(cursor_flags,0);
        const bool has_targets=memory_.read(followup::target_count)!=0;
        refresh_focus();
        if(!has_targets) { set_phase(Phase::Cleanup);return; }
    }
    services_.prepare_followup(counter?followup::Stage::Counter:followup::Stage::Snapshot);
    if(!counter)services_.invalidate_panel();
    if(signed32(memory_.read(turn::active_slot))<int(followup::party_slot_limit)) {
        const auto action_mode=memory_.read(turn::action_mode);
        if(counter||action_mode)services_.start_player(action_mode,memory_.read(turn::selected_action));
    } else {
        const auto action=memory_.read(followup::enemy_action);
        if(counter||action!=no_handler)services_.start_enemy(action);
    }
    set_phase(counter?Phase::WaitCounter:Phase::WaitAction);
    poll_action(counter); // Original starts polling in the same tick.
}
void Engine::poll_action(bool counter) {
    if(!services_.poll_handler())return;
    services_.prepare_followup(counter?followup::Stage::RestoreCounter:followup::Stage::Restore);
    services_.enqueue_wave(counter?1:0);
    set_phase(counter?Phase::FinishCounter:Phase::FinishAction);
}
void Engine::finish_action(bool counter) {
    if(!services_.finish_followup())return;
    if(!counter&&signed32(memory_.read(followup::selected_row))>=0) { set_phase(Phase::StartCounter);return; }
    memory_.write(frame_parity,memory_.read(frame_parity,1)^1,1);refresh_focus();set_phase(Phase::Cleanup);
}
void Engine::post_action() {
    const auto ready=memory_.read(actor_ready,1),slot=memory_.read(entrance_slot);
    if(!ready&&signed32(slot)>=0&&memory_.read(turn::active_slot)==slot&&
       signed32(memory_.read(globals::party_actor_ids+slot*4))>retired_party_id) {
        set_phase(Phase::FlashRetirement);memory_.write(flash_ticks,0);return;
    }
    if(!ready&&signed32(slot)>=0&&memory_.read(extra_trigger,1)) {
        set_phase(Phase::FlashEntrance);memory_.write(flash_ticks,0);memory_.write(extra_trigger,0,1);return;
    }
    set_phase(Phase::Precheck);
}
void Engine::flash_entrance(bool retiring) {
    const auto tick=memory_.read(flash_ticks);
    const bool pattern_set=((1u<<(tick&31))&flash_pattern)!=0;
    const bool use_palette=pattern_set!=retiring;
    const auto sprite=use_palette?memory_.read(palette_sprites+memory_.read(palette_index)*palette_stride):memory_.read(normal_sprite);
    memory_.write(actor_record(memory_.read(entrance_slot))+actor_offset::sprite_base,sprite);
    const auto next=tick+1;memory_.write(flash_ticks,next);
    if(signed32(next)<=last_flash_tick)return;
    if(retiring)services_.retire_entrance();else services_.mark_entrance();
    set_phase(Phase::Precheck);services_.snapshot_party(false);
}
void Engine::tick() {
    if(memory_.read(force_cleanup,1))set_phase(Phase::Cleanup);
    switch(Mode(memory_.read(mode))) {
    case Mode::Idle: {
        const auto left=signed32(memory_.read(idle_ticks));
        if(left>0)memory_.write(idle_ticks,std::uint32_t(left)-1);
        else if(!left)memory_.write(mode,unsigned(Mode::Initialize));
        return;
    }
    case Mode::Initialize:
        services_.decay_status();services_.advance_gauges();snapshot_panels(false);
        memory_.write(generation,memory_.read(generation)+1);memory_.write(mode,unsigned(Mode::Active));
        set_phase(Phase::Precheck);memory_.write(extra_trigger,0,1);return;
    case Mode::Active:break;
    default:return;
    }
    switch(Phase(memory_.read(phase))) {
    case Phase::Precheck:precheck();break;
    case Phase::PrepareContext:prepare_context();break;
    case Phase::WaitFocus:if(!memory_.read(focus_pending,1))set_phase(Phase::PrepareContext);break;
    case Phase::WaitStatus:
        if(services_.poll_handler()) {
            services_.commit_vitality();services_.prepare_vitality();snapshot_panels(true);
            services_.activate_context();set_phase(Phase::Command);
        }
        break;
    case Phase::Command:command();break;
    case Phase::StartAction:begin_action(false);break;
    case Phase::WaitAction:poll_action(false);break;
    case Phase::FinishAction:finish_action(false);break;
    case Phase::StartCounter:begin_action(true);break;
    case Phase::WaitCounter:poll_action(true);break;
    case Phase::FinishCounter:finish_action(true);break;
    case Phase::Cleanup:
        snapshot_panels(true);memory_.write(cursor_flags,0);services_.commit_vitality();
        set_phase(Phase::PostAction);clear_active_overlay();services_.apply_status_icons();break;
    case Phase::PostAction:post_action();break;
    case Phase::FlashEntrance:flash_entrance(false);break;
    case Phase::FlashRetirement:flash_entrance(true);break;
    }
}
}
