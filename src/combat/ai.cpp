#include "fsb_core/combat/mask_scan.hpp"
#include "fsb_core/combat/ai.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core::combat {
namespace {
using namespace ai;
constexpr Address enemy_action_runner = 0x45befc;
constexpr Address action_request = 0x7760d8, action_phase = 0x77ec4c, action_step = 0x77ecdc;
// Enemies the AI must not weigh: already acting or already resolved.
constexpr unsigned untargetable_icons = 0x60;
} // namespace

std::int32_t Ai::score(unsigned index) const {
    return signed32(memory_.read(candidates + index * candidate_stride + candidate_score));
}
bool Ai::reachable(unsigned index) const {
    const auto record = candidates + index * candidate_stride;
    const auto x = memory_.read(record + candidate_x);
    const auto y = memory_.read(record + candidate_y) * memory_.read(globals::grid_row_stride);
    return memory_.read(reach_overlay + y + x, 1) != 0;
}
std::int32_t Ai::validate(unsigned first, unsigned count) {
    std::int32_t valid = 0;
    for (unsigned i = first; i < first + count; ++i) {
        if (reachable(i)) ++valid;
        else memory_.write(candidates + i * candidate_stride + candidate_score, 0xffffffffu);
    }
    return valid;
}
std::int32_t Ai::validate_candidates_before_active() {
    const auto region = memory_.read(active_region);
    const auto through = signed32(memory_.read(before_region_first + region * 4)
                                + memory_.read(before_region_length + region * 4));
    return through <= 0 ? 0 : validate(0, unsigned(through));
}
std::int32_t Ai::validate_region_candidates(unsigned region) {
    const auto first = signed32(memory_.read(region_first + region * 4));
    const auto length = signed32(memory_.read(region_length + region * 4));
    return length <= 0 ? 0 : validate(unsigned(first), unsigned(length));
}
unsigned Ai::best(unsigned first, unsigned last) const {
    // The running maximum starts at the first candidate of the whole list, not
    // at `first`; a region that scores lower than it never replaces index zero.
    auto highest = score(0);
    unsigned chosen = 0;
    for (unsigned i = first; i < last; ++i)
        if (score(i) > highest) { chosen = i; highest = score(i); }
    return chosen;
}
unsigned Ai::best_candidate_in_regions(unsigned region) const {
    const auto through = signed32(memory_.read(region_length + region * 4)
                                + memory_.read(region_first + region * 4));
    return through <= 0 ? 0 : best(0, unsigned(through));
}
unsigned Ai::best_candidate_in_region(unsigned region) const {
    const auto first = signed32(memory_.read(region_first + region * 4));
    const auto length = signed32(memory_.read(region_length + region * 4));
    return length <= 0 ? 0 : best(unsigned(first), unsigned(first + length));
}
unsigned Ai::best_candidate() const {
    const auto count = signed32(memory_.read(candidate_count));
    return count <= 0 ? 0 : best(0, unsigned(count));
}

bool Ai::refresh_hurt_ratios(unsigned enemy_slot) {
    bool asked_for = false;
    const auto enemies = signed32(memory_.read(0x776484));
    for (std::int32_t slot = 0; slot < enemies; ++slot) {
        const auto record = BattleRules::enemy_record(unsigned(slot));
        const auto vitality = signed32(memory_.read(record + 20));
        const auto maximum = signed32(memory_.read(BattleRules::monster_record(memory_.read(record + 4)) + 24));
        const bool weighed = vitality > 0 && vitality < maximum
                          && !(memory_.read(record + 10, 1) & untargetable_icons);
        std::int32_t percent = 0;
        if (weighed) {
            // The original multiplies before dividing and wraps at 32 bits.
            percent = std::int32_t(std::uint32_t(vitality) * 100u) / maximum;
            if (percent < 1) percent = 1;
            memory_.write(hurt_count, memory_.read(hurt_count) + 1);
        }
        memory_.write(hurt_ratio + std::uint32_t(slot) * 4, std::uint32_t(percent));
        if (unsigned(slot) == enemy_slot) asked_for = weighed;
    }
    return asked_for;
}
std::optional<unsigned> Ai::priority_skill(unsigned monster_id) const {
    const auto count = signed32(memory_.read(skill_slot_count));
    for (std::int32_t i = 0; i < count; ++i) {
        const auto slot = memory_.read(skill_slots + std::uint32_t(i) * 4);
        const auto action = memory_.read(monster_actions + (slot + monster_id * monster_action_count) * 4);
        if (memory_.read(action_records + action * action_stride, 1) & 1) return unsigned(i);
    }
    return std::nullopt;
}
void Ai::begin_action(Address actor, std::int32_t action) {
    memory_.write(action_request, std::uint32_t(action));
    if (action < 0) return;
    memory_.write(actor + actor_offset::path_cursor, 0);
    memory_.write(actor + 0x2c, 1);
    memory_.write(actor + actor_offset::callback, enemy_action_runner);
    memory_.write(actor + actor_offset::flags,
                  (memory_.read(actor + actor_offset::flags) & ~0x4000u) | 0x10000u);
    memory_.write(action_step, 0);
    memory_.write(action_phase, 3);
}
void Ai::select_region_count(unsigned handler) {
    const auto row = memory_.read(handler_rows + handler * handler_row_stride);
    memory_.write(region_count, row & 1 ? 1u : 4u);
}
void Ai::clear_line_effects() {
    const auto cells = memory_.span(line_effects, line_effect_bytes);
    std::fill(cells.begin(), cells.end(), std::uint8_t(0));
}

