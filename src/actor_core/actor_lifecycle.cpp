#include "fsb_core/actor_core/actor_lifecycle.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/actor_core/actor_runtime.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
Address actor_slot(std::uint32_t index) { return globals::actor_objects + index * layout::actor_size; }

// 45d91d walks back to the chain entry through the record's own link word.
constexpr unsigned chain_link = 0x11c;
constexpr std::uint32_t script_slot_limit = 0x5a;
constexpr std::uint32_t finalized_marker = 0xfffffffeu;
constexpr std::uint32_t spawning_marker = 0xffffffffu;
// 45db85 parks its argument in the record's callback argument word.
constexpr unsigned callback_argument = 0x1a8;
// The item-use menu words 45e139 publishes.
constexpr Address item_menu_selection = 0x80384c;
constexpr Address item_menu_mode = 0x803828;
constexpr Address item_menu_frame_ready = 0x803850;
constexpr Address item_menu_target = 0x80382c;
constexpr std::uint32_t item_menu_game_mode = 5;
// 45f495/45f554 move this callback with field control.
constexpr std::uint32_t field_movement_callback = 0x458ed7;
constexpr std::uint32_t field_control_bits = 0x10080;
constexpr std::uint32_t elevated_bit = 0x10000;
} // namespace

unsigned live_party_count(const Memory& memory) {
    unsigned live = 0;
    for (unsigned slot = 0; slot < party_roster_slots; ++slot)
        if (character_active(memory, memory.read(party_roster + slot * 4))) ++live;
    if (!live) throw Fault(0x43070b, "no live party member on the roster");
    return live;
}

void rewind_party_iterator(Memory& memory) {
    const auto player = player_character(memory);
    unsigned slot = 0;
    while (slot < party_roster_slots && memory.read(party_roster + slot * 4) != player) ++slot;
    if (slot == party_roster_slots) throw Fault(0x43075b, "player is not on the party roster");
    memory.write(party_iterator_cursor, slot);
}

std::uint32_t next_live_party_member(Memory& memory) {
    auto cursor = signed32(memory.read(party_iterator_cursor));
    for (unsigned guard = 0; guard <= party_roster_slots; ++guard) {
        cursor %= std::int32_t(party_roster_slots);
        memory.write(party_iterator_cursor, std::uint32_t(cursor));
        const auto member = memory.read(party_roster + std::uint32_t(cursor) * 4);
        if (character_active(memory, member)) {
            memory.write(party_iterator_cursor, std::uint32_t(cursor) + 1);
            return member;
        }
        ++cursor;
    }
    // The original loops here forever; its callers size the walk with
    // live_party_count first, so reaching this is a broken roster.
    throw Fault(0x4307ac, "party iterator found no live member to advance to");
}

void hide_all_party(const Memory& memory, const GuestWords& words, const ResolveActor& resolve) {
    for (unsigned slot = 0; slot < party_roster_slots; ++slot) {
        const auto member = memory.read(party_roster + slot * 4);
        if (character_active(memory, member)) set_visible_by_selector(words, member, 0, resolve);
    }
}

void open_item_use_menu(Memory& memory, std::uint32_t mode) {
    memory.write(item_menu_selection, 0);
    memory.write(item_menu_mode, 0);
    memory.write(item_menu_frame_ready, 1);
    memory.write(item_menu_target, mode);
    memory.write(globals::game_mode, item_menu_game_mode);
}

void write_vector3(const GuestWords& words, Address out,
                   std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    words.write(out, x);
    words.write(out + 4, y);
    words.write(out + 8, z);
}

void finalize_object(Memory& memory, Address object, const InvokeActorCallback& invoke) {
    if (memory.read(object + actor_offset::callback_state) == finalized_marker) return;
    const auto identity = memory.read(object);
    if (signed32(identity) >= 0 && identity < script_slot_limit) {
        const auto link = signed32(memory.read(actor_slot(identity) + chain_link));
        if (link > 0 && link < std::int32_t(alias_entry_guard) &&
            memory.read(tables::actor_slot_ids + std::uint32_t(link) * 0x44) == identity) {
            memory.write(tables::actor_slot_ids + std::uint32_t(link) * 0x44, 0xffffffffu);
            memory.write(tables::actor_object_pointers + std::uint32_t(link) * 0x44, 0);
        }
    }
    if (const auto callback = memory.read(object + actor_offset::callback)) {
        memory.write(object + actor_offset::callback_state, finalized_marker);
        invoke(callback, object);
    }
    for (unsigned at = 0; at < layout::actor_size; at += 4) memory.write(object + at, 0);
    memory.write(object, identity);
}

