#include "fsb_core/camera.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/arena.hpp"

namespace fsb::core {
void Camera::update_scroll_bounds(std::int32_t x,std::int32_t y,bool enabled){
    if(enabled){
        const auto low=signed32(memory_.read(globals::camera_scroll_min_x)),high=signed32(memory_.read(globals::camera_scroll_max_x));
        if(x<low)x=low;if(x>high)x=high;
        // Actual0x454106 compares Y against X bounds, then substitutes Y bounds.
        if(y<low)y=signed32(memory_.read(globals::camera_scroll_min_y));if(y>high)y=signed32(memory_.read(globals::camera_scroll_max_y));
    }
    const auto left=std::uint32_t(x)-memory_.read(0x787470),top=std::uint32_t(y)-memory_.read(0x787474);
    memory_.write(0x787460,left);memory_.write(0x787464,top);
    memory_.write(0x787468,left+memory_.read(0x5c4ec8));memory_.write(0x78746c,top+memory_.read(0x5c4ecc));
}
void Camera::enter_tile_focus() {
    const auto index = memory_.read(globals::camera_focus_actor_index);
    if (index >= 0x300) throw Fault(index, "camera focus index outside actor pool");
    for (unsigned axis = 0; axis < 3; ++axis)
        memory_.write(globals::camera_focus_x_q16 + axis * 4, memory_.read(0x8073e0 + index * 0x1ac + axis * 4));
    memory_.write(globals::camera_focus_actor_index, 0x2ff);
}
void Camera::focus_actor(Address actor) {
    if (!actor) throw Fault(0x4545b7, "cannot focus an absent actor");
    memory_.write(globals::camera_focus_actor_index, memory_.read(actor));
}
Handle Camera::spawn_followup(std::uint32_t duration, std::uint32_t x, std::uint32_t y, std::uint32_t actor_id) {
    if (!duration) throw Fault(0x430ee7, "camera tween duration is zero");
    Arena arena(memory_);
    const auto handle = arena.allocate_after(memory_.read(globals::group1_append_link), routines::camera_follow_tick, 0x30000, 0);
    const auto object = *resolve_compact(memory_, handle);
    memory_.write(object + 0x178, memory_.read(globals::camera_focus_x_q16)); memory_.write(object + 0x17c, memory_.read(globals::camera_focus_y_q16));
    if (actor_id == 0xffffffffu) {
        memory_.write(object + 0x168, ((x << 16) | 0x8000u) << 6);
        memory_.write(object + 0x16c, ((y << 16) | 0x8000u) * 48);
    } else {
        memory_.write(object + vm_offset::actor_id, actor_id); memory_.write(object + vm_offset::actor_object, lookup_actor(memory_, actor_id));
        const auto party_index = memory_.read(globals::active_party_index);
        if (party_index > 15) throw Fault(party_index, "invalid player party index");
        if (actor_id == memory_.read(globals::party_actor_ids + party_index * 4)) memory_.write(object + 0x19c, 1);
    }
    const auto actor = memory_.read(object + vm_offset::actor_object);
    for (unsigned axis = 0; axis < 2; ++axis) {
        const auto target = memory_.read(actor ? actor + actor_offset::world_x + axis * 4 : object + 0x168 + axis * 4);
        const auto delta = target - memory_.read(object + 0x178 + axis * 4);
        memory_.write(object + 0x180 + axis * 4, sequence_alu(alu::signed_divide, delta, duration));
    }
    return handle;
}
void Camera::tick_followup(Address object, unsigned jobs) {
    auto actor = memory_.read(object + vm_offset::actor_object);
    if (actor) {
        const auto current = lookup_actor(memory_, memory_.read(object + vm_offset::actor_id)); memory_.write(object + vm_offset::actor_object, current);
        if (!current || memory_.read(globals::camera_focus_actor_index) != 0x2ff || actor != current) { memory_.write(object + compact_offset::lifecycle, 0xffffffff); return; }
        if (!(memory_.read(current + 4) & 64) && memory_.read(object + 0x19c)) {
            const auto party_index = memory_.read(globals::active_party_index);
            if (party_index > 15) throw Fault(party_index, "invalid camera fallback player index");
            const auto id = memory_.read(globals::party_actor_ids + party_index * 4);
            memory_.write(object + vm_offset::actor_id, id); actor = lookup_actor(memory_, id); memory_.write(object + vm_offset::actor_object, actor);
            if (!actor) throw Fault(object, "camera fallback player has no actor");
        }
    }
    bool arrived = true;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const auto target = memory_.read(actor ? actor + actor_offset::world_x + axis * 4 : object + 0x168 + axis * 4);
        auto value = memory_.read(globals::camera_focus_x_q16 + axis * 4), velocity = memory_.read(object + 0x180 + axis * 4);
        if (!velocity) {
            if (signed32(value) < signed32(target)) velocity = 0x20000;
            else if (signed32(value) > signed32(target)) velocity = 0xfffe0000;
            memory_.write(object + 0x180 + axis * 4, velocity);
        }
        value += jobs * velocity;
        if ((signed32(velocity) < 0 && signed32(value) < signed32(target)) ||
            (signed32(velocity) > 0 && signed32(value) > signed32(target))) value = target;
        memory_.write(globals::camera_focus_x_q16 + axis * 4, value); arrived &= value == target;
    }
    if (arrived) { if (actor) focus_actor(actor); memory_.write(object + compact_offset::lifecycle, 0xffffffff); }
}
void Camera::clamp_target(std::int32_t x, std::int32_t y, bool enabled) {
    // 0x453fe3 pins one-tile borders. The right/bottom <= test deliberately
    // subtracts one extra pixel, and equality of viewport/interior centers it.
    for (unsigned axis = 0; axis < 2; ++axis) {
        auto target = axis ? y : x;
        if (enabled) {
            const int extent = signed32(memory_.read(globals::map_extent_x_q16 + axis * 4)) / units::q16_one;
            const int size = signed32(memory_.read(axis ? globals::viewport_height : globals::viewport_width));
            const int border = axis ? 48 : 64, interior = extent - border * 2;
            if (size >= interior) target = interior / 2 + border;
            else {
                const auto low = std::int64_t(target) - size / 2, high = low + size;
                const auto add = low < border ? border - low : 0;
                const auto subtract = extent - border <= high ? high - (extent - border) + 1 : 0;
                target = signed32(std::uint32_t(std::int64_t(target) + add - subtract));
            }
        }
        memory_.write(globals::camera_x + axis * 4, std::uint32_t(target));
    }
}
} // namespace fsb::core
