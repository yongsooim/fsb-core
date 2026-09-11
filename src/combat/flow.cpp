#include "fsb_core/combat/flow.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/combat/grid.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::combat {
namespace {
using namespace flow;
} // namespace

void Flow::restore_after_handler() {
    memory_.scene_state().game_mode=field_game_mode;
    memory_.write(globals::active_party_index,memory_.read(battle_leader_slot));
    const auto count=signed32(memory_.read(globals::party_count));
    for(std::int32_t i=0;i<count;++i) {
        const auto id=memory_.read(globals::party_actor_ids+std::uint32_t(i)*4);
        const auto actor=Actors::slot(std::uint32_t(i)),record=BattleRules::party_record(id);
        const auto flags=memory_.read(actor+actor_offset::flags);
        const auto hp=signed32(memory_.read(record + party_field::hit_points));
        memory_.write(actor+actor_offset::flags,flags&~battle_overlay_bit);
        if(hp<minimum_vitality)memory_.write(record + party_field::hit_points,std::uint32_t(minimum_vitality));
        if(signed32(memory_.read(record + party_field::turn_gauge))>=gauge_cap)memory_.write(record + party_field::turn_gauge,std::uint32_t(gauge_left));
        const auto base_status=memory_.read(record + party_field::base_status);
        memory_.write(actor+actor_offset::flags,(flags&~battle_overlay_bit)|actor_visible_bit);
        memory_.write(actor+actor_offset::callback,default_visual_callback);
        memory_.write(record + party_field::battle_status,base_status);
    }
    if(!restore_field_view)throw Fault(0x45d84b,"field actor reset service is not connected");
    restore_field_view(field_reset_first_slot,field_reset_end_slot);
}

bool Flow::fade_idle() const { return memory_.read(fade_state) == 0; }

bool Flow::finish_pending_steps() {
    const auto waiting = signed32(memory_.read(pending_count));
    for (std::int32_t i = 0; i < waiting; ++i) {
        const auto entry = pending_actors + std::uint32_t(i) * 4;
        const auto actor = memory_.read(entry);
        if (!actor) continue;
        const auto flags = memory_.read(actor + actor_offset::flags);
        if (flags & busy_flag) continue; // Still running; leave it queued.
        if (flags & 0x100) {
            memory_.write(actor + actor_offset::flags, flags | resume_flag);
            memory_.write(actor + actor_offset::callback, party_idle_callback);
            memory_.write(outstanding, memory_.read(outstanding) - 1);
        } else if (flags & 0x400) {
            memory_.write(actor + actor_offset::flags, flags | resume_flag);
            memory_.write(actor + actor_offset::callback, enemy_cleanup_callback);
            memory_.write(actor + actor_offset::callback_tick_count, 0);
            memory_.write(actor + actor_offset::callback_state, 0);
        } else continue; // Neither side owns it; keep the queue entry.
        memory_.write(entry, 0);
    }
    return memory_.read(outstanding) == 0;
}

void Flow::trigger_scripted(unsigned group) {
    memory_.write(scripted_pending, 1, 1);
    if (memory_.read(globals::current_event_id) == random_encounter_event) {
        memory_.write(scripted_phase, 0);
        memory_.write(scripted_group, (crt_rand(memory_) & 1) + group * 2);
        return;
    }
    memory_.write(scripted_group, 1);
    memory_.write(scripted_phase, 5);
    // 30 ticks out, battle track39, 30 ticks in, full volume.
    if (transition_music) transition_music(30, 0, 39, 0, 30, 100);
    memory_.write(scripted_music_started, 1, 1);
}

void Flow::end_player_turn() {
    const auto actor = Actors::slot(memory_.read(entrance_actor_slot));
    if (apply_difficulty) apply_difficulty(memory_.read(entrance_difficulty));
    memory_.write(entrance_marker, 0, 1);
    memory_.write(globals::party_actor_ids + memory_.read(actor) * 4, retired_party_slot);
}

void Flow::release_target_tables() {
    for (unsigned i = 0; i < grid::target_table_words; ++i) {
        const auto block = memory_.read(grid::target_tables[0] + i * 4);
        memory_.write(grid::target_tables[2] + i * 4, 0);
        memory_.write(grid::target_tables[1] + i * 4, 0);
        if (block && release_block) release_block(block);
        memory_.write(grid::target_tables[0] + i * 4, 0);
    }
}

