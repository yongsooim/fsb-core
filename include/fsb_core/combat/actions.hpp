#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>

namespace fsb::core::combat {
namespace actions {
// A battle action runs as a controller object stepped by the states its own
// effects report back. Its second argument is a per-facing script table.
inline constexpr Address acting_actor = 0x8059f0, action_target = 0x8059f8;
inline constexpr Address target_count = 0x77a50c;
// Counters the controller keeps while its effects are in flight.
inline constexpr Address pending_effects = 0x78, pending_reports = 0x7a;
inline constexpr Address per_target_flags = 0x3c;
inline constexpr Address controller_owner = 0x1a8;
// States the effects report; the controller reads them as its own.
inline constexpr std::int32_t opened_state = -100, hit_state = -200, done_state = -250;
inline constexpr std::int32_t shown_state = -20, landed_state = -260;
inline constexpr std::int32_t flash_ticks = 0x1e;
// Bit0 of an actor's third flags byte hides it while its action plays.
inline constexpr std::uint32_t hidden_bit = 1;
// Per-target reaction: the script the target plays and the effect placed on it.
inline constexpr Address target_scripts = 0x5d2b18;
inline constexpr Address effect_selectors = 0x5bf498, effect_scripts = 0x5d2f40;
inline constexpr std::uint32_t effect_row = 0x1900000, effect_lift = 0x1c00000;
inline constexpr unsigned target_flag_bytes = 0x40;
inline constexpr std::int32_t missed_low = -12, missed_high = -10;
// The counter an action keeps of the effects that have landed.
inline constexpr Address landed = 0x7c;
// Per-target resolved amount, in the battle's own marker records.
inline constexpr Address target_amounts = 0x773f90, target_amount_stride = 0x10;
// The orbit action plays its own reaction scripts and only for targets that
// are flagged for one.
inline constexpr Address orbit_target_scripts = 0x5d2dd8;
inline constexpr std::uint32_t reacts_bit = 1;
} // namespace actions

// Battle action controllers. Each is a small state machine over the effect
// objects it spawns; the effects and the screen flash belong to other layers.
class Actions {
public:
    explicit Actions(Memory& memory) : memory_(memory) {}
    std::function<void(Address actor, Address script)> start_script;   // 447841
    std::function<void(Address object)> release_object;                // 45d91d
    std::function<bool(bool fade_in, unsigned ticks)> screen_flash;    // 464c59
    std::function<void(Address anchor)> spawn_beam;                    // 4657d7
    std::function<void(Address anchor)> spawn_orbit;                   // 467040
    std::function<void(Address anchor)> spawn_swirl;                   // 466df3
    std::function<void(Address actor, std::int32_t amount)> show_amount;  // 4647ad
    std::function<void(Address actor)> stop_motion;                    // 45db6c
    std::function<void(unsigned cue)> play_cue;                        // 435373
    std::function<void(unsigned cue)> stop_cue;                        // 4353cb
    std::function<void(std::int32_t x, std::int32_t y, std::int32_t z,
                       std::uint32_t layer, Address script)> spawn_effect;   // 464c27

    // 4658de: the beam action. Flashes the screen, plays the caster's script,
    // drops the beam on the target, then flashes back and finishes.
    void run_beam(Address controller, Address scripts);
    // How one target-effect action differs from the others.
    struct TargetAction {
        // Script the target plays and effect it takes, indexed by its facing
        // unless `per_facing` is false, when both tables hold one entry.
        Address target_script_table = actions::target_scripts;
        Address selector_table = actions::effect_selectors;
        bool per_facing = true;
        // Whether the action counts the reports its effects make.
        bool counts_reports = true;
        // A gate the action clears when it restarts, and how much of its own
        // per-target flag area it wipes.
        Address clear_on_reset = 0;
        unsigned reset_bytes = actions::target_flag_bytes;
        // Which reported state means one of this action's effects has finished.
        std::int32_t effect_done_state = actions::hit_state;
        // Actions that wait for every effect to land count them here and open
        // a gate once they all have.
        Address landed_counter = 0;
        Address landed_gate = 0;
        // A cue held for the length of the action, opened on the first target.
        unsigned held_cue = 0;
        std::int32_t release_cue_state = 0;
        // A cue played when the action restarts.
        unsigned reset_cue = 0;
    };
    // 465b7d/4660c4/466aaf: the shared shape of an action that puts one effect
    // on every target. `spawn_for_target` is what it opens with.
    void run_target_effects(Address controller, Address scripts, const TargetAction& shape,
                            const std::function<void(Address target)>& spawn_for_target);
    // 467111: the orbit action. Sets every target orbiting, then shows each
    // one's amount as its orbit breaks.
    void run_orbit(Address controller, Address scripts);

private:
    Memory& memory_;
    // Reveals the acting actor and starts the script for the way it faces.
    void play_caster_script(Address scripts);
    // Where in the target list the object that reported sits; the count itself
    // when it is not one of them, which is what the original leaves behind.
    unsigned reporting_target(Address controller) const;
    // Whether every target has cleared its own reaction flag.
    bool all_targets_settled(Address controller) const;
};
} // namespace fsb::core::combat