void reset_slot_range(Memory& memory, std::uint32_t first, std::uint32_t end,
                      const InvokeActorCallback& invoke) {
    for (auto slot = signed32(first); slot < signed32(end); ++slot) {
        finalize_object(memory, actor_slot(std::uint32_t(slot)), invoke);
        memory.write(actor_slot(std::uint32_t(slot)), std::uint32_t(slot));
    }
    memory.write(globals::actor_active_count, first);
}

Address allocate_spare_slot(Memory& memory) {
    for (auto flags = spare_pool_first_flags; flags < spare_pool_flags_limit;
         flags += layout::actor_size)
        if (!(memory.read(flags) & slot_in_use_bits)) return flags - actor_offset::flags;
    return 0;
}

Address spawn_callback_object(Memory& memory, Address callback,
                              const InvokeActorCallback& invoke, const PoolExhausted& exhausted) {
    const auto object = allocate_spare_slot(memory);
    if (!object) {
        if (exhausted) exhausted();
        return 0;
    }
    const auto flags = object + actor_offset::flags;
    memory.write(flags, memory.read(flags) | 0x800);
    memory.write(object + actor_offset::callback_tick_count, 0);
    if (callback) {
        memory.write(flags, memory.read(flags) | elevated_bit);
        memory.write(object + actor_offset::callback, callback);
        memory.write(object + actor_offset::callback_state, spawning_marker);
        invoke(callback, object);
        memory.write(object + actor_offset::callback_state, 0);
    }
    return object;
}

void copy_render_state(const GuestWords& words, Address destination, Address source) {
    for (auto at : {pending_move_marker, pending_move_facing, pending_move_frame,
                    pending_move_target_facing})
        words.write(destination + at, words.read(source + at));
    // The original copies the0xc8-byte path command block a word at a time.
    for (unsigned at = actor_offset::path_commands; at < actor_offset::motion_state; at += 4)
        words.write(destination + at, words.read(source + at));
    for (auto at : {actor_offset::world_x, actor_offset::world_y, actor_offset::elevation,
                    actor_offset::tile_x_q16, actor_offset::tile_y_q16, actor_offset::layer_q16,
                    actor_offset::motion_state, actor_offset::motion_frame, actor_offset::frame_group,
                    actor_offset::facing, actor_offset::target_facing})
        words.write(destination + unsigned(at), words.read(source + unsigned(at)));
}

void init_global_motion_template(Memory& memory, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    const auto words = memory_words(memory);
    const auto record = global_motion_template;
    write_vector3(words, record + actor_offset::draw_offset_x, 0, 0, 0);
    // The original masks the sign bit out of the second axis before storing it.
    const auto masked = (y & 0xffff0000u) | (y & 0x8000u);
    write_vector3(words, record + actor_offset::tile_x_q16, x, masked, z);
    const auto pixels = tile_to_pixels(signed32(memory.read(record + actor_offset::tile_x_q16)),
                                       signed32(memory.read(record + actor_offset::tile_y_q16)));
    memory.write(record + actor_offset::world_x, std::uint32_t(pixels.x));
    memory.write(record + actor_offset::world_y, std::uint32_t(pixels.y));
    memory.write(record + actor_offset::template_link, 0xffffffffu);
    for (auto at : {pending_move_marker, pending_move_facing, pending_move_frame,
                    pending_move_target_facing, unsigned(actor_offset::motion_frame),
                    unsigned(actor_offset::frame_group), unsigned(actor_offset::facing),
                    unsigned(actor_offset::target_facing), unsigned(actor_offset::screen_anchor_x),
                    unsigned(actor_offset::screen_anchor_y), 0x118u,
                    unsigned(actor_offset::tile_x), unsigned(actor_offset::tile_y)})
        memory.write(record + at, 0);
    memory.write(record + actor_offset::flags, 0x200180c0);
    memory.write(record + actor_offset::callback, 0x45c3a9);
}

void invoke_callback_substate(Memory& memory, Address object, std::uint32_t substate,
                              std::uint32_t argument, const InvokeActorCallback& invoke) {
    if (!object) return;
    const auto callback = memory.read(object + actor_offset::callback);
    if (!callback) return;
    memory.write(object + callback_argument, argument);
    const auto saved = memory.read(object + actor_offset::callback_state);
    memory.write(object + actor_offset::callback_state, substate);
    invoke(callback, object);
    if (memory.read(object + actor_offset::callback_state) == substate)
        memory.write(object + actor_offset::callback_state, saved);
    memory.write(object + callback_argument, 0);
}

