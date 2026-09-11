#include "fsb_core/visual_effects/skill_callbacks.hpp"
#include "fsb_core/visual_effects/effect_object.hpp"
#include "fsb_core/effect_script.hpp"
#include <algorithm>

namespace fsb::core::visual_effects {
namespace {
// The phase lists the rows point into, and the rows themselves.
#include "skill_callback_phases.inc"
constexpr SkillCallbackRow table[] = {
#include "skill_callback_table.inc"
};
}

std::span<const SkillCallbackRow> SkillCallbacks::rows() { return table; }

const SkillCallbackRow* SkillCallbacks::find(Address entry) {
    const auto at = std::lower_bound(std::begin(table), std::end(table), entry,
                                     [](const SkillCallbackRow& row, Address key) { return row.entry < key; });
    return at != std::end(table) && at->entry == entry ? at : nullptr;
}

std::vector<Address> SkillCallbacks::routines() {
    std::vector<Address> found;
    for (const auto& row : table) found.push_back(row.routine);
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

void SkillCallbacks::bind(Address routine, Routine handler) { bound_[routine] = std::move(handler); }

std::int32_t SkillCallbacks::resolve(const DispatchArgument& argument) const {
    switch (argument.kind) {
    case DispatchArgument::Kind::Absent:
        return 0;
    case DispatchArgument::Kind::Constant:
        return std::int32_t(argument.value);
    case DispatchArgument::Kind::ActiveFacingTable: {
        const auto actor = memory_.read(active_battle_actor_pointer);
        const auto facing = std::int32_t(memory_.read(actor + actor_offset::facing));
        return std::int32_t(memory_.read(argument.value + 4 * std::uint32_t(facing)));
    }
    }
    return 0;
}

std::uint32_t SkillCallbacks::dispatch(const SkillCallbackRow& row, Address sequence) const {
    const auto binding = bound_.find(row.routine);
    if (binding == bound_.end()) throw Fault(row.routine, "skill action routine is not bound");
    // The original reads the facing-indexed descriptor before pushing either
    // argument; both reads are free of side effects, so the order is not
    // observable, but the values are taken before the routine can change them.
    const auto first = resolve(row.arguments[0]);
    const auto second = resolve(row.arguments[1]);
    return binding->second(sequence, first, second);
}

std::uint32_t SkillCallbacks::invoke(Address entry, Address sequence) const {
    const auto* row = find(entry);
    if (!row) throw Fault(entry, "no skill callback row for this entry point");
    if (!row->phase_count) return dispatch(*row, sequence);
    const auto phase = std::int32_t(memory_.read(sequence + actor_offset::callback_state));
    for (unsigned i = 0; i < row->phase_count; ++i) {
        const auto& action = row->phases[i];
        if (action.phase != phase) continue;
        switch (action.kind) {
        case PhaseAction::Kind::PlayCue:
            if (!play_cue) throw Fault(entry, "skill cue service is not attached");
            return play_cue(action.value);
        case PhaseAction::Kind::SetTimingMode:
            memory_.write(effect_script::timing_mode, action.value);
            // The original never touches EAX on this path, so the phase it
            // loaded to make the comparison is what the callback returns.
            return std::uint32_t(phase);
        case PhaseAction::Kind::SetTimingModeThenDispatch:
            memory_.write(effect_script::timing_mode, action.value);
            return dispatch(*row, sequence);
        }
    }
    return dispatch(*row, sequence);
}

}
