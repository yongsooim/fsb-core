#include "fsb_core/actor_core/map_transition.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
std::uint32_t map_bgm_of(const Memory& memory, std::uint32_t map) {
    return memory.read(map_record_bgm + map * map_record_bytes);
}
// Modes 0..4 hand the screen effect the palette buffer; 5 and 9 hand it the
// active actor's record; everything else gets nothing.
Address screen_effect_context(const Memory& memory, std::int32_t mode) {
    if (mode < 0) return 0;
    if (mode < map_palette_mode_limit) return palette_fade_buffer;
    if (mode == map_actor_mode_a || mode == map_actor_mode_b)
        return globals::actor_objects + memory.read(globals::active_party_index) * layout::actor_size;
    return 0;
}
void finish_to_field(Memory& memory) {
    memory.write(globals::game_mode, game_mode_field);
    memory.write(map_phase, map_phase_idle);
}
} // namespace

std::uint32_t advance_map_transition(Memory& memory, std::uint32_t map, std::int32_t mode,
                                     std::uint32_t flags, const MapTransitionHooks& hooks) {
    if (hooks.close_dialogue) hooks.close_dialogue();
    memory.write(0x802c9c, 0); // No field event is pending across a transition.
    memory.write(0x802ca0, 0);
    const auto context = screen_effect_context(memory, mode);

    // Wait for the screen and the music, then either load or hand the frame to
    // a worldmap route. Answers true only when the caller should go on to load.
    const auto try_finish_fade_out = [&] {
        if (hooks.run_screen_fade && !hooks.run_screen_fade(mode, map_fade_out, map_fade_ms, context))
            return false;
        if (hooks.music_fading && hooks.music_fading()) return false;
        memory.write(map_phase, map_phase_load);
        const auto from = signed32(memory.read(requested_map));
        const auto to = signed32(memory.read(map_transition_target));
        if (!hooks.worldmap_route_exists || !hooks.worldmap_route_exists(from, to)) return true;
        // A worldmap route owns the following frames, so this transition stops
        // polling here either way.
        if (!hooks.route_event || hooks.route_event(from, -1) != 1) {
            memory.write(worldmap_route_from, std::uint32_t(from));
            memory.write(worldmap_route_to, std::uint32_t(to));
            memory.write(globals::game_mode, game_mode_worldmap_event);
            return false;
        }
        finish_to_field(memory);
        return false;
    };

    bool load_now = false;
    switch (memory.read(map_phase)) {
    case map_phase_begin:
        if (hooks.capture_palette) hooks.capture_palette(palette_fade_buffer, 0, map_palette_words);
        if (memory.read(current_bgm) != map_bgm_of(memory, map) && hooks.fade_music)
            hooks.fade_music(map_bgm_fade_steps, 0);
        memory.write(map_phase, map_phase_fading_out);
        memory.write(map_effect_pending, 1);
        // The screen and the music may both already be done this same frame.
        load_now = try_finish_fade_out();
        break;
    case map_phase_fading_out:
        load_now = try_finish_fade_out();
        break;
    case map_phase_load:
        load_now = true;
        break;
    case map_phase_arm_fade_in: {
        if (flags) {
            const auto next = map_bgm_of(memory, map);
            if (memory.read(current_bgm) != next) {
                memory.write(current_bgm, next);
                if (hooks.play_music) hooks.play_music(next);
                if (hooks.fade_music) hooks.fade_music(map_bgm_fade_steps, map_bgm_full_volume);
            }
        }
        memory.write(map_phase, map_phase_fading_in);
        memory.write(map_effect_pending, 1);
        // The newly loaded overlay palette slice is the fade-in target.
        if (hooks.copy_palette)
            hooks.copy_palette(palette_fade_buffer, overlay_palette_cache,
                               map_fade_in_palette_first, map_fade_in_palette_count);
        return memory.read(map_phase);
    }
    case map_phase_fading_in: {
        const auto done = hooks.run_screen_fade &&
                          hooks.run_screen_fade(mode, map_fade_in, map_fade_ms, context);
        if (!done || (hooks.music_fading && hooks.music_fading())) {
            if (hooks.invalidate_sheet_cache) hooks.invalidate_sheet_cache();
            memory.write(encounter_steps_a, 0);
            memory.write(map_transition_mode_word, 1);
        }
        memory.write(map_phase, map_phase_idle);
        if (!(flags & map_flag_show_name)) return map_phase_idle;
        if (hooks.show_map_name)
            hooks.show_map_name(map_record_names + memory.read(loaded_map) * map_record_bytes);
        return memory.read(map_phase);
    }
    default:
        return memory.read(map_phase);
    }
    if (!load_now) return memory.read(map_phase);

    // --- phase 3: build the new map --------------------------------------
    if (hooks.release_sparkles) hooks.release_sparkles();
    if (signed32(memory.read(map_pending_save_slot)) > -1) {
        // A restored save decides the map and the flags for itself.
        if (hooks.load_save_slot) hooks.load_save_slot(memory.read(map_pending_save_slot));
        memory.write(map_pending_save_slot, no_pending_save_slot);
        memory.write(map_transition_mode_word, 1);
        flags = map_flags_after_save;
        memory.write(current_bgm, invalid_bgm);
        map = memory.read(loaded_map);
    }
    bool rebuilt = false;
    if (memory.read(requested_map) != memory.read(loaded_map)) {
        const auto target = signed32(map) >= map_event_min_target ? signed32(map) : -1;
        auto source = signed32(memory.read(requested_map));
        if (signed32(map) == map_event_alias_target) source = map_event_alias_source;
        if (hooks.route_event && hooks.route_event(source, target)) {
            // An event took the frame; the map itself is not touched.
            finish_to_field(memory);
            return memory.read(map_phase);
        }
        // New resources invalidate the gate the field code had reserved.
        memory.write(active_gate, 0xffffffffu);
        if (hooks.release_old_map) hooks.release_old_map();
        if (hooks.load_map_resources) hooks.load_map_resources(map);
        if ((flags & map_flag_spawn_actors) && hooks.spawn_collected_actors)
            hooks.spawn_collected_actors();
        rebuilt = true;
    }
    if (hooks.spawn_sparkles) hooks.spawn_sparkles(map);
    if (rebuilt) {
        if (hooks.rebuild_after_load) hooks.rebuild_after_load(map);
        // A rebuild also cancels any outstanding line-effect focus handoff.
        memory.write(focus_handoff, 0);
        memory.write(encounter_steps_b, 0);
    }
    if (hooks.apply_viewport) {
        if (memory.read(map_record_resume_flag + memory.read(requested_map) * map_record_bytes) == 1)
            hooks.apply_viewport(true);
        else if (memory.read(map_dialog_mode))
            hooks.apply_viewport(false);
    }
    if (signed32(mode) >= map_palette_mode_limit && hooks.restore_palette)
        hooks.restore_palette(palette_fade_buffer, 0, map_palette_words);
    memory.write(map_phase, map_phase_arm_fade_in);
    if (hooks.restore_actor_position) hooks.restore_actor_position();
    if (hooks.run_screen_fade) hooks.run_screen_fade(0, map_fade_out, 0, palette_fade_buffer);
    return memory.read(map_phase);
}
} // namespace fsb::core::actor_core
