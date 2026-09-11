#include "fsb_core/combat/mask_scan.hpp"
// Temporary ABI boundary for original callers of the reconstructed combat
// entries. Argument reading and return-stack cleanup live only here; nothing
// below this file uses guest registers, the guest stack or condition flags.
// Return widths follow what the original call sites actually read.
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/combat/status.hpp"
#include "fsb_core/combat/pose.hpp"
#include "fsb_core/combat/ai.hpp"
#include "fsb_core/combat/grid.hpp"
#include "fsb_core/combat/flow.hpp"
#include "fsb_core/combat/object_motion.hpp"
#include "fsb_core/combat/effects.hpp"
#include "fsb_core/combat/actions.hpp"

namespace fsb::core {
bool RecoveredBattle::dispatch_b_combat(Address entry) {
    if(entry<0x447e31u||entry>0x467111u)return false;
    BattleRules rules(memory_);
    combat::Status status(memory_);
    // Keep the original's own call to 44de15, which is a declared opt-in.
    status.award_through_boundary=[this](unsigned party_id,std::int32_t amount){
        return (callback(0x44de15,{party_id,std::uint32_t(amount)})&0xff)!=0;};
    combat::Ai ai(memory_);
    combat::Grid grid(memory_);
    combat::Flow flow(memory_);
    combat::Effects effects(memory_);
    combat::Actions battle_actions(memory_);
    battle_actions.start_script=[this](Address actor,Address script){callback(0x447841,{actor,script});};
    battle_actions.release_object=[this](Address object){callback(0x45d91d,{object});};
    battle_actions.screen_flash=[this](bool fade_in,unsigned ticks){
        return callback(0x464c59,{fade_in?1u:0u,ticks})!=0;};
    battle_actions.spawn_beam=[this](Address anchor){callback(0x4657d7,{anchor});};
    battle_actions.spawn_orbit=[this](Address anchor){callback(0x467040,{anchor});};
    battle_actions.spawn_swirl=[this](Address anchor){callback(0x466df3,{anchor});};
    battle_actions.show_amount=[this](Address actor,std::int32_t amount){
        callback(0x4647ad,{actor,std::uint32_t(amount)});};
    battle_actions.stop_motion=[this](Address actor){callback(0x45db6c,{actor});};
    battle_actions.play_cue=[this](unsigned cue){callback(0x435373,{cue});};
    battle_actions.stop_cue=[this](unsigned cue){callback(0x4353cb,{cue});};
    battle_actions.spawn_effect=[this](std::int32_t x,std::int32_t y,std::int32_t z,
                                       std::uint32_t layer,Address script){
        callback(0x464c27,{std::uint32_t(x),std::uint32_t(y),std::uint32_t(z),layer,script});};
    // The object pool belongs to A_ACTORS and the interpreter is already native;
    // both stay original calls through this boundary.
    effects.take_object=[this](Address tick){return callback(0x45d89c,{tick});};
    effects.release_object=[this](Address object){callback(0x45d91d,{object});};
    effects.start_script=[this](Address object,Address script){callback(0x447841,{object,script});};
    effects.notify_controller=[this](Address object,std::int32_t state){
        callback(0x461d25,{object,std::uint32_t(state)});};
    effects.play_cue=[this](unsigned cue){callback(0x435373,{cue});};
    effects.stop_cue=[this](unsigned cue){callback(0x4353cb,{cue});};
    effects.number_text=[this](std::int32_t amount){
        // This buffer is an ABI frame local, not a game heap allocation.
        const auto caller_stack=r[4];r[4]-=64;const auto scratch=r[4];
        std::string text;
        try{
            callback(0x8594c0,{scratch,0x4a62fc,std::uint32_t(amount)});
            for(unsigned i=0;i<63&&read(scratch+i,1);++i)text.push_back(char(read(scratch+i,1)));
        }catch(...){r[4]=caller_stack;throw;}
        r[4]=caller_stack;
        return text;
    };
    effects.write_number=[this](Address text,std::int32_t amount){
        // 4a62fc is the original's own decimal format string.
        callback(0x8594c0,{text,0x4a62fc,std::uint32_t(amount)});
        return callback(0x4973f0,{text});
    };
    effects.advance_motion=[this](Address object){callback(0x45d208,{object});};
    effects.spawn_debris=[this](Address anchor){return callback(0x465abd,{anchor});};
    effects.spawn_explosion=[this](Address anchor){return callback(0x465f7e,{anchor});};
    effects.place_on_tile=[this](Address object,std::int32_t x,std::int32_t y,std::int32_t z){
        callback(0x45d774,{object,std::uint32_t(x),std::uint32_t(y),std::uint32_t(z)});};
    // Services this session does not own stay original calls behind the bridge.
    flow.start_effect_script=[this](Address actor,Address script){callback(0x447841,{actor,script});};
    flow.release_block=[this](Address block){callback(0x4972b0,{block});};
    flow.transition_music=[this](unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f){
        callback(0x4337f4,{a,b,c,d,e,f});};
    flow.release_viewport=[this](bool transition){callback(transition?0x4320f6:0x43208d,{transition?1u:0u});};
    flow.restore_field_actor=[this](Address actor,Address leader){callback(0x45d9e7,{actor,leader});};
    flow.restore_field_view=[this](unsigned from,unsigned to){callback(0x45d84b,{from,to});};
    flow.apply_difficulty=[&](unsigned step){grid.apply_difficulty(step);};
    flow.zoom_to=[this](unsigned mode,std::int32_t left,std::int32_t top,std::int32_t right,
                        std::int32_t bottom,unsigned duration){
        // Temporary ABI argument storage only. A heap allocation here advances
        // the game's allocator and shifts every later surface handle.
        const auto caller_stack = r[4];
        r[4] -= 16;
        const auto rectangle = r[4];
        const std::int32_t edges[]{left, top, right, bottom};
        try {
            for (unsigned i = 0; i < 4; ++i) write(rectangle + i * 4, std::uint32_t(edges[i]));
            callback(0x401d66, {mode, rectangle, duration, 0});
        } catch (...) { r[4] = caller_stack; throw; }
        r[4] = caller_stack;
    };
    const auto found=[&](std::optional<unsigned> value){return value.value_or(0xffffffffu);};
    switch(entry) {
    case 0x44e085:flow.restore_after_handler();result(0);return true;
    // Battle pose callbacks. Each address is one original appearance routine;
    // the module holds their shared placement and their own descriptors.
    case 0x447e31:case 0x447ea3:case 0x447ef9:case 0x447f4c:case 0x447f9f:case 0x447fef:
    case 0x448042:case 0x448095:case 0x4480e8:case 0x4481a9:case 0x44821f:case 0x44828a:
    case 0x44830f:case 0x4483b5:case 0x44844e:case 0x4484f4:
        if(!combat::Pose(memory_).apply(entry,argument(0)))return false;
        result(0,4);return true;
    // Party and enemy status records: timers, icon bits and equipment flags.
    case 0x44caee:status.clear_turn_flags();result(0);return true;
    case 0x44cb26:result((rules.status(argument(0))&argument(1))?1u:0u,8);return true;
    case 0x44e56d:status.afflict_enemy(argument(0),argument(1));result(0,8);return true;
    case 0x44e689:status.afflict_party(argument(0),argument(1));result(0,8);return true;
    case 0x44e46e:status.refresh_equipment_flags(argument(0));result(0,4);return true;
    case 0x4489d0:result(rules.equipped(argument(0),argument(1)),8);return true;
    // Turn bookkeeping: status decay, action gauges and the next ready actor.
    case 0x44c6eb:rules.decay_status();result(0);return true;
    case 0x44cbd2:rules.advance_gauges();result(0);return true;
    case 0x44cd05:{const auto next=rules.next_context();
        write(argument(0),found(next));result(next.has_value(),4);return true;}
    case 0x44d233:result(rules.precheck());return true;
    // Vitality mirror the action resolver writes through, then commits.
    case 0x44d297:rules.prepare_vitality();result(0);return true;
    case 0x44d2d8:rules.commit_vitality();result(0);return true;
    case 0x44cb7a:result(rules.take_poison_delta(argument(0)),4);return true;
    case 0x44d345:rules.apply_delta(argument(0),argument(1));result(0,8);return true;
    case 0x44de15:result(status.award_experience(argument(0),signed32(argument(1))),8);return true;
    case 0x44e7d3:result(std::uint32_t(combat::Status::capped_amount(signed32(argument(0)))),4);return true;
    // Status panel snapshots; each original entry marks only its own panel.
    case 0x44d389:rules.snapshot_party_panel(argument(0)&1);result(0,4);return true;
    case 0x44d40a:rules.snapshot_enemy_panel(argument(0)&1);result(0,4);return true;
    // Action selection tables.
    case 0x44d09c:result(rules.default_action(argument(0),argument(1)&255),8);return true;
    case 0x44d102:result(rules.default_handler(argument(0),argument(1)&255),8);return true;
    // Hit and damage relation between two combatants.
    case 0x44e16c:result(std::uint32_t(rules.relation(argument(0),argument(1))),8);return true;
    case 0x44e200:result(std::uint32_t(BattleRules::relation_hit_bonus(argument(0))),4);return true;
    case 0x44e231:result(std::uint32_t(BattleRules::relation_damage_percent(argument(0))),4);return true;
    case 0x44e265:result(std::uint32_t(BattleRules::counter_chance(argument(0),argument(1))),8);return true;
    case 0x44e2de:result(std::uint32_t(BattleRules::guard_percent(argument(0),argument(1))),8);return true;
    case 0x44e775:result(std::uint32_t(BattleRules::accuracy_bonus(argument(0),argument(1))),8);return true;
    // Grid queries and the reachable-tile overlay.
    case 0x44ce7b:result(found(status.actor_at_tile(signed32(argument(0)),signed32(argument(1)))),8);return true;
    case 0x44e3e1:result(found(status.marker_facing(argument(0))),4);return true;
    case 0x44cdf3:rules.clear_cursor(signed32(argument(0)),signed32(argument(1)),signed32(argument(2)),argument(3));result(0,16);return true;
    case 0x451ec6:rules.mark_reachable(signed32(argument(0)));result(0,4);return true;
    case 0x44dd1c:rules.compute_rewards();result(0);return true;
    // Enemy turn candidate bookkeeping and scoring.
    case 0x452372:result(std::uint32_t(ai.validate_candidates_before_active()));return true;
    case 0x4523b9:result(std::uint32_t(ai.validate_region_candidates(argument(0))),4);return true;
    case 0x45240c:result(ai.best_candidate_in_regions(argument(0)),4);return true;
    case 0x45244d:result(ai.best_candidate_in_region(argument(0)),4);return true;
    case 0x453273:result(ai.best_candidate());return true;
    case 0x452c4d:result(ai.refresh_hurt_ratios(argument(0)),4);return true;
    case 0x452c0c:result(found(ai.priority_skill(argument(0))),4);return true;
    case 0x452a00:ai.begin_action(argument(0),signed32(argument(1)));result(0,8);return true;
    case 0x451e1e:ai.select_region_count(argument(0));result(0,4);return true;
    case 0x454220:ai.clear_line_effects();result(0);return true;
    case 0x45297e: {
        const auto mask = argument(0), pairs = argument(1), count = argument(2);
        const auto found = combat::extract_mask_offsets(
            [&](unsigned index) { return read(mask + index, 1); },
            [&](unsigned index, std::uint32_t value) { write(pairs + index * 4, value); });
        write(count, found); result(found, 12); return true;
    }
    case 0x451e3f: {
        const auto width = signed32(argument(0)), height = signed32(argument(1));
        const auto grid_at = argument(2);
        const auto x = signed32(argument(3)), y = signed32(argument(4));
        const auto count = argument(5), spans = argument(6), bit = argument(7);
        const auto found = combat::extract_mask_spans(width, height, x, y, bit,
            [&](unsigned index) { return read(grid_at + index, 1); },
            [&](unsigned index, std::uint32_t value) { write(spans + index * 4, value); });
        write(count, std::uint32_t(found)); result(std::uint32_t(found), 32); return true;
    }
    // Enemy scoring curves; the original selects one through its AI table.
    case 0x452ce2:result(std::uint32_t(combat::Ai::bias_score(memory_)),20);return true;
    case 0x452cf2:result(std::uint32_t(combat::Ai::move_score(memory_)),20);return true;
    case 0x452e79:result(std::uint32_t(combat::Ai::pressure_scaled(signed32(argument(3)),argument(4))),20);return true;
    case 0x452eb4:result(std::uint32_t(combat::Ai::pressure_flat(signed32(argument(3)),argument(4))),20);return true;
    case 0x452eea:result(std::uint32_t(combat::Ai::pressure_steep(signed32(argument(3)),argument(4))),20);return true;
    // Battle grid bookkeeping, tile reservation and difficulty scaling.
    case 0x448d72:grid.reset_dimensions();result(0,4);return true;
    case 0x449999:grid.mark_party_tiles();result(0);return true;
    case 0x4499cb:grid.clear_target_tables();result(0);return true;
    case 0x449f5b:grid.reset_actor_records();result(0);return true;
    case 0x448aca:result(grid.region_blocked(signed32(argument(0))),4);return true;
    case 0x448c6e:result(grid.reserve_footprint_tile(argument(0),argument(1)),8);return true;
    case 0x448a98:grid.set_encounter_override(argument(0));result(0,4);return true;
    case 0x448ab1:grid.clear_encounter_override(argument(0));result(0,4);return true;
    case 0x448a39:result(found(grid.unlock_skill(argument(0),signed32(argument(1)))),8);return true;
    case 0x448977: {
        const auto first = argument(0), second = argument(1);
        const auto a = read(first), b = read(second);
        if (grid.stat_greater(a, b)) { write(first, b); write(second, a); }
        result(0, 8); return true;
    }
    case 0x44899e:result(grid.stat(argument(0),argument(1),argument(2)),12);return true;
    case 0x44a93e:grid.load_difficulty(argument(0));result(0,4);return true;
    case 0x44aaa1:grid.apply_difficulty(argument(0));result(0,4);return true;
    case 0x44ab05:grid.mark_entrance_actor();result(0);return true;
    case 0x44a8d7:grid.clear_overlay_modal();result(0);return true;
    // Action footprint publication and target collection.
    case 0x45149b:rules.prepare_handler(argument(0),argument(1));result(0,8);return true;
    case 0x4519dd:case 0x451885:{
        const auto masked=entry==0x451885;
        const auto targets=rules.collect_targets(argument(0),argument(3),argument(4),
            masked?std::optional<unsigned>(argument(5)):std::nullopt);
        write(argument(1),unsigned(targets.size()));
        for(std::size_t i=0;i<targets.size();++i)write(argument(2)+unsigned(i)*4,targets[i]);
        result(0,masked?24:20);return true;}
    // Battle progression and anchored effect objects.
    case 0x4622b6:result(flow.fade_idle());return true;
    case 0x461ca2:result(flow.finish_pending_steps());return true;
    case 0x44a8df:flow.trigger_scripted(argument(0));result(0,4);return true;
    case 0x44ab2e:flow.end_player_turn();result(0);return true;
    case 0x4499f0:flow.release_target_tables();result(0);return true;
    case 0x462790:flow.track_anchor(argument(0));result(0,4);return true;
    case 0x448a0a:result(grid.party_owns_item(argument(0)),4);return true;
    case 0x452014:result(ai.collect_tiles(argument(0)!=0),4);return true;
    case 0x4529c2:result(ai.prepare_action_offsets(argument(0)),4);return true;
    case 0x451b57:result(std::uint32_t(ai.party_target_weight(read(argument(0)))),4);return true;
    case 0x451b30:result(std::uint32_t(combat::Ai::tile_distance(signed32(argument(0)),signed32(argument(1)),
        signed32(argument(2)),signed32(argument(3)))),16);return true;
    case 0x4532a5:result(combat::Ai::step_direction(signed32(argument(0)),signed32(argument(1))),8);return true;
    case 0x44e500:result(found(status.roll_status(argument(0),argument(1),signed32(argument(2)))),12);return true;
    case 0x4644d3:result(status.scene_track());return true;
    case 0x452168:result(ai.rasterize_regions(argument(0)),4);return true;
    case 0x452255:result(ai.rasterize_regions(std::nullopt));return true;
    case 0x44de92:status.award_party_experience();result(0);return true;
    case 0x452b60:ai.build_skill_list(argument(0),argument(1));result(0,8);return true;
    case 0x4520a0:ai.register_scan_regions();result(0);return true;
    case 0x451f0d:ai.build_movement_overlay(argument(0));result(0,4);return true;
    case 0x462150:flow.frame_view(signed32(argument(0)),signed32(argument(1)),true);result(0,8);return true;
    case 0x462203:flow.frame_view(signed32(argument(0)),signed32(argument(1)),false);result(0,8);return true;
    case 0x452d02:result(std::uint32_t(ai.crowding_near(signed32(argument(1)),signed32(argument(2)))),20);return true;
    case 0x452db8:result(std::uint32_t(ai.crowding_wide(signed32(argument(1)),signed32(argument(2)))),20);return true;
    case 0x45316e:result(ai.press_weakened_enemies(argument(0)&255),4);return true;
    case 0x44a513: {
        const auto first = argument(0), count = argument(1);
        const auto out_group = argument(2), out_index = argument(3);
        const auto placed = grid.take_random_placement(first, count, [&](unsigned group, unsigned index) {
            // 44a623 passes frame locals; read/write select stack or game state.
            write(out_group, group);
            write(out_index, index);
        });
        result(placed, 16); return true;
    }
    case 0x452497:result(ai.collect_reaching_candidates(signed32(argument(0)),signed32(argument(1))),8);return true;
    case 0x4525e0:result(ai.collect_region_candidates(signed32(argument(0)),signed32(argument(1)),argument(2)),12);return true;
    case 0x448b4c:result(grid.try_random_encounter());return true;
    case 0x44df90:case 0x44ab62:flow.leave_battle();result(0);return true;
    case 0x462de2: combat::ObjectMotion(memory_).home_toward_target(argument(0));result(0,4);return true;
    // Battle effect objects.
    case 0x464c27:effects.spawn_scripted(signed32(argument(0)),signed32(argument(1)),
        signed32(argument(2)),argument(3),argument(4));result(0,20);return true;
    case 0x464cf4:effects.spawn_notifying(signed32(argument(0)),signed32(argument(1)),
        signed32(argument(2)),argument(3),argument(4),signed32(argument(5)));result(0,24);return true;
    case 0x464c05:effects.retire_when_finished(argument(0));result(0,4);return true;
    case 0x464d2f:effects.notify_and_retire(argument(0));result(0,4);return true;
    case 0x46521e:effects.rise_then_retire(argument(0));result(0,4);return true;
    case 0x465b66:effects.attach_to(0x465b14,argument(0));result(0,4);return true;
    case 0x4665ca:effects.attach_to(0x466545,argument(0));result(0,4);return true;
    case 0x466a98:effects.attach_to(0x466992,argument(0));result(0,4);return true;
    case 0x4660a3:effects.attach_to_drifting(0x466015,argument(0),4);result(0,4);return true;
    case 0x465241:effects.spawn_hit_spark(argument(0));result(0,4);return true;
    case 0x4653d9:effects.spawn_phase_finish(argument(0));result(0,4);return true;
    case 0x4653a5:effects.notify_anchor_and_retire(argument(0));result(0,4);return true;
    case 0x46686d:effects.retire_when_gate_open(argument(0));result(0,4);return true;
    case 0x466cc9:effects.shrink_then_retire(argument(0));result(0,4);return true;
    case 0x465dc2:effects.flicker_then_retire(argument(0));result(0,4);return true;
    case 0x46265d:effects.sway_then_retire(argument(0));result(0,4);return true;
    case 0x465b14:effects.emit_debris(argument(0));result(0,4);return true;
    // The caller keeps the new particle, so this return is used.
    case 0x465abd:result(effects.spawn_debris_particle(argument(0)),4);return true;
    case 0x46532a:effects.spawn_falling_strikes(argument(0));result(0,4);return true;
    case 0x46528d:effects.fall_and_strike(argument(0));result(0,4);return true;
    case 0x466015:effects.sequence_explosions(argument(0));result(0,4);return true;
    // 465f7e hands the new burst back to its caller.
    case 0x465f7e:result(effects.spawn_explosion_burst(argument(0)),4);return true;
    case 0x46688c: effects.spawn_ground_burst(argument(0));result(0,4);return true;
    case 0x46691c: effects.spawn_large_ground_burst(argument(0));result(0,4);return true;
    case 0x466cfc: effects.spawn_swirl_particle(argument(0));result(0,4);return true;
    // 46648b hands the new particle back to its caller.
    case 0x46648b:result(effects.spawn_fountain_particle(argument(0),signed32(argument(1))),8);return true;
    case 0x465e0b: effects.spawn_spark_cluster(argument(0));result(0,4);return true;
    case 0x466df3: effects.spawn_swirl_cluster(argument(0));result(0,4);return true;
    case 0x465ee1: effects.grow_explosion(argument(0));result(0,4);return true;
    case 0x466da7: effects.drift_swirl_particle(argument(0));result(0,4);return true;
    case 0x466545: effects.emit_fountain(argument(0));result(0,4);return true;
    case 0x466992: effects.sequence_ground_bursts(argument(0));result(0,4);return true;
    case 0x46596b: effects.fly_debris(argument(0));result(0,4);return true;
    case 0x466309: effects.arc_fountain_particle(argument(0));result(0,4);return true;
    case 0x467040: effects.spawn_orbit_cluster(argument(0));result(0,4);return true;
    case 0x466ed1: effects.orbit_spark(argument(0));result(0,4);return true;
    case 0x4657d7: effects.spawn_beam(argument(0));result(0,4);return true;
    case 0x46562e: effects.tick_beam_charge(argument(0));result(0,4);return true;
    case 0x46570a: effects.tick_beam_core(argument(0));result(0,4);return true;
    case 0x4645e7: effects.step_floating_number(argument(0),combat::effects::digit_glyph_bias);result(0,4);return true;
    case 0x464a9d: effects.step_floating_number(argument(0),combat::effects::alt_glyph_bias);result(0,4);return true;
    case 0x4646be: effects.spawn_floating_number(argument(0),signed32(argument(1)),false);result(0,8);return true;
    case 0x464b74: effects.spawn_floating_number(argument(0),signed32(argument(1)),true);result(0,8);return true;
    case 0x464512: effects.tick_floating_digit(argument(0));result(0,4);return true;
    case 0x46474d: effects.tick_floating_digit_alt(argument(0));result(0,4);return true;
    case 0x4647ad: effects.spawn_glyph_number(argument(0),signed32(argument(1)),
        combat::effects::glyph_set_bias);result(0,8);return true;
    case 0x464925: effects.spawn_glyph_number(argument(0),signed32(argument(1)),
        combat::effects::alt_glyph_set_bias);result(0,8);return true;
    case 0x4658de: battle_actions.run_beam(argument(0),argument(1));result(0,8);return true;
    case 0x465b7d:case 0x4660c4:{
        // These two differ only in the effect each target opens with.
        const auto opener=entry==0x465b7d?0x465b66u:0x4660a3u;
        battle_actions.run_target_effects(argument(0),argument(1),{},
            [&](Address target){callback(opener,{target});});
        result(0,8);return true;}
    case 0x4665e1:{
        // The fountain holds a cue for its whole run, counts the particles that
        // land and opens its gate when the last one does.
        combat::Actions::TargetAction shape;
        shape.effect_done_state=combat::actions::landed_state;
        shape.landed_counter=combat::actions::landed;shape.landed_gate=0x806510;
        shape.held_cue=0xb6;shape.release_cue_state=-2;shape.reset_cue=0xb5;
        shape.counts_reports=false;
        shape.clear_on_reset=0x806510;shape.reset_bytes=0x42;
        battle_actions.run_target_effects(argument(0),argument(1),shape,
            [&](Address target){callback(0x4665ca,{target});});
        result(0,8);return true;}
    case 0x466aaf:{
        // The ground burst plays one script for every facing, counts no
        // reports and clears its own gate when it restarts.
        combat::Actions::TargetAction shape;
        shape.target_script_table=0x5d2b1c;shape.selector_table=0x5bf49c;
        shape.per_facing=false;shape.counts_reports=false;
        shape.clear_on_reset=0x806514;shape.reset_bytes=0x42;
        battle_actions.run_target_effects(argument(0),argument(1),shape,
            [&](Address target){callback(0x466a98,{target});});
        result(0,8);return true;}
    case 0x467111: battle_actions.run_orbit(argument(0),argument(1));result(0,8);return true;
    default:return false;
    }
}
} // namespace fsb::core
