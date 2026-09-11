#include "fsb_core/combat/grid.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core::combat {
namespace {
using namespace grid;
// Map cell dimensions the battle grid copies from the loaded map.
constexpr Address map_columns = 0x7e0d20, map_rows = 0x7e0d24;
// Tile attribute bits that stop a battle placement.
constexpr std::uint32_t blocking_attributes = 0x180000;
constexpr Address active_height = 0x77e598;
// Battle stat tables 44899e reads; kind2 is the only two-dimensional one.
constexpr Address stat_table = 0x5bf458, stat_table_stride = 12;
constexpr Address paired_stat_table = 0x5bf480, paired_stat_stride = 8;
constexpr Address stat_order_table = 0x5bf418;
// Footprint placement tables: shape offsets, then a per-facing rotation that
// says which offset component and sign each axis takes.
constexpr Address footprint_shape_x = 0x5bec98, footprint_shape_y = 0x5bec9c;
constexpr Address footprint_shape_row = 0x5bf1b8, footprint_shape_row_stride = 0x29;
constexpr Address footprint_rotation_x = 0x5bf1f8, footprint_rotation_y = 0x5bf1fc;
constexpr Address footprint_rotation_row = 0x5d0588;
constexpr Address footprint_shape_for_slot = 0x77ec08;
constexpr unsigned footprint_row_span = 15, footprint_grid_stride = 9;
// A cell must be free (slot above15) and cost less than5 to stand on.
constexpr std::int32_t footprint_free = 0xf, footprint_max_cost = 5;
constexpr Address overlay_modal = 0x77ec54, entrance_marker = 0x7760d4;
constexpr Address entrance_actor_slot = 0x77a510, entrance_actor_base = 0x774168;
} // namespace

