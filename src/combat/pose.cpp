#include "fsb_core/combat/pose.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::combat {
namespace {
constexpr std::uint32_t visible = 0x40, alternate_appearance = 0x40000000;
constexpr unsigned fixed_point = 65536, tile_width = 64, tile_height = 48;
// Sprite template records; bit0 of the first byte suppresses the alternate pose.
constexpr Address sprite_templates = 0x5b356c, sprite_template_stride = 0x44;
} // namespace

void Pose::locate(Address actor) {
    memory_.write(actor + actor_offset::tile_x,
                  std::uint32_t(signed32(memory_.read(actor + actor_offset::tile_x_q16)) / int(fixed_point)));
    memory_.write(actor + actor_offset::tile_y,
                  std::uint32_t(signed32(memory_.read(actor + actor_offset::tile_y_q16)) / int(fixed_point)));
}
void Pose::locate_from_world(Address actor) {
    memory_.write(actor + actor_offset::tile_x,
                  std::uint32_t(signed32(memory_.read(actor + actor_offset::world_x)) / int(fixed_point) / int(tile_width)));
    memory_.write(actor + actor_offset::tile_y,
                  std::uint32_t(signed32(memory_.read(actor + actor_offset::world_y)) / int(fixed_point) / int(tile_height)));
}
void Pose::show(Address actor, const Appearance& appearance) {
    const auto flags = memory_.read(actor + actor_offset::flags);
    // The original edits only the low byte and stores the whole word back.
    memory_.write(actor + actor_offset::flags,
                  (flags & ~(appearance.clear_flags & 0xffu)) | (appearance.set_flags & 0xffu));
    memory_.write(actor + actor_offset::sprite_selector, appearance.selector);
    memory_.write(actor + actor_offset::sprite_frame, appearance.frame);
    memory_.write(actor + actor_offset::screen_anchor_x, 0);
    memory_.write(actor + actor_offset::screen_anchor_y, std::uint32_t(appearance.anchor_y));
}
bool Pose::alternate(Address actor) const {
    return (memory_.read(actor + actor_offset::flags) & alternate_appearance) != 0;
}
std::uint32_t Pose::facing_frame(Address actor, Address field, const std::uint8_t (&frames)[4]) const {
    const auto facing = memory_.read(actor + field);
    if (facing >= 4) throw Fault(actor, "battle pose facing outside four rotations");
    return frames[facing];
}

bool Pose::apply(Address entry, Address actor) {
    switch (entry) {
    case 0x447e31: // Restrained character: one sprite, two frames by appearance.
        locate(actor);
        show(actor, {0x20007, alternate(actor) ? 1u : 0u, 0});
        return true;
    case 0x447ea3: // Fixed NPC that also drops the shadow flag.
        locate(actor);
        show(actor, {0xf7, 0x2a, 0, 8, visible});
        return true;
    case 0x447ef9:locate(actor);show(actor, {0x20007, 0xe, 0x14});return true;
    case 0x447f4c:locate(actor);show(actor, {0x20007, 0xc, 0x14});return true;
    case 0x447f9f:locate(actor);show(actor, {0x20004, 0xc, 0});return true;
    case 0x447fef:locate(actor);show(actor, {0x20007, 0xd, 0x14});return true;
    case 0x448042:locate(actor);show(actor, {0x20004, 0xc, 0x14});return true;
    case 0x448095:locate(actor);show(actor, {0x20004, 0xd, 0x14});return true;
    case 0x4480e8: { // Fixed NPC placed from world pixels, facing its own way.
        locate_from_world(actor);
        constexpr std::uint8_t frames[4] = {2, 5, 8, 0xb};
        const auto facing = memory_.read(actor + actor_offset::facing);
        // A facing outside the four rotations leaves flags and sprite alone;
        // only the anchor is republished.
        if (facing < 4) show(actor, {0x20004, frames[facing], 0x14});
        else {
            memory_.write(actor + actor_offset::screen_anchor_x, 0);
            memory_.write(actor + actor_offset::screen_anchor_y, 0x14);
        }
        return true;
    }
    case 0x4481a9: // Two whole appearances rather than two frames.
        locate(actor);
        show(actor, alternate(actor) ? Appearance{0x20000, 6, 0} : Appearance{0x20004, 0xd, 0});
        return true;
    case 0x44821f: { // Idle pose; the frame is the actor's facing.
        locate(actor);
        constexpr std::uint8_t frames[4] = {0xe, 0xc, 0xd, 0xf};
        show(actor, {0x20003, facing_frame(actor, actor_offset::facing, frames), 0});
        return true;
    }
    case 0x44828a: { // Player pose; an unstarted callback also unhides the actor.
        if (signed32(memory_.read(actor + actor_offset::callback_state)) == -1)
            memory_.write(actor + actor_offset::flags,
                          (memory_.read(actor + actor_offset::flags) & ~4u) | 3u);
        locate(actor);
        show(actor, {0x27, alternate(actor) ? 0u : 1u, 0});
        return true;
    }
    case 0x44830f: { // Directional pose that also cycles a three-step animation.
        locate(actor);
        constexpr std::uint8_t frames[4] = {0, 6, 0xc, 0x12};
        const auto field = alternate(actor) ? actor_offset::target_facing : actor_offset::facing;
        const auto step = memory_.read(actor + actor_offset::callback_tick_count) / 5u % 3u;
        show(actor, {0x20000, facing_frame(actor, field, frames) + step, -0x10});
        return true;
    }
    case 0x4483b5: { // Alternate pose only when the sprite template allows it.
        locate(actor);
        const auto record = sprite_templates + memory_.read(actor + actor_offset::template_link) * sprite_template_stride;
        const bool use_alternate = alternate(actor) && !(memory_.read(record, 1) & 1);
        show(actor, use_alternate ? Appearance{0x20000, 6, 0} : Appearance{0x20004, 0xd, 0x14});
        return true;
    }
    case 0x44844e: { // Walk pose; both appearance branches use the same frames.
        locate(actor);
        constexpr std::uint8_t frames[4] = {1, 4, 7, 0xa};
        show(actor, {0x20004, facing_frame(actor, actor_offset::facing, frames), 0x14});
        return true;
    }
    case 0x4484f4: { // Effect-sheet pose keyed by facing.
        locate(actor);
        constexpr std::uint8_t frames[4] = {0, 4, 6, 2};
        show(actor, {0x10a, facing_frame(actor, actor_offset::facing, frames), 0});
        return true;
    }
    default:return false;
    }
}
} // namespace fsb::core::combat
