#pragma once
#include "fsb_core/primitives.hpp"

namespace fsb::core::combat {
namespace ai {
// Candidate tiles the enemy AI is scoring this turn. One record per tile.
inline constexpr Address candidates = 0x7824c0, candidate_stride = 16;
inline constexpr Address candidate_x = 0, candidate_y = 4, candidate_score = 0xc;
// Candidates are grouped into regions; each region owns a contiguous run.
inline constexpr Address region_first = 0x7824b0, region_length = 0x781260;
// 452372 reads both tables one entry lower on purpose: that entry holds the end
// of everything before the active region, which is what it wants to validate.
inline constexpr Address before_region_first = 0x7824ac, before_region_length = 0x78125c;
inline constexpr Address active_region = 0x780250, candidate_count = 0x77fffc;
// Byte overlay marking which map cells this turn's action can reach.
inline constexpr Address reach_overlay = 0x780260;
// Per-enemy remaining-vitality percentage and how many enemies produced one.
inline constexpr Address hurt_ratio = 0x7865d0, hurt_count = 0x78739c;
// Skill slots collected for the acting monster, and the action definitions.
inline constexpr Address skill_slots = 0x780240, skill_slot_count = 0x780254;
inline constexpr Address action_records = 0x610c14, action_stride = 32;
inline constexpr Address monster_actions = 0x609ce8, monster_action_count = 0x1f;
// Region count the reachable-tile raster uses, chosen from the handler row.
inline constexpr Address region_count = 0x77fff8;
inline constexpr Address handler_rows = 0x5c208c, handler_row_stride = 24;
inline constexpr Address line_effects = 0x7873d8;
// Tiles the AI may consider this turn, one byte each, and the records it
// builds from them.
inline constexpr Address tile_choice = 0x7814b0;
inline constexpr Address tile_records = 0x786620, tile_record_stride = 24;
inline constexpr Address tile_record_count = 0x787398;
// The records run up to the counter itself. The original does not check, and a
// 144th record would start overwriting the counter and the vitality totals
// behind it, so the reconstruction stops instead of corrupting them.
inline constexpr unsigned tile_record_capacity = (tile_record_count - tile_records) / tile_record_stride;
// Per-rotation offset lists 4529c2 builds from the published action footprint.
inline constexpr Address rotation_offsets = 0x77ece8, rotation_offset_stride = 0x3c8;
inline constexpr Address rotation_offset_count = 0x77ffe4;
inline constexpr Address action_mask = 0x77fc18, action_mask_stride = 0x79;
inline constexpr unsigned rotations = 4;
// Row spans the reach overlay is republished as, and the original's own limit.
inline constexpr Address spans = 0x7870b8, span_count = 0x78025c;
// Move footprints and their spans, registered per handler slot.
inline constexpr Address move_regions = 0x780250, move_span_count = 0x77ffe8;
inline constexpr Address move_spans = 0x781270, region_span_stride = 0x90;
inline constexpr Address target_mask = 0x77fe00, region_spans = 0x780000;
inline constexpr Address region_segments = 0x7865c0;
inline constexpr Address candidate_region = 8;
// The original bounds a candidate's reach at five tiles either side before it
// walks the region's spans.
inline constexpr std::int32_t candidate_reach = 5;
inline constexpr unsigned region_span_capacity = 12;
// Skill weights the AI draws from, and the resource each skill costs.
inline constexpr Address skill_weight_total = 0x780258, skill_weights = 0x787388;
inline constexpr Address monster_skill_count = 0x609ce4, monster_skill_weights = 0x609cf8;
inline constexpr Address action_cost = 0x610c18;
inline constexpr unsigned span_capacity = 0x3c;
inline constexpr unsigned line_effect_bytes = 132;
} // namespace ai

// One span of set cells in a byte grid row, in the caller's own origin.
struct GridSpan { std::int32_t row, first, last; };

// Enemy turn scoring and the candidate bookkeeping around it. All state stays
// in the original battle tables; this owns no copy of it.
class Ai {
public:
    explicit Ai(Memory& memory) : memory_(memory) {}

    // 452372/4523b9: drop candidates whose tile is not reachable and report how
    // many survive. 452372 covers everything before the active region.
    std::int32_t validate_candidates_before_active();
    std::int32_t validate_region_candidates(unsigned region);
    // 45240c/45244d/453273: index of the best-scoring candidate. All three start
    // from the first candidate's score, so a region whose own scores are lower
    // reports index zero rather than its own first entry.
    unsigned best_candidate_in_regions(unsigned region) const;
    unsigned best_candidate_in_region(unsigned region) const;
    unsigned best_candidate() const;