std::int32_t Grid::half(std::int32_t value) {
    // cdq/sub/sar: a signed halving that truncates toward zero.
    return (value - (value >> 31)) >> 1;
}
void Grid::reset_dimensions() {
    memory_.write(globals::move_cost_grid_valid, 0, 1);
    memory_.write(globals::grid_row_stride, memory_.read(map_columns));
    memory_.write(globals::grid_height, memory_.read(map_rows));
}
void Grid::mark_party_tiles() {
    const auto width = memory_.read(globals::grid_row_stride);
    for (std::int32_t i = 0; i < signed32(memory_.read(globals::party_count)); ++i) {
        const auto tile = party_tiles + std::uint32_t(i) * party_tile_stride;
        const auto cell = memory_.read(tile + 4) * width + memory_.read(tile);
        const auto occupancy = move_cost + cell * 4 + 2;
        memory_.write(occupancy, memory_.read(occupancy, 1) | 1, 1);
    }
}
void Grid::clear_target_tables() {
    for (const auto table : target_tables) {
        const auto cells = memory_.span(table, target_table_words * 4);
        std::fill(cells.begin(), cells.end(), std::uint8_t(0));
    }
}
void Grid::reset_actor_records() {
    memory_.write(0x77ec3c, 0xffffffffu);
    memory_.write(0x7757dc, 0);
    for (unsigned i = 0; i < actor_record_count; ++i) {
        const auto record = actor_records + i * actor_record_stride;
        memory_.write(record, 0xffffffffu);
        for (const auto field : {0x4u, 0x8u, 0x14u}) memory_.write(record + field, 0);
        memory_.write(record + 0x1c, 0xffffffffu);
        for (unsigned slot = 0; slot < 5; ++slot) memory_.write(record + 0x24 + slot * 0x10, 0);
        for (unsigned slot = 0; slot < 20; ++slot) {
            memory_.write(record + 0x74 + slot * 0x14, 0xffffffffu);
            memory_.write(record + 0x78 + slot * 0x14, 0);
        }
        memory_.write(record + 0x204, 0);
        memory_.write(record + 0x218, 1, 1);
        memory_.write(record + 0x21c, 0xffffffffu);
    }
}
bool Grid::region_blocked(std::int32_t radius) const {
    const auto actor = Actors::slot(memory_.read(0x803a1c));
    const auto x = memory_.read(actor + actor_offset::tile_x);
    const auto y = memory_.read(actor + actor_offset::tile_y);
    const auto width = memory_.read(globals::grid_row_stride);
    const auto layer = memory_.read(active_height) * 4096u;
    for (std::int32_t dy = -radius; dy <= radius; ++dy)
        for (std::int32_t dx = -radius; dx <= radius; ++dx) {
            const auto cell = layer + std::uint32_t(dx) + x + (y + std::uint32_t(dy)) * width;
            if (memory_.read(globals::tile_attributes + cell * 4) & blocking_attributes) return true;
        }
    return false;
}
bool Grid::reserve_footprint_tile(unsigned slot, unsigned shape) {
    const auto facing = memory_.read(Actors::slot(memory_.read(0x803a1c)) + actor_offset::facing);
    const auto footprint = memory_.read(footprint_shape_for_slot + slot * 4);
    // The shape supplies a pair of offsets; the facing supplies which component
    // and sign each axis takes, so one shape table serves all four rotations.
    const auto shape_row = (memory_.read(footprint_shape_row + footprint * 4) * footprint_shape_row_stride + shape) * 8;
    const std::int32_t offsets[2] = {signed32(memory_.read(footprint_shape_x + shape_row)),
                                     signed32(memory_.read(footprint_shape_y + shape_row))};
    const auto rotation = (memory_.read(footprint_rotation_row + facing * 4) * footprint_row_span + footprint) * 8;
    constexpr std::int32_t signs[4] = {1, 1, -1, -1};
    constexpr unsigned axes[4] = {0, 1, 0, 1};
    const auto component = [&](Address table) {
        const auto choice = memory_.read(table + rotation);
        if (choice >= 4) throw Fault(table, "battle footprint rotation outside its table");
        return offsets[axes[choice]] * signs[choice];
    };
    const auto dx = component(footprint_rotation_x), dy = component(footprint_rotation_y);
    const auto reservation = footprint_slots + std::uint32_t(dx + dy * footprint_grid_stride) * 4;
    if (signed32(memory_.read(reservation)) <= footprint_free) return false;
    const auto y = signed32(memory_.read(map_origin_y)) + dy;
    const auto cell = signed32(memory_.read(globals::grid_row_stride)) * y + dx + signed32(memory_.read(map_origin_x));
    if (signed32(memory_.read(move_cost + std::uint32_t(cell) * 4)) >= footprint_max_cost) return false;
    memory_.write(reservation, slot);
    memory_.write(party_tiles + slot * party_tile_stride, std::uint32_t(dx + signed32(memory_.read(map_origin_x))));
    memory_.write(party_tiles + slot * party_tile_stride + 4, std::uint32_t(y));
    return true;
}
bool Grid::take_random_placement(unsigned first, unsigned count, const PlacementWriter& publish) {
    std::int32_t free_cells = 0, entries = 0;
    for (unsigned group = first; group < first + count; ++group) {
        free_cells += signed32(memory_.read(group_free + group * 4));
        entries += signed32(memory_.read(group_size + group * 4));
    }
    if (free_cells < 1 || entries <= 0) return false;
    auto skip = std::int32_t(crt_rand(memory_)) % free_cells;
    const auto width = memory_.read(globals::grid_row_stride);
    auto group = first;
    std::int32_t consumed = 0;
    for (std::int32_t visited = 0; visited < entries; ++visited) {
        auto local = visited - consumed;
        if (local >= signed32(memory_.read(group_size + group * 4))) {
            // The original steps to the next group only once per entry and gives
            // up as soon as that one is empty, rather than skipping empty groups.
            consumed += signed32(memory_.read(group_size + group * 4));
            local = 0;
            ++group;
            if (signed32(memory_.read(group_size + group * 4)) <= 0) return false;
        }
        const auto cells = memory_.read(group_cells + group * 4);
        const auto x = signed32(memory_.read(cells + std::uint32_t(local) * 8));
        const auto y = signed32(memory_.read(cells + std::uint32_t(local) * 8 + 4));
        const auto cell = std::uint32_t(y) * width + std::uint32_t(x);
        const auto cost = memory_.read(move_cost + cell * 4);
        if (x < 1 || y < 1) return false;
        if (!(cost & cell_taken_mask)) {
            if (skip == 0) {
                memory_.write(move_cost + cell * 4, memory_.read(move_cost + cell * 4) | cost | cell_reserved);
                publish(group, std::uint32_t(local));
                memory_.write(group_free + group * 4, memory_.read(group_free + group * 4) - 1);
                return true;
            }
            --skip;
        }
    }
    return false;
}
bool Grid::try_random_encounter() {
    const auto group = signed32(memory_.read(encounter_override + memory_.read(globals::current_map_id) * 4));
    BattleRules rules(memory_);
    for (std::int32_t i = 0; i < signed32(memory_.read(globals::party_count)); ++i)
        if (rules.equipped(memory_.read(globals::party_actor_ids + std::uint32_t(i) * 4), encounter_charm))
            return false;
    if (group <= -1) return false;
    // The battle record this group would use has to be free.
    if (signed32(memory_.read(actor_records + std::uint32_t(group) * actor_record_stride)) != -1) return false;
    const auto needed = std::int32_t(crt_rand(memory_)) % 40 + 14;
    if (signed32(memory_.read(steps_since_encounter)) <= needed) return false;
    if (region_blocked(encounter_blocked_radius)) return false;
    // The rarity roll is drawn whether or not the map applies one.
    const auto rarity_roll = std::int32_t(crt_rand(memory_));
    const auto rarity = signed32(memory_.read(encounter_rarity));
    if (rarity > 0 && rarity_roll % ((rarity + 1) * 100) > 0) return false;
    const auto opening = std::int32_t(crt_rand(memory_)) % 100;
    // Rarer openings run a longer approach before the fight starts.
    const auto phase = opening < 2 ? 4 : opening < 10 ? 3 : opening < 22 ? 2 : opening < 40 ? 1 : 0;
    memory_.write(scene_phase, std::uint32_t(phase));
    memory_.write(steps_since_encounter, 0);
    memory_.write(encounter_group, std::uint32_t(group));
    memory_.write(encounter_facing, memory_.read(Actors::slot(memory_.read(0x803a1c)) + actor_offset::facing));
    const auto forced = signed32(memory_.read(forced_scene_phase));
    if (forced > 0) memory_.write(scene_phase, std::uint32_t(forced));
    return true;
}
void Grid::set_encounter_override(std::uint32_t map_id) {
    const auto map = map_id == 0xffffffffu ? memory_.read(globals::current_map_id) : map_id;
    memory_.write(encounter_override + map * 4, memory_.read(encounter_override + map * 4) | 0xffffffffu);
}
void Grid::clear_encounter_override(std::uint32_t map_id) {
    const auto map = map_id == 0xffffffffu ? memory_.read(globals::current_map_id) : map_id;
    memory_.write(encounter_override + map * 4, 0);
}
std::optional<unsigned> Grid::unlock_skill(unsigned party_id, std::int32_t level) {
    const auto schedule = skill_schedule + party_id * skill_schedule_stride;
    for (unsigned pair = 0; pair < skill_schedule_pairs; ++pair) {
        const auto at = signed32(memory_.read(schedule + pair * 8));
        if (at <= 0) break; // The schedule ends at its first empty pair.
        if (at != level) continue;
        const auto slot = memory_.read(schedule + pair * 8 + 4);
        const auto skill = memory_.read(character_skills + (party_id * character_skill_slots + slot) * 4);
        memory_.write(learned_skills + (party_id * 0x2f + slot) * 4, skill);
        return skill;
    }
    return std::nullopt;
}
bool Grid::party_owns_item(unsigned item) const {
    BattleRules rules(memory_);
    for (unsigned id = 0; id < equipment_owners; ++id) if (rules.equipped(id, item)) return true;
    return signed32(memory_.read(inventory + item * 4)) > 0;
}
void Grid::order_by_stat(Address first, Address second) {
    const auto a = memory_.read(first), b = memory_.read(second);
    if (stat_greater(a, b)) {
        memory_.write(first, b);
        memory_.write(second, a);
    }
}
bool Grid::stat_greater(unsigned first, unsigned second) const {
    return signed32(memory_.read(stat_order_table + first * 4)) >
           signed32(memory_.read(stat_order_table + second * 4));
}
std::uint32_t Grid::stat(unsigned row, unsigned kind, unsigned column) const {
    if (kind == 2) return memory_.read(paired_stat_table + row * paired_stat_stride + column * 4);
    return memory_.read(stat_table + row * stat_table_stride + column * 4);
}
void Grid::load_difficulty(unsigned step) {
    // The template block holds one enemy stat line; each step scales the growing
    // stats by (step+3)/2 and shifts the flat ones by the step itself.
    const auto record = difficulty_record + step * 0xbc;
    const auto scale = step + 3;
    const auto source = [&](Address offset) { return signed32(memory_.read(difficulty_template + offset)); };
    // Products wrap at32 bits exactly as the original multiply does.
    const auto times = [&](Address offset, std::uint32_t factor) {
        return std::int32_t(std::uint32_t(source(offset)) * factor);
    };
    const auto scaled = [&](Address offset) { return std::uint32_t(half(times(offset, scale))); };
    memory_.write(record + 4, std::uint32_t(source(0)));
    memory_.write(record + 8, std::uint32_t(source(4)));
    memory_.write(record + 0x18, scaled(0x14));
    memory_.write(record + 0x1c, scaled(0x18));
    memory_.write(record + 0x20, 0);
    memory_.write(record + 0x24, 0);
    // Flat stats copy straight across, in the original's own field order.
    constexpr std::pair<Address, Address> copied[] = {{0x28, 0x24}, {0x30, 0x2c}, {0x34, 0x30},
        {0x38, 0x34}, {0x3c, 0x38}, {0x40, 0x3c}, {0x44, 0x40}, {0x48, 0x44}, {0x4c, 0x48},
        {0x50, 0x4c}, {0x54, 0x50}, {0x74, 0x70}, {0x78, 0x74}, {0x7c, 0x78}, {0x80, 0x7c},
        {0x84, 0x80}};
    for (const auto [target, offset] : copied) memory_.write(record + target, std::uint32_t(source(offset)));
    memory_.write(record + 0x58, scaled(0x54));
    memory_.write(record + 0x5c, std::uint32_t(times(0x58, 4u - step) / 3));
    memory_.write(record + 0x60, std::uint32_t(source(0x5c)) + (step * 5 + 5) * 2);
    memory_.write(record + 0x64, std::uint32_t(std::int32_t(std::uint32_t(times(0x60, step + 4)) * 4u) / 3));
    memory_.write(record + 0x68, scaled(0x64));
    memory_.write(record + 0x6c, std::uint32_t(times(0x68, 5u - step) / 3));
    memory_.write(record + 0x70, std::uint32_t(source(0x6c)) + step * 2 + 2);
    memory_.write(record + 0x88, step + 0x58);
    memory_.write(record + 0x8c, step + 0x58);
}
void Grid::apply_difficulty(unsigned step) {
    const auto record = difficulty_record + step * 0xbc;
    auto move = std::int32_t(memory_.read(record + 0x1c) * 2u) / std::int32_t(step + 3);
    memory_.write(difficulty_scaled, std::uint32_t(move));
    constexpr std::pair<Address, Address> published[] = {{0x28, 0x608300}, {0x30, 0x608308},
                                                        {0x34, 0x60830c}, {0x38, 0x608310}};
    for (const auto [offset, target] : published) memory_.write(target, memory_.read(record + offset));
    if (move < 1) { move = 1; memory_.write(difficulty_scaled, 1); }
    memory_.write(entrance_marker, 0, 1);
    memory_.write(difficulty_move, std::uint32_t(move));
}
void Grid::mark_entrance_actor() {
    const auto actor = Actors::slot(memory_.read(entrance_actor_slot));
    memory_.write(entrance_marker, 1, 1);
    memory_.write(globals::party_actor_ids + memory_.read(actor) * 4, memory_.read(entrance_actor_base) + 13);
}
void Grid::clear_overlay_modal() { memory_.write(overlay_modal, 0, 1); }
} // namespace fsb::core::combat