unsigned Ai::collect_tiles(bool both_choices) {
    const auto width = memory_.read(globals::grid_row_stride);
    const auto cells = signed32(width * memory_.read(globals::grid_height));
    const std::uint32_t accepted = both_choices ? 3u : 1u;
    unsigned found = 0;
    memory_.write(tile_record_count, 0);
    for (std::int32_t cell = 0; cell < cells; ++cell) {
        if ((memory_.read(tile_choice + std::uint32_t(cell), 1) & accepted) != 1) continue;
        if (found >= tile_record_capacity) throw Fault(tile_records, "battle AI candidate tile capacity exceeded");
        const auto record = tile_records + found * tile_record_stride;
        memory_.write(record, std::uint32_t(cell) % width);
        memory_.write(record + 4, std::uint32_t(cell) / width);
        for (unsigned i = 0; i < 16; ++i) memory_.write(record + 8 + i, 0, 1);
        memory_.write(tile_record_count, ++found);
    }
    return found;
}
unsigned Ai::prepare_action_offsets(unsigned handler) {
    BattleRules(memory_).prepare_handler(handler, 0);
    unsigned count = 0;
    for (unsigned rotation = 0; rotation < rotations; ++rotation)
        count = mask_offsets(action_mask + rotation * action_mask_stride,
                             rotation_offsets + rotation * rotation_offset_stride,
                             rotation_offset_count);
    return count;
}
std::int32_t Ai::target_weight(Address candidate) {
    return party_target_weight(memory_.read(candidate));
}
std::int32_t Ai::party_target_weight(unsigned slot) {
    const auto status = memory_.read(BattleRules::party_record(
        memory_.read(globals::party_actor_ids + slot * 4)) + 8);
    if (status & 0x600000) return 0;                       // Not a target at all.
    if (!(status & 0x600a00)) return 3;                    // Fully able: worth the most.
    // Impaired: worth two or three, and the roll is consumed only here.
    return std::int32_t(crt_rand(memory_)) % 3 + 2;
}
std::int32_t Ai::tile_distance(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) {
    const auto dx = std::uint32_t(ax) - std::uint32_t(bx);
    const auto dy = std::uint32_t(ay) - std::uint32_t(by);
    return signed32(signed_magnitude(dx) + signed_magnitude(dy));
}
unsigned Ai::step_direction(std::int32_t dx, std::int32_t dy) {
    // The original compares the two magnitudes as signed words, so an INT_MIN
    // offset reads as smaller than anything rather than larger.
    if (signed32(signed_magnitude(std::uint32_t(dx))) > signed32(signed_magnitude(std::uint32_t(dy))))
        return dx > 0 ? 3u : 2u;
    return dy > 0 ? 1u : 0u;
}
unsigned Ai::mask_offsets(Address mask, Address out_pairs, Address out_count) {
    const auto found = extract_mask_offsets(
        [&](unsigned index) { return memory_.read(mask + index, 1); },
        [&](unsigned index, std::uint32_t value) { memory_.write(out_pairs + index * 4, value); });
    memory_.write(out_count, found);
    return found;
}

