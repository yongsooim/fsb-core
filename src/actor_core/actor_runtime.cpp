#include "fsb_core/actor_core/actor_runtime.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
constexpr Address event_flag_table = 0x5b356c;
constexpr unsigned event_flag_record_bytes = 0x44;
constexpr Address tileset_slot_table = 0x804cc8;
constexpr unsigned tileset_slot_bytes = 0x44;
constexpr unsigned tileset_slots = 0x20;
constexpr Address battle_track_word_a = 0x85712c;
constexpr Address battle_track_word_b = 0x857270;
constexpr Address gatewarp_object_records = 0x8021e0;
constexpr unsigned gatewarp_record_bytes = 0xc;
constexpr unsigned gatewarp_records = 2;
constexpr unsigned gatewarp_objects_per_record = 2;
// 458da9 marks "no special transition pending" in three places at once.
constexpr Address special_transition_markers[] = {0x5d0768, 0x8021a0, 0x8021b0};
// 45d6ab clears the input-hold bit and the two menu-cursor slots on the way
// out. The original touches byte +7 of the record, which is bit30 of the same
// flag word every other actor routine addresses as a dword.
constexpr std::uint32_t input_hold_bit = 0x40000000;
constexpr Address menu_cursor_flag = 0x802c9c; // Byte-wide in the original.
constexpr Address menu_cursor_index = 0x802ca0;

Address actor_slot(unsigned index) { return globals::actor_objects + index * layout::actor_size; }
} // namespace

void set_visual_control_bits(const GuestWords& words, Address object) {
    const auto at = object + actor_offset::flags;
    words.write(at, words.read(at) | visual_control_bits);
}

void clear_visual_control_bits(const GuestWords& words, Address object) {
    const auto at = object + actor_offset::flags;
    words.write(at, words.read(at) & ~visual_control_bits);
}

void save_motion_block(const GuestWords& words, Address object) {
    for (unsigned at = 0; at < motion_block_bytes; at += 4)
        words.write(object + motion_block_backup + at, words.read(object + motion_block_offset + at));
}

void restore_motion_block(const GuestWords& words, Address object) {
    for (unsigned at = 0; at < motion_block_bytes; at += 4)
        words.write(object + motion_block_offset + at, words.read(object + motion_block_backup + at));
}

void clear_pending_move_target(const GuestWords& words, Memory& memory,
                               std::uint32_t selector, std::uint32_t facing,
                               std::uint32_t menu, const OpenItemMenu& open_menu) {
    // This one reaches the chain walk directly rather than through42feb9.
    const auto object = resolve_slot(memory, selector);
    if (signed32(facing) > -1) {
        if (words.read(object + actor_offset::facing) != facing) {
            if (words.read(object + pending_move_marker) == pending_move_walking) {
                // A walk was in flight: drop it and the pose it saved.
                words.write(object + actor_offset::motion_state, 0);
                for (auto at : {pending_move_marker, pending_move_facing,
                                pending_move_frame, pending_move_target_facing})
                    words.write(object + at, 0);
            } else {
                // Turn toward the requested facing instead of stepping.
                words.write(object + actor_offset::motion_state, 0xc);
                words.write(object + actor_offset::target_facing, facing);
            }
        } else {
            const auto saved = words.read(object + pending_move_marker);
            if (saved != pending_move_walking) {
                words.write(object + actor_offset::motion_state, saved);
                words.write(object + actor_offset::facing, words.read(object + pending_move_facing));
                words.write(object + actor_offset::motion_frame, words.read(object + pending_move_frame));
                words.write(object + actor_offset::target_facing,
                            words.read(object + pending_move_target_facing));
            }
            words.write(object + pending_move_marker, 0);
        }
        if (signed32(menu) > -1 && open_menu) open_menu(menu);
    }
    const auto flags = object + actor_offset::flags;
    words.write(flags, words.read(flags) & ~input_hold_bit);
    memory.write(menu_cursor_flag, 0, 1);
    memory.write(menu_cursor_index, 0);
}

void clear_all_pending_moves(Memory& memory) {
    for (unsigned slot = 0; slot < actor_pool_slots; ++slot)
        for (auto at : {pending_move_marker, pending_move_facing,
                        pending_move_frame, pending_move_target_facing})
            memory.write(actor_slot(slot) + at, 0);
}

void reset_party_slots(Memory& memory) {
    memory.write(globals::party_count, 0);
    for (unsigned slot = 0; slot < 10; ++slot)
        memory.write(globals::party_actor_ids + slot * 4, 0xffffffffu);
}

void release_unfocused_party_visuals(Memory& memory) {
    const auto count = signed32(memory.read(globals::party_count));
    const auto focused = memory.read(globals::active_party_index);
    for (std::int32_t slot = 0; slot < count; ++slot) {
        if (std::uint32_t(slot) == focused) continue;
        const auto at = actor_slot(unsigned(slot)) + actor_offset::flags;
        memory.write(at, memory.read(at, 1) & ~std::uint32_t(visual_control_bits), 1);
    }
}

void clear_tileset_slot_heads(Memory& memory) {
    for (unsigned slot = 0; slot < tileset_slots; ++slot)
        memory.write(tileset_slot_table + slot * tileset_slot_bytes, 0);
}

void reset_battle_track_state(Memory& memory) {
    memory.write(battle_track_word_a, 0);
    memory.write(battle_track_word_b, 0);
}

void release_gatewarp_objects(Memory& memory, const FinalizeObject& finalize) {
    for (unsigned record = 0; record < gatewarp_records; ++record)
        for (unsigned index = 0; index < gatewarp_objects_per_record; ++index) {
            const auto at = gatewarp_object_records + record * gatewarp_record_bytes + index * 4;
            if (const auto object = memory.read(at); object && finalize) finalize(object);
            memory.write(at, 0);
        }
}

void prepare_special_map_transition(Memory& memory, const FinalizeObject& finalize) {
    release_gatewarp_objects(memory, finalize);
    for (auto marker : special_transition_markers)
        memory.write(marker, memory.read(marker) | 0xffffffffu);
}

std::uint32_t event_flag_word(const Memory& memory, std::uint32_t index) {
    return memory.read(event_flag_table + index * event_flag_record_bytes);
}

void set_event_flag_bit(Memory& memory, std::uint32_t index, std::uint32_t bit) {
    const auto at = event_flag_table + index * event_flag_record_bytes;
    memory.write(at, memory.read(at) | (1u << (bit & 31)));
}

void show_party_panel(Memory& memory, std::uint32_t panel) {
    memory.write(globals::party_panel_visible_mask,
                 memory.read(globals::party_panel_visible_mask) | (1u << (panel & 31)));
}

void request_slot_map_transition(Memory& memory, std::uint32_t save_slot) {
    memory.write(0x803a48, 0xffffffffu); // No direct map target.
    memory.write(0x803a4c, 1);           // Begin the fade out.
    memory.write(0x5d2498, save_slot);
}

std::uint32_t compare_three_way(std::int32_t left, std::int32_t right) {
    if (left < right) return 0;
    return left == right ? 2 : 1;
}

std::uint32_t worldmap_mode_for_selector(std::uint32_t selector) {
    constexpr std::uint32_t modes[worldmap_mode_selectors] = {0, 1, 2, 5, 4, 9, 0xb, 0xa, 0xd, 0xe, 8, 0xc};
    if (selector >= worldmap_mode_selectors) throw Fault(0x431749, "worldmap mode selector outside table");
    return modes[selector];
}
} // namespace fsb::core::actor_core
