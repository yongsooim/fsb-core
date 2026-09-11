#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>

namespace fsb::core::combat {
namespace flow {
// Actors whose follow-up step the battle is still waiting on.
inline constexpr Address pending_actors = 0x806478, pending_count = 0x8064f0;
inline constexpr Address outstanding = 0x805508;
// Callbacks the finished actors are handed back to.
inline constexpr Address party_idle_callback = 0x45c110, enemy_cleanup_callback = 0x461c66;
inline constexpr std::uint32_t busy_flag = 0x20000, resume_flag = 0x10000;
// Screen fade state; zero means no transition is running.
inline constexpr Address fade_state = 0x6d9e70;
// Scripted battle hand-off.
inline constexpr Address scripted_pending = 0x77ec54, scripted_phase = 0x77ec58;
inline constexpr Address scripted_group = 0x77ec30, scripted_music_started = 0x77ec50;
inline constexpr unsigned random_encounter_event = 167;
// Battle entrance bookkeeping.
inline constexpr Address entrance_actor_slot = 0x77a510, entrance_difficulty = 0x774168;
inline constexpr Address entrance_marker = 0x7760d4;
inline constexpr unsigned retired_party_slot = 12;
// Anchored effect objects copy their owner's position with a fixed offset.
inline constexpr Address anchor_effect_script = 0x5d26e0;
// Actor field holding the object an anchored effect follows.
inline constexpr Address anchor_owner = 0x160;
// Viewport size and the map rectangle the view has to stay inside.
inline constexpr Address view_width = 0x6e12b0, view_height = 0x6e1440;
// Map records; a map flagged1 gets the transition, others only a plain hand-back.
inline constexpr Address map_records = 0x5c4f5c, map_record_stride = 0x44;
inline constexpr Address viewport_ready = 0x769440;
inline constexpr Address battle_leader_slot = 0x77a4fc;
inline constexpr Address field_driver_callback = 0x458ed7;
inline constexpr std::uint32_t field_resume_flags = 0x100c0;
// A party member leaves battle with at least this much left to act on.
inline constexpr std::int32_t minimum_vitality = 1, gauge_cap = 0x14, gauge_left = 0xf;
inline constexpr unsigned field_game_mode = 3;
inline constexpr unsigned field_reset_first_slot=0x3c, field_reset_end_slot=0x59;
namespace party_field {
inline constexpr Address base_status=4, battle_status=8, hit_points=0x1c, turn_gauge=0x30;
}
inline constexpr std::uint32_t battle_overlay_bit=0x80, actor_visible_bit=0x40;
inline constexpr Address default_visual_callback=0x45c526;
inline constexpr Address bound_left = 0x6e1468, bound_top = 0x74b470;
inline constexpr Address bound_right = 0x74b4ac, bound_bottom = 0x74b6c0;
inline constexpr unsigned view_transition_ticks = 0x12c;
inline constexpr std::int32_t anchor_started = 10;
inline constexpr std::uint32_t anchor_depth_bias = 0x100000, anchor_height_bias = 0x400000;
} // namespace flow

// Battle progression: which actors the round is still waiting on, the scripted
// battle hand-off and the anchored effect objects that follow an owner.
class Flow {
public:
    explicit Flow(Memory& memory) : memory_(memory) {}
    void restore_after_handler(); //44e085: handler/event completion visual reset.
    // Explicit boundaries to services this layer does not own.
    std::function<void(Address actor, Address script)> start_effect_script;
    std::function<void(Address block)> release_block;
    std::function<void(unsigned fade_out, unsigned from, unsigned track,
                       unsigned start, unsigned fade_in, unsigned volume)> transition_music;
    std::function<void(unsigned step)> apply_difficulty;
    // 401d66: the screen transition. Takes the clamped rectangle, the mode and
    // the duration; this layer does not own the renderer.
    std::function<void(unsigned mode, std::int32_t left, std::int32_t top,
                       std::int32_t right, std::int32_t bottom, unsigned duration)> zoom_to;
    // Leaving a battle hands the view back and restores each party actor.
    // Both belong to other owners; keep them as explicit calls.
    std::function<void(bool transition)> release_viewport;
    std::function<void(Address actor, Address leader)> restore_field_actor;
    std::function<void(unsigned from, unsigned to)> restore_field_view;

    // 4622b6: is the screen fade idle?
    bool fade_idle() const;
    // 461ca2: hand every finished actor back to its own callback and report
    // whether the round has nothing left outstanding.
    bool finish_pending_steps();
    // 44a8df: enter a scripted battle. The random-encounter event picks one of
    // two groups; every other event starts the battle music itself.
    void trigger_scripted(unsigned group);
    // 44ab2e: retire the entrance actor's party slot after its difficulty row
    // has been published.
    void end_player_turn();
    // 4499f0: release the collected target lists and clear their tables.
    void release_target_tables();
    // 462150/462203: centre the view on a point, then slide the whole rectangle
    // back inside the map bounds rather than clamping its edges independently.
    // `hold_focus` selects the original's two transition modes.
    void frame_view(std::int32_t x, std::int32_t y, bool hold_focus);
    // 44df90 (and its 44ab62 tail call): leave the battle scene. Restores each
    // party member's field state and hands the leader back to the field driver.
    void leave_battle();
    // 462790: keep an anchored effect object on its owner, starting the shared
    // anchor script the first time.
    void track_anchor(Address effect);

private:
    Memory& memory_;
};
} // namespace fsb::core::combat