unsigned Ai::rasterize_regions(std::optional<unsigned> region) {
    BattleRules(memory_).rasterize_ai_regions(region);
    const auto found = scan_spans(signed32(memory_.read(globals::grid_row_stride)),
                                  signed32(memory_.read(globals::grid_height)),
                                  reach_overlay, 0, 0, span_count, spans, 1);
    // The original asserts here rather than writing past its span table.
    if (found >= std::int32_t(span_capacity)) throw Fault(spans, "original AI map segment capacity exceeded");
    return unsigned(found);
}
void Ai::build_skill_list(unsigned monster_id, unsigned enemy_slot) {
    const auto monster = monster_id * 124;
    const auto resource = signed32(memory_.read(BattleRules::enemy_record(enemy_slot) + 0x18));
    const auto skills = signed32(memory_.read(monster_skill_count + monster));
    memory_.write(skill_slot_count, 0);
    memory_.write(skill_weight_total, 0);
    for (std::int32_t i = 0; i < skills; ++i) {
        const auto action = signed32(memory_.read(monster_actions + monster + std::uint32_t(i) * 4));
        if (action < 0) throw Fault(monster_actions, "original enemy skill table holds a negative action");
        if (signed32(memory_.read(action_cost + std::uint32_t(action) * action_stride)) > resource) continue;
        const auto listed = memory_.read(skill_slot_count);
        memory_.write(skill_slots + listed * 4, std::uint32_t(i));
        const auto weight = signed32(memory_.read(monster_skill_weights + monster + std::uint32_t(i) * 4));
        if (weight > 0) {
            memory_.write(skill_weight_total, memory_.read(skill_weight_total) + std::uint32_t(weight));
            memory_.write(skill_weights + listed * 4, memory_.read(skill_weight_total));
        } else memory_.write(skill_weights + listed * 4, 0);
        memory_.write(skill_slot_count, listed + 1);
    }
}
void Ai::register_scan_regions() {
    const auto register_all = [&](Address count_at, Address mask, Address out_count,
                                  Address out_spans, unsigned bit) {
        const auto regions = signed32(memory_.read(count_at));
        for (std::int32_t r = 0; r < regions; ++r) {
            const auto found = scan_spans(11, 11, mask + std::uint32_t(r) * action_mask_stride, 5, 5,
                                          out_count + std::uint32_t(r) * 4,
                                          out_spans + std::uint32_t(r) * region_span_stride, bit);
            // The original asserts rather than writing past one region's spans.
            if (found > std::int32_t(region_span_capacity))
                throw Fault(out_spans, "original AI footprint span capacity exceeded");
        }
    };
    register_all(move_regions, action_mask, move_span_count, move_spans, 4);
    register_all(region_count, target_mask, region_segments, region_spans, 2);
}
void Ai::build_movement_overlay(unsigned side) {
    const auto width = memory_.read(globals::grid_row_stride);
    const auto cells = signed32(width * memory_.read(globals::grid_height));
    for (std::int32_t i = 0; i < cells; ++i)
        memory_.write(tile_choice + std::uint32_t(i), memory_.read(0x7764e0 + std::uint32_t(i) * 4, 1), 1);
    // Around each combatant that can still act, keep only the move bit, so the
    // AI cannot path straight through the tiles next to it.
    const auto isolate = [&](std::uint32_t cell) {
        for (const auto neighbour : {cell - width, cell + width, cell - 1, cell + 1}) {
            const auto at = tile_choice + neighbour;
            memory_.write(at, memory_.read(at, 1) & 2u, 1);
        }
    };
    const auto blocked = 0x600a00u;
    if (side == 1) {
        const auto slots = signed32(memory_.read(globals::party_count));
        for (std::int32_t i = 0; i < slots; ++i) {
            const auto actor = Actors::slot(unsigned(i));
            const auto record = BattleRules::party_record(memory_.read(globals::party_actor_ids + std::uint32_t(i) * 4));
            if (memory_.read(record + 8) & blocked) continue;
            isolate(memory_.read(actor + actor_offset::tile_y) * width + memory_.read(actor + actor_offset::tile_x));
        }
    } else if (side == 2) {
        const auto enemies = signed32(memory_.read(0x776484));
        for (std::int32_t i = 0; i < enemies; ++i) {
            const auto record = BattleRules::enemy_record(unsigned(i));
            const auto actor = Actors::slot(memory_.read(record));
            if (memory_.read(record + 8) & blocked) continue;
            isolate(memory_.read(actor + actor_offset::tile_y) * width + memory_.read(actor + actor_offset::tile_x));
        }
    }
}
std::int32_t Ai::scan_spans(std::int32_t width, std::int32_t height, Address grid,
                            std::int32_t origin_x, std::int32_t origin_y,
                            Address out_count, Address out_spans, unsigned bit) {
    const auto found = extract_mask_spans(width, height, origin_x, origin_y, bit,
        [&](unsigned index) { return memory_.read(grid + index, 1); },
        [&](unsigned index, std::uint32_t value) { memory_.write(out_spans + index * 4, value); });
    memory_.write(out_count, std::uint32_t(found));
    return found;
}

