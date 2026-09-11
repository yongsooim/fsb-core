#include "fsb_core/actor_core/ifc_stage.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {

Address ifc_register(const GuestWords& words, std::uint32_t slot, std::uint32_t selector,
                     const ResolveActor& resolve) {
    const auto object = resolve(selector);
    const auto record = ifc_stage_record(slot);
    words.write(record + ifc_field::selector, selector);
    words.write(record + ifc_field::object, object);
    if (!object) return record;
    words.write(record + ifc_field::saved_visible, visible(words, object) ? 1 : 0);
    words.write(record + ifc_field::saved_tile_state, tile_state(words, object) ? 1 : 0);
    words.write(record + ifc_field::saved_layer,
                std::uint32_t(signed32(words.read(object + actor_offset::layer_q16)) >> 16));
    refresh_tile_from_pixels(words, object);
    words.write(record + ifc_field::saved_tile_x, words.read(object + actor_offset::tile_x));
    words.write(record + ifc_field::saved_tile_y, words.read(object + actor_offset::tile_y));
    words.write(record + ifc_field::saved_facing, words.read(object + actor_offset::facing));
    words.write(record + ifc_field::sequence_handle, 0);
    return record;
}

Address ifc_live_sequence(Memory& memory, std::uint32_t slot) {
    const auto at = ifc_stage_record(slot) + ifc_field::sequence_handle;
    const auto object = resolve_compact(memory, memory.read(at));
    if (!object) memory.write(at, 0);
    return object ? *object : 0;
}

void ifc_bind_sequence(Memory& memory, std::uint32_t slot, std::uint32_t handle,
                       const InvalidSlotReport& report) {
    if (signed32(slot) >= 0 && slot < ifc_stage_slots) {
        memory.write(ifc_stage_record(slot) + ifc_field::sequence_handle, handle);
        return;
    }
    // The original logs the slot and the low half of the handle, then drops it.
    if (report) report(slot, handle & 0xffffu);
}
} // namespace fsb::core::actor_core