void hand_off_field_control(Memory& memory, const GuestWords& words,
                            std::uint32_t from_slot, std::uint32_t to_slot,
                            bool publish_character_index) {
    const auto from = actor_slot(from_slot), to = actor_slot(to_slot);
    copy_render_state(words, to, from);
    clear_visual_control_bits(words, from);
    set_visual_control_bits(words, to);
    memory.write(from + actor_offset::callback, 0);
    memory.write(from + actor_offset::flags, memory.read(from + actor_offset::flags) & ~elevated_bit);
    memory.write(to + actor_offset::callback, field_movement_callback);
    memory.write(to + actor_offset::flags, memory.read(to + actor_offset::flags) | field_control_bits);
    memory.write(globals::active_party_index, to_slot);
    memory.write(globals::camera_focus_actor_index, to_slot);
    if (publish_character_index) memory.write(globals::character_index, to_slot);
}

void restore_character_snapshot(Memory& memory, const GuestWords& words, std::uint32_t index,
                                const SpawnPartySlot& spawn) {
    reset_party_slots(memory);
    memory.write(character_snapshot_slot, index);
    const auto members = character_snapshot_members + index * character_snapshot_member_bytes;
    const auto count = signed32(memory.read(character_snapshot_count + index * 4));
    for (std::int32_t member = 0; member < count; ++member)
        if (spawn) spawn(memory.read(members + std::uint32_t(member) * 4));
    const auto slot = memory.read(character_snapshot_active + index * 4);
    memory.write(globals::active_party_index, slot);
    memory.write(globals::camera_focus_actor_index, slot);
    memory.write(globals::character_index, slot);
    const auto object = actor_slot(slot);
    memory.write(object + actor_offset::flags,
                 memory.read(object + actor_offset::flags) | (field_control_bits | 0x40));
    memory.write(object + actor_offset::callback, field_movement_callback);
    const auto place = character_snapshot_place + index * character_snapshot_place_bytes;
    const auto tile_x = memory.read(place), tile_y = memory.read(place + 4);
    place_record_on_tile(words, object, tile_x, tile_y, memory.read(place + 8));
    const auto facing = memory.read(place + 0xc);
    memory.write(object + actor_offset::target_facing, facing);
    memory.write(object + actor_offset::facing, facing);
    memory.write(object + actor_offset::tile_x, tile_x);
    memory.write(object + actor_offset::tile_y, tile_y);
    for (auto at : {pending_move_marker, pending_move_facing, pending_move_frame,
                    pending_move_target_facing})
        memory.write(object + at, 0);
}

void tick_passive_actor(const GuestWords& words, Address object, const StepActorMotion& step) {
    if (step) step(object);
    words.write(object + actor_offset::tile_x,
                std::uint32_t(signed32(words.read(object + actor_offset::tile_x_q16)) / 0x10000));
    words.write(object + actor_offset::tile_y,
                std::uint32_t(signed32(words.read(object + actor_offset::tile_y_q16)) / 0x10000));
    const auto pixels = tile_to_pixels(signed32(words.read(object + actor_offset::tile_x_q16)),
                                       signed32(words.read(object + actor_offset::tile_y_q16)));
    words.write(object + actor_offset::world_x, std::uint32_t(pixels.x));
    words.write(object + actor_offset::world_y, std::uint32_t(pixels.y));
}

void focus_next_party_slot(Memory& memory, const GuestWords& words, std::uint32_t target) {
    const auto active = memory.read(globals::active_party_index);
    auto slot = target;
    if (signed32(target) <= -1)
        slot = std::uint32_t(std::int32_t(active + 1) % signed32(memory.read(globals::party_count)));
    hand_off_field_control(memory, words, active, slot, true);
}

bool focus_party_character(Memory& memory, const GuestWords& words, std::uint32_t character) {
    const auto count = signed32(memory.read(globals::party_count));
    std::int32_t slot = 0;
    while (slot < count && memory.read(globals::party_actor_ids + std::uint32_t(slot) * 4) != character)
        ++slot;
    if (slot == count) return false;
    const auto active = memory.read(globals::active_party_index);
    if (memory.read(globals::party_actor_ids + active * 4) == character) return false;
    hand_off_field_control(memory, words, active, std::uint32_t(slot), false);
    return true;
}
} // namespace fsb::core::actor_core
