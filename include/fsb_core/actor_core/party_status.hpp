#pragma once
#include "actor_slots.hpp"

// Equipment status flags, and the gate that decides whether a character may
// change one equipment slot.
//
// A character record is0xbc bytes at0x607a08. Its five equipment slots hold
// item ids at +0x74..+0x84 of that record (0x607a7c onward), and-1 means
// empty. Each item definition is0x4c bytes at0x61314c, with its status flag
// word at +0x4c of that (0x613198).
namespace fsb::core::actor_core {

inline constexpr Address character_records = 0x607a08;
inline constexpr unsigned character_record_bytes = 0xbc;
inline constexpr Address character_equipment = 0x607a7c;
inline constexpr unsigned equipment_slots = 5;
inline constexpr Address item_status_flags = 0x613198;
inline constexpr unsigned item_record_bytes = 0x4c;
// 45dd8b publishes the mask here, one dword per character in a16-byte row.
inline constexpr Address party_status_masks = 0x803864;
inline constexpr unsigned party_status_row_bytes = 0x10;
// Passing a slot index no equipment slot uses asks for the whole mask.
inline constexpr std::uint32_t no_excluded_slot = 5;

// 45dd8b: OR together the status flags of everything the character has
// equipped except one slot, publish it, and return it. An empty slot and the
// excluded slot both contribute nothing.
std::uint32_t collect_status_mask(Memory& memory, std::uint32_t character,
                                  std::uint32_t excluded_slot);

// 45de29: may this character change the equipment in this slot? Returns1 when
// the change is allowed. A slot is blocked only when it holds an item, that
// item carries the status bit its slot answers to, and the rest of the
// character's equipment carries the matching counter-bit.
std::uint32_t status_gate_allows(Memory& memory, std::uint32_t character, std::uint32_t slot);

} // namespace fsb::core::actor_core
