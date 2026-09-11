#pragma once
#include "actor_slots.hpp"

// The CIMsave side blob: the variable-length tail SaveN.dat carries after its
// fixed prefix. Four sections behind a six-word header, then the whole thing is
// XORed with a key taken from the tick counter at save time.
//
//   header[0] total bytes
//   header[1] magic 0x34
//   header[2] object and effect flag bytes
//   header[3] compact world record bytes
//   header[4] event and profile head bytes
//   header[5] key ^ sentinel, so the loader can recover the key
//   +0x18     the 0x34-byte event flag bitmap
//   +0x4c     the object and effect flag bits
//   then      one 12-byte record per world city: flags, destination map,
//             entry parameter - three fields out of each 0x15-word row
//   then      the first word of each 0x11-word event and profile row
//
// The three counts come from the executable's own tables, so a blob written by
// one build only loads into the same build; the loader checks all four sizes
// and the magic before it touches anything.
namespace fsb::core::actor_core {

inline constexpr std::uint32_t cim_magic = 0x34;
inline constexpr std::uint32_t cim_key_sentinel = 0x66666666;
inline constexpr unsigned cim_header_words = 6;
inline constexpr unsigned cim_header_and_bitmap_bytes = 0x4c;
inline constexpr unsigned cim_event_bitmap_bytes = 0x34;
inline constexpr unsigned cim_section_offset_words = 0x13;
inline constexpr unsigned cim_world_record_bytes = 0xc;
inline constexpr unsigned cim_world_row_words = 0x15;
inline constexpr unsigned cim_event_row_words = 0x11;

// Section counts, fixed in the executable.
inline constexpr Address cim_world_record_count = 0x4a2660;
inline constexpr Address cim_event_row_count = 0x4a27cc;
inline constexpr Address cim_object_effect_words = 0x4a27d0;
// Live state the blob mirrors.
inline constexpr Address cim_event_bitmap = 0x806b28;
inline constexpr Address cim_object_effect_bits = 0x7ab4d8;
// The world row's destination map word; its city flags sit two words earlier.
inline constexpr Address cim_world_rows = 0x5affa0;
inline constexpr Address cim_event_rows = 0x5b356c;

// Every way the two halves can refuse. The original writes one debug line per
// case, so the caller supplies the report rather than this module holding
// string addresses.
enum class CimFailure {
    AlreadyHeld,        // Serialise called while a blob is still allocated.
    AllocationFailed,
    NullBlob,           // Deserialise called with no payload.
    TotalSize,
    Magic,
    ObjectEffectSize,
    WorldRecordSize,
    EventFlagSize,
};
using CimReport = std::function<void(CimFailure reason)>;
using AllocateBlob = std::function<Address(std::uint32_t bytes)>;
using TickSource = std::function<std::uint32_t()>;

struct CimBlob { Address payload = 0; std::uint32_t bytes = 0; };

// 461549: build and obfuscate the blob, publishing it at 0x804c7c/0x804c80.
// Fails without allocating when a blob is already held.
bool serialize_cim_blob(Memory& memory, CimBlob& blob, const AllocateBlob& allocate,
                        const TickSource& tick, const CimReport& report = {});

// 461712: decode a blob in place and restore what it carries. Every size and
// the magic are checked first; a mismatch restores nothing.
bool deserialize_cim_blob(Memory& memory, Address payload, const CimReport& report = {});

} // namespace fsb::core::actor_core
