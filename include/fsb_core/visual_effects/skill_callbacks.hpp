#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace fsb::core::visual_effects {

// One constant a skill callback hands to its action routine.
struct DispatchArgument {
    enum class Kind { Absent, Constant, ActiveFacingTable };
    Kind kind;
    std::uint32_t value;  // Constant: the value. ActiveFacingTable: the table base.
};

// What a skill callback does when the sequence is in one of its cue phases.
// These phases are presentation only: they play a sound or publish a timing
// mode and, except for the last kind, return without running the action.
struct PhaseAction {
    enum class Kind { PlayCue, SetTimingMode, SetTimingModeThenDispatch };
    std::int32_t phase;
    Kind kind;
    std::uint32_t value;
};

// A monster, weapon or character skill callback. The original entry address
// stays the identifier: it is what the battle code registered as the skill's
// callback, and stage 3 is where those references move.
//
// A row with no phases is the plain forwarding case, which is most of them.
struct SkillCallbackRow {
    Address entry;
    Address routine;
    DispatchArgument arguments[2];
    const PhaseAction* phases;
    unsigned phase_count;
};

// The battle code keeps the acting actor here; the facing-indexed rows read
// its facing to choose a hit descriptor.
inline constexpr Address active_battle_actor_pointer = 0x8059f0;

// How many arguments the row pushes, counting the sequence object. The action
// routines clear their own arguments, so a caller that still uses the original
// calling convention has to push exactly this many.
inline constexpr unsigned pushed_arguments(const SkillCallbackRow& row) {
    return 1 + unsigned(row.arguments[0].kind != DispatchArgument::Kind::Absent)
             + unsigned(row.arguments[1].kind != DispatchArgument::Kind::Absent);
}

// Runs a skill callback: its cue phases, then its action routine.
//
// The routines are not owned by this session: the melee, status, burst,
// scatter, orbit, fountain, explosion and beam dispatchers are battle-side.
// Each is bound once, by address, so an unreconstructed routine shows up as a
// missing binding rather than a quietly different result.
class SkillCallbacks {
public:
    using Routine = std::function<std::uint32_t(Address sequence, std::int32_t first, std::int32_t second)>;

    explicit SkillCallbacks(Memory& memory) : memory_(memory) {}
    void bind(Address routine, Routine handler);
    // 435373. The original leaves the cue service's own EAX as the callback's
    // result, so this returns a value rather than dropping it.
    std::function<std::uint32_t(unsigned cue)> play_cue;

    static std::span<const SkillCallbackRow> rows();
    static const SkillCallbackRow* find(Address entry);
    // Distinct routine addresses the table forwards to, for binding and audit.
    static std::vector<Address> routines();

    bool handles(Address entry) const { return find(entry) != nullptr; }
    std::uint32_t invoke(Address entry, Address sequence) const;

private:
    std::int32_t resolve(const DispatchArgument& argument) const;
    std::uint32_t dispatch(const SkillCallbackRow& row, Address sequence) const;
    Memory& memory_;
    std::unordered_map<Address, Routine> bound_;
};

}
