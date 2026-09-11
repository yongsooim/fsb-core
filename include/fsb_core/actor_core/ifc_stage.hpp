#pragma once
#include "actor_slots.hpp"

// The IFC stage table: the0x40 slots an event uses to borrow actors, remember
// how they looked before the scene, and put them back afterwards.
//
// One record is nine words at0x768aa0 + slot *0x24:
//   +0x00 selector the script asked for
//   +0x04 actor record it resolved to, or0 when the actor was created for the
//         scene and must be released instead of restored
//   +0x08 visibility bit before the scene
//   +0x0c tile state bit before the scene
//   +0x10 layer, in whole units
//   +0x14/+0x18 tile position before the scene
//   +0x1c facing before the scene
//   +0x20 handle of the child sequence currently driving the slot
namespace fsb::core::actor_core {

inline constexpr Address ifc_stage_table = 0x768aa0;
inline constexpr unsigned ifc_stage_record_bytes = 0x24;
inline constexpr unsigned ifc_stage_slots = 0x40;

namespace ifc_field {
inline constexpr unsigned selector = 0x00;
inline constexpr unsigned object = 0x04;
inline constexpr unsigned saved_visible = 0x08;
inline constexpr unsigned saved_tile_state = 0x0c;
inline constexpr unsigned saved_layer = 0x10;
inline constexpr unsigned saved_tile_x = 0x14;
inline constexpr unsigned saved_tile_y = 0x18;
inline constexpr unsigned saved_facing = 0x1c;
inline constexpr unsigned sequence_handle = 0x20;
} // namespace ifc_field

inline constexpr Address ifc_stage_record(std::uint32_t slot) {
    return ifc_stage_table + slot * ifc_stage_record_bytes;
}

// 430b1e: claim a stage slot for a selector and record the actor's current
// appearance. Returns the record address. An unresolved selector leaves the
// saved fields alone, which is how the release path tells the two cases apart.
// Note this refreshes the actor's tile lanes from its pixel position first, so
// the saved tile is the one the renderer is using rather than a stale cache.
Address ifc_register(const GuestWords& words, std::uint32_t slot, std::uint32_t selector,
                     const ResolveActor& resolve);

// 430cc3: the live child sequence driving a slot, clearing the handle when the
// object behind it is already gone.
Address ifc_live_sequence(Memory& memory, std::uint32_t slot);

// 430ce8: bind a child sequence handle to a slot. Slots outside0..0x3f are
// reported through the shared error log, and nothing is stored.
using InvalidSlotReport = std::function<void(std::uint32_t slot, std::uint32_t handle)>;
void ifc_bind_sequence(Memory& memory, std::uint32_t slot, std::uint32_t handle,
                       const InvalidSlotReport& report = {});

} // namespace fsb::core::actor_core
