#pragma once
#include "actor_lifecycle.hpp"

// Moving from one map to the next.
//
// The phase word at0x803a4c drives a small state machine the field loop
// re-enters every frame:
//
//   1 begin    snapshot the palette, start the music fading out, fall through
//   2 fade out wait for the screen and the music, then load
//   3 load     release the old map, build the new one, arm the fade in
//   4 arm      start the new music, prepare the fade in
//   5 fade in  wait for the screen, go idle, maybe show the map name
//
// Phase3 is also entered directly by the title, a new game and a worldmap
// handoff, which have already left ordinary field rendering behind. Every
// call this routine makes outside its own state is an injected hook.
namespace fsb::core::actor_core {

inline constexpr Address map_phase = 0x803a4c;
inline constexpr Address map_transition_target = 0x803a48;
inline constexpr Address map_pending_save_slot = 0x5d2498;
inline constexpr Address map_transition_mode_word = 0x804a68;
inline constexpr Address map_effect_pending = 0x804a6c;
inline constexpr Address current_bgm = 0x804658;
inline constexpr Address palette_fade_buffer = 0x804660;
inline constexpr Address overlay_palette_cache = 0x803e50;
inline constexpr Address requested_map = 0x5d229c;   // Where the game is going.
inline constexpr Address loaded_map = 0x5d22a0;      // What is actually loaded.
inline constexpr Address active_gate = 0x5f858c;
inline constexpr Address encounter_steps_a = 0x804aac;
inline constexpr Address encounter_steps_b = 0x773f80;
inline constexpr Address focus_handoff = 0x7873b8;
inline constexpr Address map_dialog_mode = 0x769440;
inline constexpr Address worldmap_route_from = 0x77149c;
inline constexpr Address worldmap_route_to = 0x7714a0;

// The map record table:0x44 bytes per map.
inline constexpr Address map_record_names = 0x5c4f24;
inline constexpr Address map_record_resume_flag = 0x5c4f5c;
inline constexpr Address map_record_bgm = 0x5c4f58;
inline constexpr Address map_record_callback = 0x5c4f60;
inline constexpr unsigned map_record_bytes = 0x44;

inline constexpr std::uint32_t map_phase_idle = 0;
inline constexpr std::uint32_t map_phase_begin = 1;
inline constexpr std::uint32_t map_phase_fading_out = 2;
inline constexpr std::uint32_t map_phase_load = 3;
inline constexpr std::uint32_t map_phase_arm_fade_in = 4;
inline constexpr std::uint32_t map_phase_fading_in = 5;

inline constexpr unsigned map_fade_out = 1, map_fade_in = 0;
inline constexpr unsigned map_fade_ms = 500;
inline constexpr unsigned map_bgm_fade_steps = 0x1e, map_bgm_full_volume = 100;
inline constexpr unsigned map_palette_words = 0x100;
inline constexpr unsigned map_fade_in_palette_first = 0x70, map_fade_in_palette_count = 0x70;
// Modes below this take the palette buffer as their screen-effect context;
// modes5 and9 take the active actor's record instead, and the rest take none.
inline constexpr std::int32_t map_palette_mode_limit = 5;
inline constexpr std::int32_t map_actor_mode_a = 5, map_actor_mode_b = 9;
inline constexpr std::uint32_t map_flag_spawn_actors = 1, map_flag_show_name = 2;
// Flags the transition adopts after restoring a save.
inline constexpr std::uint32_t map_flags_after_save = 3;
// Only maps at or above this id are worth telling the event router about.
inline constexpr std::int32_t map_event_min_target = 10;
// One map reports itself to the router under its neighbour's id.
inline constexpr std::int32_t map_event_alias_target = 0x169, map_event_alias_source = 0x168;
inline constexpr std::uint32_t map_character_slots = 0x59;
inline constexpr std::uint32_t game_mode_field = 3, game_mode_worldmap_event = 10;
inline constexpr std::uint32_t no_pending_save_slot = 0xffffffffu;
inline constexpr std::uint32_t invalid_bgm = 0xffffffffu;

// Everything this routine reaches outside its own state. The names say what
// the transition wants, not how the service does it.
struct MapTransitionHooks {
    std::function<void()> close_dialogue;
    std::function<void(Address buffer, unsigned first, unsigned count)> capture_palette;
    std::function<void(Address buffer, unsigned first, unsigned count)> restore_palette;
    std::function<void(Address destination, Address source, unsigned first, unsigned count)> copy_palette;
    std::function<void(unsigned steps, unsigned volume)> fade_music;
    std::function<void(std::uint32_t track)> play_music;
    std::function<bool()> music_fading;
    // 4321ab: answers true once the screen effect has finished.
    std::function<bool(std::int32_t mode, unsigned direction, unsigned milliseconds,
                       Address context)> run_screen_fade;
    std::function<void(Address name)> show_map_name;
    std::function<void()> invalidate_sheet_cache;
    std::function<void()> release_sparkles;
    std::function<void(std::uint32_t slot)> load_save_slot;
    // 412181: non-zero means an event took this frame over.
    std::function<std::uint32_t(std::int32_t source, std::int32_t target)> route_event;
    std::function<bool(std::int32_t from, std::int32_t to)> worldmap_route_exists;
    // The five table clears the old map is torn down with.
    std::function<void()> release_old_map;
    std::function<void(std::uint32_t map)> load_map_resources;
    std::function<void()> spawn_collected_actors;
    std::function<void(std::uint32_t map)> spawn_sparkles;
    // Patch resync, the map's own init callback, and the three lookup rebuilds.
    std::function<void(std::uint32_t map)> rebuild_after_load;
    std::function<void(bool resume)> apply_viewport;
    std::function<void()> restore_actor_position;
};

// 46018b. Returns the phase the transition is now in.
std::uint32_t advance_map_transition(Memory& memory, std::uint32_t map, std::int32_t mode,
                                     std::uint32_t flags, const MapTransitionHooks& hooks);

} // namespace fsb::core::actor_core
