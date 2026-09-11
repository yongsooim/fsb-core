#pragma once
#include "cim_blob.hpp"
#include "item_menu.hpp"
#include "session_state.hpp"

// SaveN.dat: the two halves of the slot snapshot.
//
// The file is a fixed prefix followed by two length-prefixed payloads:
//
//   0x0000  0x30-byte preview header, the only part the slot menu reads
//   0x0030  the sixteen party records
//   0x0bf0  the run of scalars and tables the field keeps its state in
//   0x1aa0  the sixteen fixed CIM rows
//   0x1ba0  the 0x38-byte CIM runtime block
//   0x1bd8  the 0x24-byte CIM companion block
//   0x1bfc  the CIM blob, byte count first
//           then the event-counter blob, byte count first
//
// The two routines walk the same list in the same order, so anything added to
// one has to be added to the other. Every file, heap and codec call is
// injected: what is reconstructed here is the order, the sizes and the small
// amount of state fixing that happens around them.
namespace fsb::core::actor_core {

// The file mode strings the original hands the CRT, kept so the substituted
// service still sees which way the stream was opened.
inline constexpr Address save_mode_write = 0x4a6b84;
inline constexpr Address save_mode_read = 0x4a6b48;
inline constexpr Address save_backup_name = 0x5d2570;

// The preview header, both in the file and in the slot menu's own cache.
inline constexpr unsigned save_header_slot_id = 0;
inline constexpr unsigned save_header_map_name = 4;
inline constexpr unsigned save_header_level = 0x20;
inline constexpr unsigned save_header_hours = 0x24;
inline constexpr unsigned save_header_minutes = 0x28;
inline constexpr unsigned save_header_seconds = 0x2c;
// The map name is copied as six whole words and a terminator byte.
inline constexpr unsigned save_map_name_words = 6;

inline constexpr Address playtime_hours = 0x804c70;
inline constexpr Address playtime_minutes = 0x804c74;
inline constexpr Address playtime_seconds = 0x804c78;

// The party. Sixteen records; the name is a live pointer into the heap, so the
// loader has to put the sixteen of them back after it reads over the records.
inline constexpr Address party_records = 0x607a08;
inline constexpr unsigned party_record_bytes = 0xbc;
inline constexpr unsigned party_record_count = 0x10;
inline constexpr unsigned party_record_name = 0x14;
inline constexpr unsigned party_record_level = 0x28;
// Which party member sits in each of the ten slots.
inline constexpr Address party_slot_members = 0x5d2258;
inline constexpr unsigned party_slot_count = 10;

// The field state the snapshot carries, in file order.
inline constexpr Address character_object_count = 0x803a20;
inline constexpr Address character_index = 0x803a24;
inline constexpr Address camera_focus_actor = 0x787478;
inline constexpr Address active_actor_index = 0x803a1c;
inline constexpr Address character_runtime_snapshot = 0x803960;
inline constexpr unsigned character_runtime_snapshot_words = 0x1e;
inline constexpr Address save_object_counts = 0x803a30;
inline constexpr Address character_snapshot_actors = 0x803a08;
inline constexpr unsigned save_object_categories = 3;
inline constexpr Address active_character_snapshot = 0x803a3c;
inline constexpr Address character_snapshot_buffer = 0x8039d8;
inline constexpr unsigned character_snapshot_buffer_words = 0xc;
inline constexpr Address party_panel_mask = 0x803a40;
inline constexpr Address item_quantities = 0x806e30;
inline constexpr unsigned item_kinds = 0x168;
inline constexpr Address encounter_overrides = 0x5bf4d8;
inline constexpr unsigned encounter_override_count = 0x1f4;
inline constexpr Address map_switch_bits = 0x806b24;
inline constexpr unsigned gate_record_count = 4;
inline constexpr unsigned gate_record_bytes = 8;

// The CIM blocks that are fixed size and travel with the prefix.
inline constexpr Address cim_fixed_rows = 0x803858;
inline constexpr unsigned cim_fixed_row_count = 0x10;
inline constexpr unsigned cim_fixed_row_bytes = 0x10;
inline constexpr Address cim_runtime_block = 0x5d21b0;
inline constexpr unsigned cim_runtime_block_bytes = 0x38;
inline constexpr Address cim_companion_block = 0x5d21e8;
inline constexpr unsigned cim_companion_block_bytes = 0x24;
// +0x00 of the runtime block mirrors the slot menu's option count both ways.
inline constexpr Address saveload_menu_max_index = 0x804aa8;
// The three restored cursors the loader clamps against the actor count.
inline constexpr Address cim_runtime_cursor_a = 0x5d21b4;
inline constexpr Address cim_runtime_cursor_b = 0x5d21c4;
inline constexpr Address cim_runtime_cursor_c = 0x5d21cc;

// Odds and ends the loader resets so the restored frame behaves.
inline constexpr Address pending_pcpos_index = 0x803a28;
inline constexpr Address saved_active_actor_layer = 0x803a14;
inline constexpr Address battle_entrance_bgm_played = 0x77ec50;
inline constexpr Address autostep_sound_active = 0x8021d8;
inline constexpr Address event_state_mode = 0x607cbc;
inline constexpr Address event_sequence_mode = 0x607cc0;
inline constexpr Address screen_request_mode = 0x607cf4;
inline constexpr std::uint32_t event_continuation_mode = 0xe6;
inline constexpr std::uint32_t save_load_screen_request = 0x13;
// A loaded save is settled on its tile, so a jmp tile under it waits a frame.
inline constexpr std::int32_t save_load_settled_phase = 2;
inline constexpr std::uint32_t no_pcpos_override = 0xffffffffu;
inline constexpr std::uint32_t invalid_map_id = 0xffffffffu;

// Every file and heap call the two routines make. The (count, bytes) pair is
// kept as the original passes it because that pair is part of the file layout.
struct SaveFile {
    std::function<Address(std::uint32_t slot, bool for_writing)> open;
    std::function<void(Address file)> close;
    std::function<void(Address file, Address at, std::uint32_t count, std::uint32_t bytes)> write;
    std::function<void(Address file, Address at, std::uint32_t count, std::uint32_t bytes)> read;
    std::function<std::uint32_t(std::uint32_t slot, Address to)> rename;
    std::function<void(std::uint32_t slot)> remove;
    std::function<Address(std::uint32_t bytes)> allocate;
    std::function<void(Address buffer)> release;
    // Room for at least twelve dwords the routine may use for the few values
    // it passes through the file without keeping: in the original this is its
    // own stack frame, so the calls stay ordinary block transfers.
    Address scratch = 0;
    std::function<std::uint32_t(unsigned offset)> read_scratch;
    std::function<void(unsigned offset,std::uint32_t value)> write_scratch;
};

// The codecs and the object table rebuild, all owned elsewhere.
struct SaveSlotHooks {
    std::function<bool(CimBlob& blob)> serialize_cim;          // 461549
    std::function<void()> release_cim;                         // 4616e2
    std::function<void(Address payload)> deserialize_cim;      // 461712
    std::function<Address(std::uint32_t& bytes)> event_data;    // 431e95
    std::function<void(Address payload, std::uint32_t bytes)> store_event_data;  // 431f06
    std::function<void()> reset_character_objects;             // 45f47e
    std::function<void(std::uint32_t member)> spawn_character_object;  // 45f0e9
    std::function<void()> prepare_map_transition;              // 458da9
    std::function<bool(std::uint32_t slot)> slot_readable;      // 460deb
};

// 460e58. Rotates a readable slot into Saveold.dat, then writes the snapshot.
// Answers true only when every payload made it out.
bool write_save_slot(Memory& memory, std::uint32_t slot, const SaveFile& file,
                     const SaveSlotHooks& hooks);

// 46118e. Restores the snapshot and puts the field back into a state a frame
// can be drawn from. Answers false only when the slot file would not open.
bool load_save_slot(Memory& memory, std::uint32_t slot, const SaveFile& file,
                    const SaveSlotHooks& hooks);

} // namespace fsb::core::actor_core