    // 452c4d: publish each enemy's remaining vitality as a percentage, skipping
    // the downed, the untouched and the ones the AI may not target. Returns
    // whether the enemy the caller asked about produced a percentage.
    bool refresh_hurt_ratios(unsigned enemy_slot);
    // 452c0c: first collected skill slot whose action is flagged as priority.
    std::optional<unsigned> priority_skill(unsigned monster_id) const;
    // 452a00: hand the actor to the enemy action runner. A negative action id
    // only records the request.
    void begin_action(Address actor, std::int32_t action);
    // 451e1e: one or four regions, depending on the handler's own row.
    void select_region_count(unsigned handler);
    // 454220: clear the battle line-effect records.
    void clear_line_effects();
    // 452014: list the tiles this turn's action may reach. `both_choices`
    // widens the accepted marker from one value to two.
    unsigned collect_tiles(bool both_choices);
    // 4529c2: publish the handler's footprint, then turn each rotation into an
    // offset list. Returns the count of the last rotation, as the original does.
    unsigned prepare_action_offsets(unsigned handler);
    // 451b57: how much this candidate's occupant is worth as a target.
    std::int32_t target_weight(Address candidate);
    std::int32_t party_target_weight(unsigned slot);

    // 452d02/452db8: how crowded a tile is, summed over the party members that
    // can still act. Both weigh nearer members more; they differ in their curve
    // and in what they do past its end.
    std::int32_t crowding_near(std::int32_t x, std::int32_t y);
    std::int32_t crowding_wide(std::int32_t x, std::int32_t y);
    // 452497: for every registered region, keep the candidate tiles whose
    // footprint would cover the given target tile, and record which region each
    // came from. Returns how many candidates all regions produced together.
    unsigned collect_reaching_candidates(std::int32_t target_x, std::int32_t target_y);
    // 4525e0: the same test for one region on its own. It always writes from
    // the first candidate slot and reports that region's first entry as zero.
    unsigned collect_region_candidates(std::int32_t target_x, std::int32_t target_y, unsigned region);
    // 45316e: charge each enemy's remaining-vitality percentage for the party
    // members standing close to it, then report whether any enemy now looks
    // weak enough to finish. The threshold is rolled once per call.
    bool press_weakened_enemies(bool count_nearby_party);
    // 451b30: grid distance between two tiles.
    static std::int32_t tile_distance(std::int32_t ax, std::int32_t ay,
                                      std::int32_t bx, std::int32_t by);
    // 4532a5: which of the four steps a move offset points along.
    static unsigned step_direction(std::int32_t dx, std::int32_t dy);

    // 45297e: every offset of an 11x11 action mask that carries the target bit.
    unsigned mask_offsets(Address mask, Address out_pairs, Address out_count);
    // 452168/452255: stamp the action footprint of every candidate tile into
    // the reach overlay, then republish it as row spans. nullopt combines every
    // registered region; a value selects one. Returns the span count.
    //
    // The original stamps without clipping, so a footprint crossing the map edge
    // overwrites the AI metadata next to the overlay. This keeps the in-map
    // union and the original segment scan and drops the cells outside the map;
    // that choice predates this session and lives in Battle::service.
    unsigned rasterize_regions(std::optional<unsigned> region);
    // 452b60: list the acting monster's affordable skills with running weights.
    void build_skill_list(unsigned monster_id, unsigned enemy_slot);
    // 4520a0: turn every registered move and target footprint into row spans.
    void register_scan_regions();
    // 451f0d: copy the cursor overlay into the AI's own byte grid, then clear
    // everything but the move bit around each able combatant.
    void build_movement_overlay(unsigned side);
    // 451e3f: convert a byte grid into row spans.
    std::int32_t scan_spans(std::int32_t width, std::int32_t height, Address grid,
                            std::int32_t origin_x, std::int32_t origin_y,
                            Address out_count, Address out_spans, unsigned bit);

    // Enemy scoring curves. The original selects one of these through its AI
    // table; each combines the acting enemy's own pressure with a target's.
    static std::int32_t bias_score(Memory& memory);              // 452ce2
    static std::int32_t move_score(Memory& memory);              // 452cf2
    static std::int32_t pressure_scaled(std::int32_t hurt, unsigned tier);  // 452e79
    static std::int32_t pressure_flat(std::int32_t hurt, unsigned tier);    // 452eb4
    static std::int32_t pressure_steep(std::int32_t hurt, unsigned tier);   // 452eea

private:
    Memory& memory_;
    std::int32_t validate(unsigned first, unsigned count);
    unsigned best(unsigned first, unsigned last) const;
    std::int32_t score(unsigned index) const;
    bool reachable(unsigned index) const;
    static std::int32_t tier_weight(unsigned tier);
    unsigned collect_region(std::int32_t target_x, std::int32_t target_y, unsigned region, unsigned first);
};
} // namespace fsb::core::combat