namespace {
// Weight a party member contributes at each distance, nearest first.
constexpr std::int32_t near_curve[] = {10, 9, 8, 7, 6, 5, 4};
constexpr std::int32_t wide_curve[] = {0, 2, 2, 3, 4, 5, 8, 9, 10, 12, 14, 0};
} // namespace
unsigned Ai::collect_region(std::int32_t target_x, std::int32_t target_y, unsigned region, unsigned first) {
    const auto span_base = region_spans + region * region_span_stride;
    const auto segments = signed32(memory_.read(region_segments + region * 4));
    // The original reads the row of this region's last span from one entry below
    // the table base; with no segments the value cannot change the outcome.
    const auto last_row = signed32(memory_.read(span_base + std::uint32_t(segments) * 12 - 12));
    memory_.write(region_length + region * 4, 0);
    const auto tiles = signed32(memory_.read(tile_record_count));
    unsigned added = 0;
    for (std::int32_t tile = 0; tile < tiles; ++tile) {
        const auto record = tile_records + std::uint32_t(tile) * tile_record_stride;
        const auto x = signed32(memory_.read(record)), y = signed32(memory_.read(record + 4));
        // Reject on the footprint's own bounding box before walking its spans.
        if (y + signed32(memory_.read(span_base)) > target_y) continue;
        if (target_y > y + last_row) continue;
        if (x - candidate_reach > target_x || target_x > x + candidate_reach) continue;
        bool covers = false;
        for (std::int32_t j = 0; j < segments && !covers; ++j) {
            const auto span = span_base + std::uint32_t(j) * 12;
            if (y + signed32(memory_.read(span)) != target_y) continue;
            if (x + signed32(memory_.read(span + 4)) > target_x) continue;
            covers = target_x <= x + signed32(memory_.read(span + 8));
        }
        if (!covers) continue;
        const auto candidate = candidates + (first + added) * candidate_stride;
        memory_.write(candidate + candidate_x, std::uint32_t(x));
        memory_.write(candidate + candidate_y, std::uint32_t(y));
        memory_.write(candidate + candidate_region, region);
        memory_.write(candidate + candidate_score, 1);
        memory_.write(region_length + region * 4, memory_.read(region_length + region * 4) + 1);
        ++added;
    }
    return added;
}
unsigned Ai::collect_reaching_candidates(std::int32_t target_x, std::int32_t target_y) {
    unsigned found = 0;
    const auto regions = signed32(memory_.read(region_count));
    for (std::int32_t region = 0; region < regions; ++region) {
        memory_.write(region_first + std::uint32_t(region) * 4, found);
        found += collect_region(target_x, target_y, unsigned(region), found);
    }
    return found;
}
unsigned Ai::collect_region_candidates(std::int32_t target_x, std::int32_t target_y, unsigned region) {
    memory_.write(region_first + region * 4, 0);
    return collect_region(target_x, target_y, region, 0);
}
bool Ai::press_weakened_enemies(bool count_nearby_party) {
    // One roll for the whole call, so every enemy is judged against the same bar.
    const auto threshold = 50 - std::int32_t(crt_rand(memory_)) % 20;
    constexpr std::int32_t crowding_range = 6, crowding_charge = 4;
    bool worth_finishing = false;
    const auto enemies = signed32(memory_.read(0x776484));
    const auto slots = signed32(memory_.read(globals::party_count));
    for (std::int32_t i = 0; i < enemies; ++i) {
        const auto ratio_at = hurt_ratio + std::uint32_t(i) * 4;
        const auto actor = Actors::slot(memory_.read(BattleRules::enemy_record(unsigned(i))));
        const auto x = signed32(memory_.read(actor + actor_offset::tile_x));
        const auto y = signed32(memory_.read(actor + actor_offset::tile_y));
        if (count_nearby_party) {
            std::int32_t charge = 0;
            for (std::int32_t j = 0; j < slots; ++j) {
                const auto record = BattleRules::party_record(memory_.read(globals::party_actor_ids + std::uint32_t(j) * 4));
                if (memory_.read(record + 8) & 0x600a00u) continue;
                const auto member = Actors::slot(unsigned(j));
                if (tile_distance(x, y, signed32(memory_.read(member + actor_offset::tile_x)),
                                        signed32(memory_.read(member + actor_offset::tile_y))) < crowding_range)
                    charge += crowding_charge;
            }
            const auto ratio = signed32(memory_.read(ratio_at));
            // Only an enemy that still has a percentage is charged, and it never
            // drops below one.
            if (ratio > 0) memory_.write(ratio_at, std::uint32_t(std::max(ratio - charge, 1)));
        }
        const auto ratio = signed32(memory_.read(ratio_at));
        if (ratio > 0 && ratio < threshold) worth_finishing = true;
    }
    return worth_finishing;
}
std::int32_t Ai::crowding_near(std::int32_t x, std::int32_t y) {
    std::int32_t total = 0;
    const auto slots = signed32(memory_.read(globals::party_count));
    for (std::int32_t i = 0; i < slots; ++i) {
        const auto record = BattleRules::party_record(memory_.read(globals::party_actor_ids + std::uint32_t(i) * 4));
        if (memory_.read(record + 8) & 0x600a00u) continue;
        const auto actor = Actors::slot(unsigned(i));
        const auto distance = tile_distance(x, y, signed32(memory_.read(actor + actor_offset::tile_x)),
                                                  signed32(memory_.read(actor + actor_offset::tile_y)));
        // Past the curve the original spends a roll instead of a fixed weight.
        total += distance < std::int32_t(std::size(near_curve)) ? near_curve[distance]
                                                                : std::int32_t(crt_rand(memory_)) % 2;
    }
    return total;
}
std::int32_t Ai::crowding_wide(std::int32_t x, std::int32_t y) {
    std::int32_t total = 0;
    const auto slots = signed32(memory_.read(globals::party_count));
    for (std::int32_t i = 0; i < slots; ++i) {
        const auto record = BattleRules::party_record(memory_.read(globals::party_actor_ids + std::uint32_t(i) * 4));
        if (memory_.read(record + 8) & 0x600a00u) continue;
        const auto actor = Actors::slot(unsigned(i));
        const auto distance = tile_distance(x, y, signed32(memory_.read(actor + actor_offset::tile_x)),
                                                  signed32(memory_.read(actor + actor_offset::tile_y)));
        total += distance < std::int32_t(std::size(wide_curve)) ? wide_curve[distance] : 14;
    }
    return total;
}
std::int32_t Ai::bias_score(Memory& memory) { return std::int32_t(crt_rand(memory)) % 2; }
std::int32_t Ai::move_score(Memory& memory) { return std::int32_t(crt_rand(memory)) % 30; }
std::int32_t Ai::tier_weight(unsigned tier) {
    constexpr std::int32_t weights[] = {12, 10, 6};
    if (tier >= 3) throw Fault(tier, "battle AI pressure tier outside its table");
    return weights[tier];
}
std::int32_t Ai::pressure_scaled(std::int32_t hurt, unsigned tier) {
    return (tier_weight(tier) + 10) * 3 - hurt / 5;
}
std::int32_t Ai::pressure_flat(std::int32_t hurt, unsigned tier) {
    return tier_weight(tier) - hurt / 5 + 50;
}
std::int32_t Ai::pressure_steep(std::int32_t hurt, unsigned tier) {
    return tier_weight(tier) - hurt / 5 * 3 + 70;
}
} // namespace fsb::core::combat
