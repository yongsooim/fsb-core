#pragma once
#include "fsb_core/battle_rules.hpp"
#include <functional>

namespace fsb::core::combat {
// Status slots occupy one nibble each of a combatant's timer word and one bit
// of the byte that tells the panel which icon to draw. Party records hold five
// slots in the first timer word; enemy records hold the same five plus a sixth
// in the following word. Durations are per slot, not per caller.
enum class StatusSlot : unsigned { poison, sleep, silence, paralysis, curse, enemy_extra };
inline constexpr unsigned party_slots = 5, enemy_slots = 6;

// Battle tracks the scene may play, in the original's own order.
inline constexpr unsigned scene_tracks[] = {0x29, 0x28, 0x2c};
// Item that doubles its wearer's share of the battle experience.
inline constexpr unsigned experience_ring = 0xe1;
// One byte per party slot saying whether that member gained a level.
inline constexpr Address levelled_row = 0x77e578;
inline constexpr unsigned levelled_row_bytes = 10;
// Set to a party slot when one member should take the whole payout.
inline constexpr Address sole_recipient = 0x7757d8;
inline constexpr Address earned_experience = 0x77ebf8;

// Turn-level status bookkeeping over the original party and enemy records.
// Every field stays in the existing guest records; this owns no copy.
class Status {
public:
    explicit Status(Memory& memory) : memory_(memory) {}
    // 44de15 is a declared service boundary: the approved development reward
    // policy opts into it. The payout must go through the same boundary rather
    // than calling award_experience() straight through, or that policy is lost.
    std::function<bool(unsigned party_id, std::int32_t amount)> award_through_boundary;
    // 44caee: drop the per-turn status bits and both timer words of every
    // party member currently in the formation.
    void clear_turn_flags();
    // 44e689/44e56d: start one status slot at its own duration and mark the
    // icon bit. Slots outside the record's range leave it untouched.
    void afflict_party(unsigned party_id, unsigned slot);
    void afflict_enemy(unsigned enemy_slot, unsigned slot);
    // 44e46e: OR the equipment table's status word of all five equipped items
    // into the character's status flags.
    void refresh_equipment_flags(unsigned party_id);
    // 44de15: add experience and, at most once per call, advance one level.
    // Returns whether the level advanced; also republishes experience-to-next.
    bool award_experience(unsigned party_id, std::int32_t gain);
    // 44e3e1: index into the battle marker list of the party member standing on
    // the tile the actor faces, or nullopt when that tile holds none.
    std::optional<unsigned> marker_facing(Address actor) const;
    // 44ce7b: actor id occupying a grid cell on the active height layer.
    std::optional<unsigned> actor_at_tile(std::int32_t x, std::int32_t y) const;
    // 44de92: pay out the battle's experience. A named recipient takes it all;
    // otherwise the living party splits it. The experience ring doubles a
    // member's share. Records each member's level-up in the result row.
    void award_party_experience();
    // 44e500: roll whether an attack inflicts a status, and which slot. The
    // chance roll always happens; a second roll only for a random-slot attack.
    // Returns the slot, or nullopt when nothing lands.
    std::optional<unsigned> roll_status(std::uint32_t attack_flags, std::uint32_t immune,
                                        std::int32_t chance) const;
    // 4644d3: which battle track this scene plays. A negative selector picks
    // one at random from the scene's own range.
    unsigned scene_track() const;
    // 44e7d3: the original caps a resolved amount before it reaches a record.
    static std::int32_t capped_amount(std::int32_t amount);
    static constexpr std::int32_t amount_cap = 0x10270f;

private:
    Memory& memory_;
    void afflict(Address timers, Address icons, unsigned slot, unsigned count) const;
};
} // namespace fsb::core::combat
