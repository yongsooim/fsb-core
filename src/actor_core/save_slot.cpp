#include "fsb_core/actor_core/save_slot.hpp"
#include "fsb_core/actor_core/jump_motion.hpp"
#include "fsb_core/actor_core/map_transition.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {

// The active actor's own record, which the snapshot carries four fields of.
Address active_actor(const Memory& memory) {
    return globals::actor_objects + memory.read(active_actor_index) * layout::actor_size;
}

// One block of the fixed body, in file order. Everything here is at a fixed
// address and a fixed size, so the two halves can share the list.
struct Block { Address at; std::uint32_t count, bytes; };
constexpr Block body_tail[] = {
    {character_index, 1, 4},
    {camera_focus_actor, 1, 4},
    {active_actor_index, 1, 4},
    {character_runtime_snapshot, character_runtime_snapshot_words, 4},
    {save_object_counts, save_object_categories, 4},
    {character_snapshot_actors, save_object_categories, 4},
    {active_character_snapshot, 1, 4},
    {character_snapshot_buffer, character_snapshot_buffer_words, 4},
    {party_panel_mask, 1, 4},
    {item_quantities, item_kinds, 4},
};
// After the four active-actor fields, and before the CIM blocks.
constexpr Block body_end[] = {
    {encounter_overrides, encounter_override_count, 4},
    {jump_active_gate, 1, 4},
    {map_switch_bits, 1, 4},
    {jump_gate_tile_x, gate_record_count, gate_record_bytes},
};
constexpr Block cim_blocks[] = {
    {cim_fixed_rows, cim_fixed_row_count, cim_fixed_row_bytes},
    {cim_runtime_block, 1, cim_runtime_block_bytes},
    {cim_companion_block, 1, cim_companion_block_bytes},
};

} // namespace

bool write_save_slot(Memory& memory, std::uint32_t slot, const SaveFile& file,
                     const SaveSlotHooks& hooks) {
    // A slot with a readable preview header is worth keeping: rotate it into
    // the backup name, deleting whatever was already there if the rename is
    // refused the first time.
    if (hooks.slot_readable && hooks.slot_readable(slot) && file.rename &&
        file.rename(slot, save_backup_name) != 0) {
        if (file.remove) file.remove(slot);
        file.rename(slot, save_backup_name);
    }
    const auto stream = file.open ? file.open(slot, true) : 0;
    if (!stream) return false;

    // --- the preview header, built fresh from live state ------------------
    const auto header = save_slot_headers + slot * save_slot_header_bytes;
    memory.write(header + save_header_slot_id, slot);
    const auto name = map_record_names + memory.read(requested_map) * map_record_bytes;
    for (unsigned word = 0; word < save_map_name_words; ++word)
        memory.write(header + save_header_map_name + word * 4, memory.read(name + word * 4));
    memory.write(header + save_header_map_name + save_map_name_words * 4,
                 memory.read(name + save_map_name_words * 4, 1), 1);
    const auto leader = memory.read(party_slot_members + memory.read(active_actor_index) * 4);
    memory.write(header + save_header_level,
                 memory.read(party_records + leader * party_record_bytes + party_record_level));
    memory.write(header + save_header_hours, memory.read(playtime_hours));
    memory.write(header + save_header_minutes, memory.read(playtime_minutes));
    memory.write(header + save_header_seconds, memory.read(playtime_seconds));
    file.write(stream, header, 1, save_slot_header_bytes);

    // --- the fixed body ---------------------------------------------------
    file.write(stream, party_records, party_record_count, party_record_bytes);
    file.write(stream, requested_map, 1, 4);
    file.write(stream, party_gold, 1, 4);
    file.write(stream, character_object_count, 1, 4);
    file.write(stream, party_slot_members, party_slot_count, 4);
    for (const auto& block : body_tail) file.write(stream, block.at, block.count, block.bytes);

    // The four fields of the active actor a restored game is placed from. Its
    // layer is stored as a whole tile rather than the record's Q16 lane.
    const auto actor = active_actor(memory);
    file.write(stream, actor + actor_offset::tile_x, 1, 4);
    file.write(stream, actor + actor_offset::tile_y, 1, 4);
    file.write_scratch(0, std::uint32_t(signed32(memory.read(actor + actor_offset::layer_q16)) >> 16));
    file.write(stream, file.scratch, 1, 4);
    file.write(stream, actor + actor_offset::facing, 1, 4);

    for (const auto& block : body_end) file.write(stream, block.at, block.count, block.bytes);
    // Publish the live option count before the block that carries it.
    memory.write(cim_runtime_block, memory.read(saveload_menu_max_index));
    for (const auto& block : cim_blocks) file.write(stream, block.at, block.count, block.bytes);

    // --- the two length-prefixed payloads ---------------------------------
    CimBlob blob;
    if (!hooks.serialize_cim || !hooks.serialize_cim(blob)) return false;
    file.write_scratch(0, blob.bytes);
    file.write(stream, file.scratch, 1, 4);
    file.write(stream, blob.payload, blob.bytes, 1);
    if (hooks.release_cim) hooks.release_cim();

    std::uint32_t event_bytes = 0;
    const auto events = hooks.event_data ? hooks.event_data(event_bytes) : 0;
    file.write_scratch(0, event_bytes);
    file.write(stream, file.scratch, 1, 4);
    file.write(stream, events, event_bytes, 1);
    file.close(stream);
    return true;
}

