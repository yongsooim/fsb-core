#include "fsb_core/combat/status.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::combat {
namespace {
// Party and enemy records keep their status timers in the word after the flags
// and their icon bits in the two bytes inside that word's own record.
constexpr Address party_timers = 12, party_icons = 9;
constexpr Address enemy_timers = 12, enemy_icons = 9, enemy_extra_timers = 16, enemy_extra_icons = 10;
// Each slot starts at its own tick count; the original writes them as literals.
constexpr unsigned durations[] = {4, 3, 3, 2, 3, 1};
constexpr Address equipment_first = 0x74, equipment_slots = 5;
constexpr Address item_table = 0x613190, item_stride = 0x4c, item_status_flags = 0xc;
constexpr Address experience_table = 0x5c0238;
constexpr unsigned maximum_level = 99;
constexpr Address battle_markers = 0x773f88, battle_marker_count = 0x77a50c, battle_marker_stride = 0x10;
constexpr Address active_height = 0x77e598;
// Attacks flagged 2000 pick their status slot at random instead of carrying it.
constexpr std::uint32_t random_slot_attack = 0x2000;
constexpr unsigned attack_slot_shift = 8, attack_slot_mask = 0x1f;
constexpr Address scene_track_selector = 0x773004, scene_track_range = 0x804aa8;
} // namespace

