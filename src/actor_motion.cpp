#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/arena.hpp"
#include "fsb_core/progress_timer.hpp"
#include <cstring>
#include <limits>

namespace fsb::core {
Handle Actors::attach_child(std::uint32_t id,Address definition){
    const auto handle=Arena(memory_).clone_event(definition,1),child=*resolve_compact(memory_,handle);
    memory_.write(child+vm_offset::actor_id,id);
    memory_.write(child+vm_offset::actor_object,lookup_actor(memory_,id));
    return handle;
}
namespace {
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53,
              "Original CRT motion arithmetic requires IEEE binary64");
std::uint32_t power_curve(std::uint32_t progress, unsigned power) {
    // 0x404343; CRT0x49a2f0 selects53-bit mantissa, round-to-nearest.
    // Preserve every multiply/divide. Rational simplification changes boundary results.
    double result = 1;
    for (unsigned i = 0; i < power; ++i) { result *= double(signed32(progress)); if (i) result /= 30030.0; }
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(result));
}
std::uint32_t scale_delta(const Memory& memory, std::uint32_t delta, std::uint32_t blend) {
    const std::uint64_t bits = memory.read(tables::motion_negative_reciprocal) | (std::uint64_t(memory.read(0x4a261c)) << 32);
    const auto reciprocal = std::bit_cast<double>(bits);
    const double product = double(signed32(delta)) * double(signed32(blend));
    return 0u - static_cast<std::uint32_t>(static_cast<std::int64_t>(product * reciprocal));
}
}
Handle Actors::start_turn(std::uint32_t id, std::uint32_t direction, std::uint32_t extra) {
    if (direction > 7) return 0; // 0x430d46 rejects an invalid selector before actor lookup.
    const auto actor = lookup_actor(memory_, id);
    if (!actor) throw Fault(routines::start_turn_child, "turn child needs a materialized actor");
    const auto handle = Arena(memory_).clone_event(scripts::turn, 1), child = *resolve_compact(memory_, handle);
    memory_.write(child + vm_offset::actor_id, id); memory_.write(child + vm_offset::actor_object, actor);
    memory_.write(child + vm_offset::frame_bias, extra); memory_.write(actor + actor_offset::target_facing, direction);
    return handle;
}
Handle Actors::tween_raw(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t duration, bool input_pause) {
    const auto actor = lookup_actor(memory_, id);
    if (!actor) throw Fault(0x431407, "raw tween needs a materialized actor");
    if (input_pause || signed32(duration) <= 0) { set_actor_raw_position(memory_, actor, x << 16, y << 16); return 0; }
    const auto handle = Arena(memory_).allocate_after(memory_.read(globals::group1_append_link), routines::raw_tween_tick, 0x30000, 0);
    const auto child = *resolve_compact(memory_, handle);
    memory_.write(child + vm_offset::actor_object, actor); memory_.write(child + 0x168, x << 16); memory_.write(child + 0x16c, y << 16);
    const auto current_x = sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::world_x), 16), current_y = sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::world_y), 16);
    memory_.write(child + 0x180, sequence_alu(alu::signed_divide, (x - current_x) << 16, duration));
    memory_.write(child + 0x184, sequence_alu(alu::signed_divide, (y - current_y) << 16, duration));
    return handle;
}
Handle Actors::tween_tiles(std::uint32_t id, std::uint32_t dx, std::uint32_t dy, std::uint32_t duration,
                          std::uint32_t arc_x, std::uint32_t arc_y, bool hide, bool input_pause) {
    const auto actor = lookup_actor(memory_, id);
    if (!actor) throw Fault(0x43113b, "tile tween needs a materialized actor");
    const auto x = dx * unsigned(units::tile_width_q16), y = dy * unsigned(units::tile_height_q16);
    if (input_pause) { set_actor_raw_position(memory_, actor, memory_.read(actor + actor_offset::world_x) + x, memory_.read(actor + actor_offset::world_y) + y); return 0; }
    const auto handle = Arena(memory_).allocate_after(memory_.read(globals::group1_append_link), routines::tile_tween_tick, 0x10010000, 0);
    const auto child = *resolve_compact(memory_, handle);
    memory_.write(child + vm_offset::actor_id, id); memory_.write(child + vm_offset::actor_object, actor);
    memory_.write(child + 0x178, memory_.read(actor + actor_offset::world_x)); memory_.write(child + 0x17c, memory_.read(actor + actor_offset::world_y));
    memory_.write(child + 0x168, x); memory_.write(child + 0x16c, y);
    memory_.write(child + 0x170, arc_x); memory_.write(child + 0x174, arc_y);
    memory_.write(child + 0x14c, signed32(arc_x) < 0 ? 0xc000 : 0x4000);
    memory_.write(child + 0x150, signed32(arc_y) < 0 ? 0 : 0x8000);
    const auto timer = memory_.allocate_zeroed(28); memory_.write(child + compact_offset::state_pointer, timer);
    ProgressTimer(memory_, timer).start(sequence_alu(alu::signed_divide, duration * 1000u, 60), true, false, memory_.read(globals::frame_time_ms));
    memory_.write(child + 0x19c, hide); return handle;
}
void Actors::tick_tile_tween(Address object, bool input_pause) {
    const auto timer = memory_.read(object + compact_offset::state_pointer);
    if (memory_.read(object + compact_offset::lifecycle) == 0xffffffffu) {
        memory_.release_allocation(timer); memory_.write(object + compact_offset::lifecycle, 0); return;
    }
    if (!timer) throw Fault(object, "tile tween has no progress timer");
    auto progress = ProgressTimer(memory_, timer).tick(memory_.read(globals::frame_time_ms));
    if (input_pause) progress = units::progress_complete;
    const auto blend = progress * 2 - power_curve(progress, 2), ramp = progress - power_curve(progress, 3);
    for (unsigned axis = 0; axis < 2; ++axis) {
        auto current = memory_.read(object + 0x178 + axis * 4) + scale_delta(memory_, memory_.read(object + 0x168 + axis * 4), blend);
        const auto amplitude = memory_.read(object + 0x170 + axis * 4);
        if (amplitude) {
            const auto magnitude = signed32(amplitude) < 0 ? 0u - amplitude : amplitude;
            memory_.write(object + 0x158 + axis * 4, sequence_alu(alu::signed_divide, magnitude * ramp, units::progress_complete));
        }
        const auto phase = memory_.read(object + 0x14c + axis * 4);
        const auto offset = (axis ? fixed_cos(memory_, phase) : fixed_sin(memory_, phase)) * memory_.read(object + 0x158 + axis * 4);
        current += axis ? 0u - offset : offset; memory_.write(object + 0x11c + axis * 4, current);
    }
    const auto actor = memory_.read(object + 0xfc);
    set_actor_raw_position(memory_, actor, memory_.read(object + 0x11c), memory_.read(object + 0x120));
    if (progress == units::progress_complete) { if (memory_.read(object + 0x19c)) visible(actor, false); memory_.write(object + compact_offset::lifecycle, 0xffffffffu); }
}
void Actors::tick_raw_tween(Address object, unsigned jobs) {
    const auto actor = memory_.read(object + 0xfc);
    std::uint32_t current[2]; bool done = true;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const auto target = memory_.read(object + 0x168 + axis * 4), velocity = memory_.read(object + 0x180 + axis * 4);
        auto value = memory_.read(actor + actor_offset::world_x + axis * 4) + velocity * jobs;
        if ((signed32(velocity) < 0 && signed32(value) < signed32(target)) ||
            (signed32(velocity) > 0 && signed32(value) > signed32(target))) value = target;
        current[axis] = value; done &= ((value ^ target) & 0xffff0000u) == 0;
    }
    set_actor_raw_position(memory_, actor, current[0], current[1]);
    if (done) memory_.write(object + compact_offset::lifecycle, 0xffffffffu); // 0x20000 flag defers release without another callback.
}
} // namespace fsb::core
