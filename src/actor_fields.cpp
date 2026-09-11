#include "fsb_core/actor_fields.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
// 0x408de1/0x408e69: original Q16 quarter-wave table, 65536 units per turn.
// Unsigned arithmetic deliberately preserves x86 wrapping, including angle+8.
std::uint32_t fixed_sin(const Memory& memory, std::uint32_t angle) {
    const auto phase = ((angle + 8u) >> 4) & 0xfffu;
    switch (phase & 0xc00u) {
    case 0: return memory.read(tables::sine_quarter_wave + phase * 4);
    case 0x400: return memory.read(tables::sine_quarter_wave + (0x800 - phase) * 4);
    case 0x800: return 0u - memory.read(tables::sine_negative_quarter_alias + phase * 4);
    default: return 0u - memory.read(0x4ab7b8 - phase * 4);
    }
}
std::uint32_t fixed_cos(const Memory& memory, std::uint32_t angle) {
    return fixed_sin(memory, angle + 0x4000u);
}
Address lookup_actor(const Memory& memory, std::uint32_t id) {
    // Original42feb9: the direct-object form passes through, everything else
    // goes to the457ec9 chain walk that actor_core owns.
    if (id >= actor_core::direct_object_selector) { memory.read(id); return id; }
    return actor_core::resolve_slot(memory, id);
}
void set_actor_tile_position(Memory& memory, Address object, std::int32_t x, std::int32_t y, std::int32_t z) {
    // Original45d774. The single implementation lives in actor_core so the
    // reconstruction and this in-memory entry shape cannot drift apart.
    actor_core::place_record_on_tile(actor_core::memory_words(memory), object,
                                     std::uint32_t(x), std::uint32_t(y), std::uint32_t(z));
}
void set_actor_tile_state(Memory& memory, std::uint32_t id, unsigned state) {
    if (state > 1) throw Fault(0x430059, "invalid actor tile state");
    const auto object = lookup_actor(memory, id);
    if (!object) return; // The original wrapper permits an unmaterialized actor.
    const auto flags = memory.read(object + actor_offset::flags);
    memory.write(object + actor_offset::flags, state ? flags | 0x10000u : flags & ~0x10000u);
    set_actor_tile_position(memory, object,
        signed32(memory.read(object + actor_offset::world_x)) / units::tile_width_q16,
        signed32(memory.read(object + actor_offset::world_y)) / units::tile_height_q16,
        signed32(memory.read(object + actor_offset::layer_q16)) / units::q16_one);
}
void set_actor_raw_position(Memory& memory, Address object, std::uint32_t x, std::uint32_t y) {
    // Original4301e1, implemented once in actor_core.
    actor_core::place_at_pixels(actor_core::memory_words(memory), object, x, y);
}
} // namespace fsb::core
