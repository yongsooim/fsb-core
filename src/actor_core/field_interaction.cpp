#include "fsb_core/actor_core/field_interaction.hpp"
#include "fsb_core/actor_core/item_menu.hpp"
#include "fsb_core/actor_core/actor_runtime.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
// How each facing moves through the cell grid, in facing order.
std::int32_t cell_step(const Memory& memory, unsigned facing) {
    const auto stride = signed32(memory.read(globals::grid_row_stride));
    switch (facing) {
    case 0: return -stride;
    case 1: return stride;
    case 2: return -1;
    default: return 1;
    }
}
std::uint32_t occupancy(const Memory& memory, std::int32_t layer, std::int32_t cell) {
    return memory.read(tile_occupancy +
                       (std::uint32_t(layer) * occupancy_cells_per_layer + std::uint32_t(cell)) * 4);
}
// True when the actor may cross this tile's edge in the direction it faces.
bool edge_open(const Memory& memory, std::int32_t cell, Address plane, std::uint32_t mask) {
    return (memory.read(plane + std::uint32_t(cell) * 4) & mask) == 0;
}
Address actor_slot(std::uint32_t index) { return globals::actor_objects + index * layout::actor_size; }
} // namespace

TileProbe probe_faced_tile(const Memory& memory, const GuestWords& words, Address actor) {
    const auto facing = words.read(actor + actor_offset::facing);
    if (facing >= 4) return {};
    const auto layer = signed32(words.read(actor + actor_offset::layer_q16)) / 0x10000;
    const auto cell = signed32(words.read(actor + actor_offset::tile_y)) *
                          signed32(memory.read(globals::grid_row_stride)) +
                      signed32(words.read(actor + actor_offset::tile_x));
    const auto plane = globals::tile_attributes +
                       memory.read(current_grid_id) * tile_attribute_plane_bytes;
    const auto mask = edge_blocked[facing];
    const auto step = cell_step(memory, facing);
    const auto ahead = cell + step;
    if ((occupancy(memory, layer, ahead) & occupancy_occupied) &&
        edge_open(memory, cell, plane, mask))
        return {ProbeDistance::Adjacent,
                std::int32_t(occupancy(memory, layer, ahead) & occupancy_object_mask)};
    // Nothing immediately ahead: look one further, but only through two open
    // edges.
    const auto beyond = ahead + step;
    if ((occupancy(memory, layer, beyond) & occupancy_occupied) &&
        edge_open(memory, cell, plane, mask) && edge_open(memory, ahead, plane, mask))
        return {ProbeDistance::TwoAway,
                std::int32_t(occupancy(memory, layer, beyond) & occupancy_object_mask)};
    return {};
}

std::uint32_t probe_adjacent_trigger(Memory& memory, const GuestWords& words, Address actor,
                                     const FieldInteractionHooks& hooks) {
    const auto facing = words.read(actor + actor_offset::facing);
    const auto probe = probe_faced_tile(memory, words, actor);
    const auto distance = std::uint32_t(std::int32_t(probe.distance));
    // A negative id is the probe's own "nothing here" answer.
    if (probe.object < 0) return distance;
    const auto message = memory.read(facing_dialogue_message + (facing & 0xff) * 4);
    if (std::uint32_t(probe.object) < interaction_actor_limit) {
        // A live actor. Either distance can start a conversation, but only once
        // per press.
        const auto target = actor_slot(std::uint32_t(probe.object));
        if (words.read(target + actor_offset::flags) & dialogue_answered_bit) return distance;
        std::int32_t actor_type = -1;
        const auto started = hooks.start_dialogue
                                 ? hooks.start_dialogue(words.read(target + actor_offset::template_link),
                                                        message, actor_type)
                                 : 0;
        if (!started) return 0;
        words.write(target + actor_offset::flags,
                    words.read(target + actor_offset::flags) | dialogue_answered_bit);
        memory.write(field_event_pending, 1);
        if (actor_type < 0 || actor_type > dialogue_actor_type_limit) return started;
        // A typed speaker also strikes a pose: its live motion lanes go into
        // the pending-move save block so the dialogue can put them back.
        words.write(target + pending_move_marker, words.read(target + actor_offset::motion_state));
        words.write(target + pending_move_facing, words.read(target + actor_offset::facing));
        words.write(target + pending_move_target_facing, words.read(target + actor_offset::target_facing));
        words.write(target + pending_move_frame, words.read(target + actor_offset::motion_frame));
        words.write(target + actor_offset::motion_state, dialogue_pose_state);
        words.write(target + actor_offset::target_facing, std::uint32_t(actor_type));
        return target + actor_offset::motion_state;
    }
    // A map object. Only the tile immediately ahead can be picked up from.
    if (probe.distance != ProbeDistance::Adjacent) return distance;
    std::int32_t item = -1, quantity = -1;
    if (hooks.collect_sparkle) hooks.collect_sparkle(std::uint32_t(probe.object), item, quantity);
    if (item <= 0) return std::uint32_t(item);
    std::int32_t unused_facing = -1;
    // The item and how many of it ride in the upper bytes of the message.
    const auto payload = message | (std::uint32_t((item << 8) | quantity) << 8);
    const auto started = hooks.start_dialogue
                             ? hooks.start_dialogue(0xffffffffu, payload, unused_facing)
                             : 0;
    if (hooks.play_pickup_cue) hooks.play_pickup_cue();
    const auto owned = owned_item_counts + std::uint32_t(item) * 4;
    memory.write(owned, memory.read(owned) + std::uint32_t(quantity));
    if (started) memory.write(field_event_pending, 1);
    return owned;
}
} // namespace fsb::core::actor_core