bool load_save_slot(Memory& memory, std::uint32_t slot, const SaveFile& file,
                    const SaveSlotHooks& hooks) {
    const auto stream = file.open ? file.open(slot, false) : 0;
    if (!stream) return false;
    // Nothing is loaded yet, so the field has no map until the transition the
    // caller starts afterwards picks up the restored one.
    memory.write(requested_map, invalid_map_id);

    const auto header = save_slot_headers + slot * save_slot_header_bytes;
    file.read(stream, header, 1, save_slot_header_bytes);
    memory.write(playtime_hours, memory.read(header + save_header_hours));
    memory.write(playtime_minutes, memory.read(header + save_header_minutes));
    memory.write(playtime_seconds, memory.read(header + save_header_seconds));

    // The party names are live heap pointers, and the record block is about to
    // be read straight over them.
    std::uint32_t names[party_record_count];
    for (unsigned index = 0; index < party_record_count; ++index)
        names[index] = memory.read(party_records + index * party_record_bytes + party_record_name);
    file.read(stream, party_records, party_record_count, party_record_bytes);
    for (unsigned index = 0; index < party_record_count; ++index)
        memory.write(party_records + index * party_record_bytes + party_record_name, names[index]);
    memory.write(pending_pcpos_index, no_pcpos_override);

    file.read(stream, loaded_map, 1, 4);
    file.read(stream, party_gold, 1, 4);

    // The character objects are rebuilt from the saved slot list rather than
    // read back, so the count is only borrowed for the loop.
    file.read(stream, file.scratch, 1, 4);
    const auto saved_count = file.read_scratch(0);
    file.read(stream, file.scratch + 4, party_slot_count, 4);
    if (hooks.reset_character_objects) hooks.reset_character_objects();
    // The slot table itself is not restored: the objects are respawned from
    // what the file said instead. The original walks its own ten-dword local
    // for as many entries as the file claims, so a file claiming more than ten
    // reads past it; the count is bounded here rather than reproduced.
    for (std::int32_t index = 0; index < signed32(saved_count) && index < signed32(party_slot_count);
         ++index)
        if (hooks.spawn_character_object)
            hooks.spawn_character_object(file.read_scratch(4 + std::uint32_t(index) * 4));

    for (const auto& block : body_tail) file.read(stream, block.at, block.count, block.bytes);

    const auto actor = active_actor(memory);
    file.read(stream, actor + actor_offset::tile_x, 1, 4);
    file.read(stream, actor + actor_offset::tile_y, 1, 4);
    // The layer is parked for the caller instead of going back into the record.
    file.read(stream, saved_active_actor_layer, 1, 4);
    file.read(stream, actor + actor_offset::facing, 1, 4);
    // The restored facing is also the actor's target facing, so it does not
    // immediately turn away from where it was saved.
    memory.write(actor + actor_offset::target_facing, memory.read(actor + actor_offset::facing));

    for (const auto& block : body_end) file.read(stream, block.at, block.count, block.bytes);
    for (const auto& block : cim_blocks) file.read(stream, block.at, block.count, block.bytes);
    memory.write(saveload_menu_max_index, memory.read(cim_runtime_block));

    // --- the two length-prefixed payloads ---------------------------------
    file.read(stream, file.scratch, 1, 4);
    const auto cim_bytes = file.read_scratch(0);
    const auto cim = file.allocate(cim_bytes);
    file.read(stream, cim, cim_bytes, 1);
    if (hooks.deserialize_cim) hooks.deserialize_cim(cim);
    file.release(cim);
    // The restored map is handed to the transition rather than loaded here.
    memory.write(map_transition_target, invalid_map_id);
    memory.write(map_transition_mode_word, 1);

    file.read(stream, file.scratch, 1, 4);
    const auto event_bytes = file.read_scratch(0);
    const auto events = file.allocate(event_bytes);
    file.read(stream, events, event_bytes, 1);
    if (hooks.store_event_data) hooks.store_event_data(events, event_bytes);
    file.release(events);
    file.close(stream);

    // --- put the restored state back into a shape a frame can use ---------
    // The guard is upper-bound only: a negative cursor is left alone.
    const auto clamp = [&](Address cursor) {
        if (signed32(memory.read(character_object_count)) <= signed32(memory.read(cursor)))
            memory.write(cursor, 0);
    };
    clamp(cim_runtime_cursor_a);
    clamp(cim_runtime_cursor_b);
    clamp(cim_runtime_cursor_c);
    memory.write(battle_entrance_bgm_played, 0);
    if (hooks.prepare_map_transition) hooks.prepare_map_transition();
    memory.write(autostep_sound_active, 0);
    memory.write(globals::field_transition_phase, std::uint32_t(save_load_settled_phase));
    // A save taken while an event continuation was live asks for the screen
    // back on the way in.
    if (memory.read(event_state_mode) == event_continuation_mode ||
        memory.read(event_sequence_mode) == event_continuation_mode)
        memory.write(screen_request_mode, save_load_screen_request);
    return true;
}

} // namespace fsb::core::actor_core