void Status::afflict(Address timers, Address icons, unsigned slot, unsigned count) const {
    const auto shift = slot * 4;
    memory_.write(timers, (memory_.read(timers) & ~(15u << shift)) | (count << shift));
    memory_.write(icons, memory_.read(icons, 1) | (1u << slot), 1);
}
void Status::clear_turn_flags() {
    for (unsigned i = 0; i < memory_.read(globals::party_count); ++i) {
        const auto record = BattleRules::party_record(memory_.read(globals::party_actor_ids + i * 4));
        memory_.write(record + 8, memory_.read(record + 8) & 0xff9c00ffu);
        memory_.write(record + party_timers, 0);
        memory_.write(record + 16, 0, 1); // Only the low byte of the second timer word.
    }
}
void Status::afflict_party(unsigned party_id, unsigned slot) {
    if (slot >= party_slots) return;
    const auto record = BattleRules::party_record(party_id);
    afflict(record + party_timers, record + party_icons, slot, durations[slot]);
}
void Status::afflict_enemy(unsigned enemy_slot, unsigned slot) {
    if (slot >= enemy_slots) return;
    const auto record = BattleRules::enemy_record(enemy_slot);
    if (slot == unsigned(StatusSlot::enemy_extra))
        afflict(record + enemy_extra_timers, record + enemy_extra_icons, 2, durations[slot]);
    else afflict(record + enemy_timers, record + enemy_icons, slot, durations[slot]);
}
void Status::refresh_equipment_flags(unsigned party_id) {
    const auto record = BattleRules::party_record(party_id);
    std::uint32_t flags = 0;
    for (unsigned i = 0; i < equipment_slots; ++i)
        flags |= memory_.read(item_table + memory_.read(record + equipment_first + i * 4) * item_stride + item_status_flags);
    memory_.write(record + 8, memory_.read(record + 8) | flags);
}
bool Status::award_experience(unsigned party_id, std::int32_t gain) {
    const auto record = BattleRules::party_record(party_id);
    auto level = memory_.read(record + 0x28);
    const auto experience = std::int32_t(memory_.read(record + 0x34) + std::uint32_t(gain));
    if (level == maximum_level) { memory_.write(record + 0x38, 0); return false; }
    const auto threshold = [&](std::uint32_t at) { return signed32(memory_.read(experience_table + at * 4)); };
    if (threshold(level) > experience) {
        memory_.write(record + 0x34, std::uint32_t(experience));
        memory_.write(record + 0x38, std::uint32_t(threshold(level) - experience));
        return false;
    }
    memory_.write(record + 0x28, ++level);
    // A single call never skips a level; overflowing experience stops one short
    // of the next threshold so the following gain resumes from there.
    const auto next = threshold(level);
    const auto carried = next <= experience ? next - 1 : experience;
    memory_.write(record + 0x34, std::uint32_t(carried));
    memory_.write(record + 0x38, level == maximum_level ? 0 : std::uint32_t(next - carried));
    return true;
}
std::optional<unsigned> Status::marker_facing(Address actor) const {
    constexpr int steps[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    const auto facing = memory_.read(actor + actor_offset::facing);
    if (facing >= 4) throw Fault(actor, "battle marker lookup outside four facings");
    const auto x = memory_.read(actor + actor_offset::tile_x) + std::uint32_t(steps[facing][0]);
    const auto y = memory_.read(actor + actor_offset::tile_y) + std::uint32_t(steps[facing][1]);
    const auto markers = signed32(memory_.read(battle_marker_count));
    for (int i = 0; i < markers; ++i) {
        const auto slot = Actors::slot(memory_.read(battle_markers + std::uint32_t(i) * battle_marker_stride));
        if (memory_.read(slot + actor_offset::tile_x) == x && memory_.read(slot + actor_offset::tile_y) == y)
            return unsigned(i);
    }
    return std::nullopt;
}
std::optional<unsigned> Status::actor_at_tile(std::int32_t x, std::int32_t y) const {
    const auto cell = memory_.read(active_height) * 4096u
                    + std::uint32_t(y) * memory_.read(globals::grid_row_stride) + std::uint32_t(x);
    const auto occupancy = memory_.read(globals::tile_occupancy + cell * 4);
    if (!(occupancy & 0x10000)) return std::nullopt;
    return occupancy & 0xffffu;
}
void Status::award_party_experience() {
    for (unsigned i = 0; i < levelled_row_bytes; ++i) memory_.write(levelled_row + i, 0, 1);
    BattleRules rules(memory_);
    const auto unavailable = battle_status::unavailable;
    const auto pay = [&](unsigned slot, unsigned party_id, std::int32_t share) {
        const auto amount = rules.equipped(party_id, experience_ring) ? signed32(std::uint32_t(share) * 2u) : share;
        const auto levelled = award_through_boundary ? award_through_boundary(party_id, amount)
                                                     : award_experience(party_id, amount);
        memory_.write(levelled_row + slot, levelled, 1);
    };
    const auto chosen = signed32(memory_.read(sole_recipient));
    const auto living = [&](unsigned party_id) {
        return !(memory_.read(BattleRules::party_record(party_id) + 8) & unavailable);
    };
    if (chosen > -1) {
        const auto party_id = memory_.read(globals::party_actor_ids + std::uint32_t(chosen) * 4);
        if (living(party_id)) { pay(unsigned(chosen), party_id, signed32(memory_.read(earned_experience))); return; }
        // A downed recipient falls through to the ordinary split.
    }
    const auto slots = signed32(memory_.read(globals::party_count));
    std::int32_t sharers = 0;
    for (std::int32_t i = 0; i < slots; ++i)
        if (living(memory_.read(globals::party_actor_ids + std::uint32_t(i) * 4))) ++sharers;
    if (sharers < 1) return;
    const auto share = signed32(memory_.read(earned_experience)) / sharers;
    for (std::int32_t i = 0; i < slots; ++i) {
        const auto party_id = memory_.read(globals::party_actor_ids + std::uint32_t(i) * 4);
        if (living(party_id)) pay(unsigned(i), party_id, share);
    }
}
std::optional<unsigned> Status::roll_status(std::uint32_t attack_flags, std::uint32_t immune,
                                            std::int32_t chance) const {
    if (chance <= std::int32_t(crt_rand(memory_)) % 100) return std::nullopt;
    // The second roll is consumed only on the random-slot path.
    const auto offered = attack_flags & random_slot_attack
        ? 1u << (std::int32_t(crt_rand(memory_)) % 5)
        : (attack_flags >> attack_slot_shift) & attack_slot_mask;
    const auto landed = offered & ~immune;
    // Exactly one slot has to survive; two at once is not a status the original
    // knows how to apply.
    for (unsigned slot = 0; slot < party_slots; ++slot) if (landed == 1u << slot) return slot;
    return std::nullopt;
}
unsigned Status::scene_track() const {
    const auto selector = signed32(memory_.read(scene_track_selector));
    const auto choice = selector >= 0 ? std::uint32_t(selector)
        : std::int32_t(crt_rand(memory_)) % std::int32_t(memory_.read(scene_track_range) + 1);
    if (choice >= std::size(scene_tracks)) throw Fault(scene_track_selector, "battle scene track outside its table");
    return scene_tracks[choice];
}
std::int32_t Status::capped_amount(std::int32_t amount) { return amount > amount_cap ? amount_cap : amount; }
} // namespace fsb::core::combat