void Flow::leave_battle() {
    const auto map = map_records + memory_.read(globals::current_map_id) * map_record_stride;
    if (memory_.read(map) == 1) { if (release_viewport) release_viewport(true); }
    else if (memory_.read(viewport_ready) && release_viewport) release_viewport(false);
    const auto leader_slot = memory_.read(battle_leader_slot);
    memory_.write(globals::game_mode, field_game_mode);
    memory_.write(globals::active_party_index, leader_slot);
    const auto slots = signed32(memory_.read(globals::party_count));
    for (std::int32_t i = 0; i < slots; ++i) {
        const auto actor = Actors::slot(unsigned(i));
        const auto record = BattleRules::party_record(memory_.read(globals::party_actor_ids + std::uint32_t(i) * 4));
        // Drop the battle-only appearance bits from the actor's flag byte.
        memory_.write(actor + actor_offset::flags, memory_.read(actor + actor_offset::flags, 1) & 0x3fu, 1);
        if (signed32(memory_.read(record + party_field::hit_points)) < minimum_vitality)
            memory_.write(record + party_field::hit_points, std::uint32_t(minimum_vitality));
        if (signed32(memory_.read(record + party_field::turn_gauge)) >= gauge_cap)
            memory_.write(record + party_field::turn_gauge, std::uint32_t(gauge_left));
        // The battle status word is replaced by the field one kept beside it.
        memory_.write(record + party_field::battle_status, memory_.read(record + party_field::base_status));
        if (restore_field_actor)
            restore_field_actor(actor, Actors::slot(memory_.read(globals::active_party_index)));
    }
    if (restore_field_view) restore_field_view(field_reset_first_slot, field_reset_end_slot);
    const auto leader = Actors::slot(memory_.read(globals::active_party_index));
    memory_.write(leader + actor_offset::flags, memory_.read(leader + actor_offset::flags) | field_resume_flags);
    memory_.write(leader + actor_offset::callback, field_driver_callback);
}
void Flow::frame_view(std::int32_t x, std::int32_t y, bool hold_focus) {
    // Quarter the doubled viewport, then halve that quotient with the original's
    // round-toward-zero shift. Two truncating steps are not one divide by four.
    const auto half = [&](Address size) {
        const auto quarter = std::int32_t(memory_.read(size) * 2u) / 4;
        return (quarter - (quarter >> 31)) >> 1;
    };
    const auto wide = half(view_width), tall = half(view_height);
    auto left = x - wide, right = x + wide, top = y - tall, bottom = y + tall;
    std::int32_t slide_x = 0, slide_y = 0;
    if (left < signed32(memory_.read(bound_left))) slide_x = signed32(memory_.read(bound_left)) - left;
    if (top < signed32(memory_.read(bound_top))) slide_y = signed32(memory_.read(bound_top)) - top;
    if (right > signed32(memory_.read(bound_right))) slide_x += signed32(memory_.read(bound_right)) - right;
    if (bottom > signed32(memory_.read(bound_bottom))) slide_y += signed32(memory_.read(bound_bottom)) - bottom;
    left += slide_x; right += slide_x; top += slide_y; bottom += slide_y;
    if (zoom_to) zoom_to(hold_focus, left, top, right, bottom, view_transition_ticks);
}
void Flow::track_anchor(Address effect) {
    const auto state = signed32(memory_.read(effect + actor_offset::callback_state));
    const auto anchor = memory_.read(effect + anchor_owner);
    if (state == -1) return;
    if (state == 0) {
        if (start_effect_script) start_effect_script(effect, anchor_effect_script);
        // The original adds to the field, not to the value it read first.
        memory_.write(effect + actor_offset::callback_state,
                      memory_.read(effect + actor_offset::callback_state) + std::uint32_t(anchor_started));
    } else if (state != anchor_started) return;
    memory_.write(effect + actor_offset::layer_q16, memory_.read(anchor + actor_offset::layer_q16));
    memory_.write(effect + actor_offset::world_x, memory_.read(anchor + actor_offset::world_x) - anchor_depth_bias);
    memory_.write(effect + actor_offset::world_y, memory_.read(anchor + actor_offset::world_y));
    memory_.write(effect + actor_offset::elevation, memory_.read(anchor + actor_offset::elevation) + anchor_height_bias);
}
} // namespace fsb::core::combat
