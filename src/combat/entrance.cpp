#include "fsb_core/combat/entrance.hpp"
#include "fsb_core/combat/engine.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/symbols.hpp"
namespace fsb::core::combat {
using namespace entrance;
namespace {
Address actor_record(unsigned index){return globals::actor_objects+index*layout::actor_size;}
}
std::uint32_t Entrance::activate_enemy(unsigned index,bool refresh) {
    const auto record=BattleRules::enemy_record(index),actor=actor_record(memory_.read(record));
    const auto flags=(memory_.read(actor+actor_offset::flags)&~dive_flag)|enemy_flags;
    memory_.write(actor+actor_offset::flags,flags);
    if(memory_.read(record+enemy_kind)==special_monster)
        memory_.write(record+enemy_status+2,memory_.read(record+enemy_status+2,1)|engine::enemy_down_byte,1);
    else memory_.write(actor+actor_offset::flags,flags|turn::visible_bit);
    memory_.write(actor+actor_offset::callback,default_visual);
    memory_.write(actor+actor_offset::path_cursor,0);memory_.write(actor+actor_offset::path_count,0);
    memory_.write(actor+command_busy,0);memory_.write(actor+actor_offset::motion_state,0);
    if(refresh) {
        const auto definition=BattleRules::monster_record(memory_.read(record+enemy_kind));
        memory_.write(record+enemy_status,memory_.read(definition+definition_status));
        services_.clear_timer(record+enemy_timers);
        memory_.write(engine::enemy_vitality+index*4,memory_.read(definition+definition_hp));
        memory_.write(record+enemy_resource,memory_.read(definition+definition_mp));
        const auto random=signed32(services_.random());
        memory_.write(record+enemy_gauge,std::uint32_t(random%std::int32_t(gauge_modulus)));
        return std::uint32_t(random/std::int32_t(gauge_modulus));
    }
    const auto stage=enemy_staging+index*enemy_stage_stride;
    services_.place_actor(actor,memory_.read(stage),memory_.read(stage+4),memory_.read(stage+8));
    memory_.write(actor+actor_offset::facing,memory_.read(stage+12));
    memory_.write(actor+actor_offset::tile_x,memory_.read(stage));
    const auto y=memory_.read(stage+4);memory_.write(actor+actor_offset::tile_y,y);return y;
}
void Entrance::arm_enemy_focus() {
    const auto target=memory_.read(BattleRules::enemy_record(0));
    memory_.write(phase,unsigned(Phase::BeginEnemies));memory_.write(enemy_index,0);
    services_.move_focus(memory_.read(engine::focus_slot),target,focus_ticks,true);
}
void Entrance::wait_party(std::int32_t count) {
    unsigned busy=0;
    for(std::int32_t i=0;i<count;++i) {
        const auto actor=actor_record(unsigned(i));
        if(memory_.read(actor+command_busy)==1)++busy;
        else {
            memory_.write(actor+command_busy,0);memory_.write(actor+actor_offset::path_count,0);
            memory_.write(actor+actor_offset::callback,field_movement);
        }
    }
    if(!busy)arm_enemy_focus();
}
void Entrance::begin_enemies(bool immediate) {
    if(!immediate){step_enemy();return;}
    for(std::uint32_t i=0;signed32(i)<signed32(memory_.read(enemy_count));++i)services_.activate_enemy(i,false);
    memory_.write(engine::focus_pending,1,1);memory_.write(phase,unsigned(Phase::ReadyFlash));
}
void Entrance::step_enemy() {
    auto index=memory_.read(enemy_index);
    const auto actor=actor_record(memory_.read(BattleRules::enemy_record(index)));
    const auto flags=memory_.read(actor+actor_offset::flags);
    if(flags&dive_flag) {
        memory_.write(actor+actor_offset::flags,(flags&~dive_flag)|ready_flags);
        memory_.write(actor+actor_offset::callback,enemy_dive);memory_.write(actor+enemy_animation_phase,0);
        memory_.write(actor+enemy_motion_phase,dive_phase);memory_.write(actor+actor_offset::elevation,dive_height);return;
    }
    if(memory_.read(actor+enemy_animation_phase)!=dive_done)return;
    for(;;) {
        memory_.write(enemy_index,++index);
        // The special-row read intentionally precedes the enemy-count check.
        if(memory_.read(BattleRules::enemy_record(index)+enemy_kind)!=special_monster)break;
        services_.activate_enemy(index,false);index=memory_.read(enemy_index);
    }
    if(signed32(index)>=signed32(memory_.read(enemy_count))) {
        memory_.write(entrance_marker,0,1);memory_.write(phase,unsigned(Phase::ReadyFlash));
    } else services_.focus(memory_.read(BattleRules::enemy_record(index)),focus_ticks);
}
void Entrance::finish_flash() {
    memory_.write(phase,unsigned(Phase::Handoff));services_.focus(memory_.read(globals::active_party_index),focus_ticks);
}
void Entrance::tick(bool staged_party,bool immediate_enemies) {
    switch(Phase(memory_.read(phase))) {
    case Phase::Initialize:
        if(!memory_.read(music_started,1))services_.transition_music(music_fade,0,services_.select_music(),0,music_fade,music_volume);
        if(staged_party) {
            for(std::uint32_t i=0;signed32(i)<signed32(memory_.read(globals::party_count));++i) {
                const auto stage=party_staging+i*party_stage_stride,actor=actor_record(i);
                const auto grid=memory_.read(stage+8),facing=memory_.read(stage+12);
                memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)|ready_flags);
                memory_.write(actor+actor_offset::facing,facing);memory_.write(actor+command_busy,0);
                services_.place_actor(actor,memory_.read(stage),memory_.read(stage+4),grid);
                memory_.write(actor+actor_offset::tile_x,memory_.read(stage));memory_.write(actor+actor_offset::tile_y,memory_.read(stage+4));
                memory_.write(actor+actor_offset::callback,field_movement);
                memory_.write(actor+actor_offset::path_cost,0);memory_.write(actor+actor_offset::path_count,0);memory_.write(actor+actor_offset::path_cursor,0);
            }
            begin_enemies(immediate_enemies);return;
        } else {
            const auto count=signed32(memory_.read(globals::party_count));
            for(std::int32_t i=0;i<count;++i) {
                const auto actor=actor_record(unsigned(i)),flags=memory_.read(actor+actor_offset::flags);
                memory_.write(actor+command_busy,1);memory_.write(actor+actor_offset::path_cursor,0);
                memory_.write(actor+actor_offset::callback,scripted_steps);memory_.write(actor+actor_offset::flags,(flags&~dive_flag)|ready_flags);
            }
            memory_.write(phase,unsigned(Phase::WaitParty));wait_party(count);return;
        }
    case Phase::WaitParty:wait_party(signed32(memory_.read(globals::party_count)));return;
    case Phase::ArmEnemyFocus:arm_enemy_focus();return;
    case Phase::BeginEnemies:begin_enemies(immediate_enemies);return;
    case Phase::StepEnemy:step_enemy();return;
    case Phase::ReadyFlash:
        if(memory_.read(engine::actor_ready,1)) {
            memory_.write(phase,unsigned(Phase::Flash));services_.focus(memory_.read(engine::entrance_slot),focus_ticks);
            memory_.write(engine::flash_ticks,0);return;
        }
        finish_flash();return;
    case Phase::Flash: {
        const auto tick=memory_.read(engine::flash_ticks);
        const auto sprite=((1u<<(tick&31))&engine::flash_pattern)?memory_.read(engine::palette_sprites+memory_.read(engine::palette_index)*engine::palette_stride):memory_.read(engine::normal_sprite);
        memory_.write(actor_record(memory_.read(engine::entrance_slot))+actor_offset::sprite_base,sprite);
        const auto next=tick+1;memory_.write(engine::flash_ticks,next);
        if(signed32(next)<=engine::last_flash_tick)return;
        services_.mark_entrance();finish_flash();return;
    }
    case Phase::Handoff:
        if(memory_.read(engine::focus_pending,1)==1) {
            memory_.write(engine::frame_submode,1);memory_.write(engine::generation,0);memory_.write(engine::mode,0);
            memory_.write(engine::idle_ticks,engine::idle_delay);memory_.write(turn::active_slot,0);memory_.write(engine::phase,0);
        }
        return;
    }
}
}
