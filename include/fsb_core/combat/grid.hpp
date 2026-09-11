#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>

namespace fsb::core::combat {
namespace grid {
// Movement cost per cell; the high byte carries occupancy rather than cost.
inline constexpr Address move_cost = 0x77a570, move_cost_occupied_bit = 0x10000;
// Tiles the party currently stands on, as (x, y) pairs the AI and the cost
// grid both read.
inline constexpr Address party_tiles = 0x776488, party_tile_stride = 8;
// Battle target scratch tables, cleared together at the start of targeting.
inline constexpr Address target_tables[] = {0x774190, 0x775960, 0x7760f0};
inline constexpr unsigned target_table_words = 0xc9;
// Per-battle actor records rebuilt on entry.
inline constexpr Address actor_records = 0x7744b8, actor_record_stride = 0x220;
inline constexpr unsigned actor_record_count = 8;
// Skill unlock schedule: ten (level, slot) pairs per character.
inline constexpr Address skill_schedule = 0x5bfd30, skill_schedule_stride = 80;
inline constexpr unsigned skill_schedule_pairs = 10;
inline constexpr Address learned_skills = 0x607a9c, character_skills = 0x6085c8;
inline constexpr unsigned character_skill_slots = 10;
// Difficulty scaling: a template block feeding one generated stat record.
inline constexpr Address difficulty_record = 0x608394, difficulty_template = 0x6082dc;
inline constexpr Address difficulty_scaled = 0x6082f4, difficulty_move = 0x776448;
// Random-encounter override, one entry per map.
inline constexpr Address encounter_override = 0x5bf4d8;
// Steps walked since the last encounter, and the item that suppresses them.
inline constexpr Address steps_since_encounter = 0x804aac, encounter_charm = 0x142;
// Extra rarity applied on top of the step count, and a forced scene phase.
inline constexpr Address encounter_rarity = 0x773f80, forced_scene_phase = 0x804aa4;
inline constexpr Address scene_phase = 0x77ec58, encounter_group = 0x77ec30;
inline constexpr Address encounter_facing = 0x5c0230;
inline constexpr std::int32_t encounter_blocked_radius = 2;
// Bag quantities, one entry per item.
inline constexpr Address inventory = 0x806e30;
inline constexpr unsigned equipment_owners = 13;
// Attack footprint reservation scratch, and the map origin it is placed at.
inline constexpr Address footprint_slots = 0x775888;
// Placement groups: how many cells each group still offers, how many it holds
// and the (x, y) pairs themselves.
inline constexpr Address group_free = 0x7760f0, group_size = 0x775960, group_cells = 0x774190;
inline constexpr std::uint32_t cell_reserved = 0x20000, cell_taken_mask = 0xffff0000;
inline constexpr Address map_origin_x = 0x77e58c, map_origin_y = 0x77e590;
} // namespace grid

// Battle grid bookkeeping: tile reservations, movement-cost occupancy, the
// per-battle scratch tables and the difficulty-scaled stat record.
class Grid {
public:
    explicit Grid(Memory& memory) : memory_(memory) {}

    // 448d72: republish the map's cell dimensions and drop the cached costs.
    void reset_dimensions();
    // 449999: mark every tile the party stands on as occupied.
    void mark_party_tiles();
    // 4499cb: clear the battle target scratch tables.
    void clear_target_tables();
    // 449f5b: reset the per-battle actor records to their empty state.
    void reset_actor_records();
    // 448aca: does any tile within `radius` of the active party member carry a
    // blocking attribute?
    bool region_blocked(std::int32_t radius) const;
    // 448c6e: reserve the tile a footprint cell maps to, if it is free and
    // cheap enough to stand on. Returns whether the reservation happened.
    bool reserve_footprint_tile(unsigned slot, unsigned shape);

    // 448a98/448ab1: force or clear a map's random-encounter override. -1 means
    // the map the party is on.
    void set_encounter_override(std::uint32_t map_id);
    void clear_encounter_override(std::uint32_t map_id);

    // 448a39: grant the skill this character unlocks at `level`, if any.
    std::optional<unsigned> unlock_skill(unsigned party_id, std::int32_t level);
    // 44a513: pick one still-free placement cell at random out of the groups
    // [first, first+count), reserve it and report which group and entry it came
    // from. Returns whether a cell was taken.
    using PlacementWriter = std::function<void(unsigned group, unsigned index)>;
    // Publish before decrementing the free count, preserving original write order.
    bool take_random_placement(unsigned first, unsigned count, const PlacementWriter& publish);
    // 448b4c: decide whether walking this step starts a random encounter, and
    // if so publish which group and how the scene should open.
    bool try_random_encounter();
    // 448a0a: does the party hold this item, equipped or in the bag?
    bool party_owns_item(unsigned item) const;
    // 448977: order two indices by their battle stat, largest first.
    void order_by_stat(Address first, Address second);
    bool stat_greater(unsigned first, unsigned second) const;
    // 44899e: battle stat lookup; kind2 uses a separate two-dimensional table.
    std::uint32_t stat(unsigned row, unsigned kind, unsigned column) const;

    // 44a93e/44aaa1: build and publish the difficulty-scaled stat record.
    void load_difficulty(unsigned step);
    void apply_difficulty(unsigned step);
    // 44ab05: give the entrance marker actor its party slot.
    void mark_entrance_actor();
    // 44a8d7: leave the modal overlay.
    void clear_overlay_modal();

private:
    Memory& memory_;
    // The original halves a scaled stat with a truncating signed shift.
    static std::int32_t half(std::int32_t value);
};
} // namespace fsb::core::combat
