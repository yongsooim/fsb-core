#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
// 45dc7f rewrites the live screen rectangles from the stored ones. Both tables
// hold three dwords per record; the original touches records1 and2.
constexpr Address stored_anchor_records = 0x5d2184;
constexpr Address live_anchor_records = 0x803804;
constexpr std::int32_t anchor_reference_half_width = 0x140;
constexpr std::int32_t anchor_reference_half_height = 0xf0;
constexpr unsigned anchor_record_bytes = 0xc;

std::int32_t field(const GuestWords& words, Address object, unsigned offset) {
    return signed32(words.read(object + offset));
}
// The original halves the viewport size with cdq/sub/sar, which truncates
// toward zero rather than flooring the way a shift alone would.
std::int32_t half_toward_zero(std::int32_t value) { return value / 2; }

// Both readers publish each requested axis, so a null output address leaves
// that slot untouched instead of writing a default.
void read_axes(const GuestWords& words, std::uint32_t selector, unsigned first_offset,
               Address out_x, Address out_y, Address out_z, const ResolveActor& resolve) {
    const auto object = resolve(selector);
    const Address outputs[] = {out_x, out_y, out_z};
    for (unsigned axis = 0; axis < 3; ++axis)
        if (outputs[axis])
            words.write(outputs[axis], std::uint32_t(field(words, object, first_offset + axis * 4) >> 16));
}
} // namespace

PixelPair tile_to_pixels(std::int32_t tile_x, std::int32_t tile_y) {
    return {std::int32_t(std::uint32_t(tile_x) * std::uint32_t(tile_width_pixels)),
            std::int32_t(std::uint32_t(tile_y) * std::uint32_t(tile_height_pixels))};
}

void refresh_tile_from_pixels(const GuestWords& words, Address object) {
    const auto tile_x = field(words, object, actor_offset::world_x) / tile_width_pixels;
    const auto tile_y = field(words, object, actor_offset::world_y) / tile_height_pixels;
    words.write(object + actor_offset::tile_x_q16, std::uint32_t(tile_x));
    words.write(object + actor_offset::tile_x, std::uint32_t(tile_x >> 16));
    words.write(object + actor_offset::tile_y_q16, std::uint32_t(tile_y));
    words.write(object + actor_offset::tile_y, std::uint32_t(tile_y >> 16));
}

void place_at_pixels(const GuestWords& words, Address object, std::uint32_t x, std::uint32_t y) {
    words.write(object + actor_offset::world_x, x);
    words.write(object + actor_offset::world_y, y);
    refresh_tile_from_pixels(words, object);
}

void place_record_on_tile(const GuestWords& words, Address object,
                          std::uint32_t x, std::uint32_t y, std::uint32_t layer) {
    const auto centred = [](std::uint32_t tile) { return (tile << 16) + tile_centre_bias; };
    words.write(object + actor_offset::tile_x_q16, centred(x));
    words.write(object + actor_offset::tile_y_q16, centred(y));
    if (layer != 0xffffffffu) words.write(object + actor_offset::layer_q16, centred(layer));
    const auto pixels = tile_to_pixels(field(words, object, actor_offset::tile_x_q16),
                                       field(words, object, actor_offset::tile_y_q16));
    words.write(object + actor_offset::world_x, std::uint32_t(pixels.x));
    words.write(object + actor_offset::world_y, std::uint32_t(pixels.y));
    // The integer cache takes the arguments directly, not the biased lanes.
    words.write(object + actor_offset::tile_x, x);
    words.write(object + actor_offset::tile_y, y);
}

void place_at_tile(const GuestWords& words, std::uint32_t selector, std::uint32_t layer,
                   std::uint32_t x, std::uint32_t y, std::uint32_t facing,
                   const ResolveActor& resolve, const MissingActorReport& report) {
    const auto object = resolve_or_report(selector, resolve, report);
    place_record_on_tile(words, object, x, y, layer);
    // Re-derive the cache from the Q16 lanes; the half-tile bias shifts out.
    words.write(object + actor_offset::tile_x,
                std::uint32_t(field(words, object, actor_offset::tile_x_q16) >> 16));
    words.write(object + actor_offset::tile_y,
                std::uint32_t(field(words, object, actor_offset::tile_y_q16) >> 16));
    if (facing == 0xffffffffu) return;
    set_both_facings(words, object, facing);
    reset_pose_row(words, object);
}

void refresh_screen_anchor(Memory& memory, const GuestWords& words, Address object) {
    const auto x = std::uint32_t(field(words, object, actor_offset::world_x) +
                                field(words, object, actor_offset::draw_offset_x)) >> 16;
    const auto y = std::uint32_t(field(words, object, actor_offset::world_y) +
                                field(words, object, actor_offset::draw_offset_y) -
                                field(words, object, actor_offset::elevation)) >> 16;
    const auto anchor_x = std::int32_t(x) + half_toward_zero(signed32(memory.read(globals::viewport_width))) -
                          signed32(memory.read(globals::camera_x)) + signed32(memory.read(globals::viewport_left));
    const auto anchor_y = std::int32_t(y) + half_toward_zero(signed32(memory.read(globals::viewport_height))) -
                          signed32(memory.read(globals::camera_y)) - dialog_anchor_rise +
                          signed32(memory.read(globals::viewport_top));
    words.write(object + actor_offset::screen_anchor_x, std::uint32_t(anchor_x));
    words.write(object + actor_offset::screen_anchor_y, std::uint32_t(anchor_y));
}

void read_tile_position(const GuestWords& words, std::uint32_t selector,
                        Address out_x, Address out_y, Address out_layer,
                        const ResolveActor& resolve) {
    read_axes(words, selector, actor_offset::tile_x_q16, out_x, out_y, out_layer, resolve);
}

void read_pixel_position(const GuestWords& words, std::uint32_t selector,
                         Address out_x, Address out_y, Address out_elevation,
                         const ResolveActor& resolve) {
    read_axes(words, selector, actor_offset::world_x, out_x, out_y, out_elevation, resolve);
}

void refresh_view_anchor_records(Memory& memory) {
    const auto centre_x = signed32(memory.read(globals::viewport_center_x));
    const auto centre_y = signed32(memory.read(globals::viewport_center_y));
    for (unsigned record = 1; record <= 2; ++record) {
        const auto at = record * anchor_record_bytes;
        const auto stored = stored_anchor_records + at, live = live_anchor_records + at;
        memory.write(live, std::uint32_t(signed32(memory.read(stored)) + centre_x - anchor_reference_half_width));
        memory.write(live + 4, std::uint32_t(signed32(memory.read(stored + 4)) + centre_y - anchor_reference_half_height));
        memory.write(live + 8, memory.read(stored + 8));
    }
}

void copy_placement(const GuestWords& words, std::uint32_t destination, std::uint32_t source,
                    const ResolveActor& resolve, const MissingActorReport& report) {
    const auto to = resolve_or_report(destination, resolve, report);
    const auto from = resolve_or_report(source, resolve, report);
    if (to == from) return;
    for (unsigned offset : {actor_offset::world_x, actor_offset::world_y, actor_offset::elevation,
                            actor_offset::draw_offset_x, actor_offset::draw_offset_y, 0x28,
                            actor_offset::tile_x_q16, actor_offset::tile_y_q16, actor_offset::layer_q16,
                            actor_offset::tile_x, actor_offset::tile_y, actor_offset::facing})
        words.write(to + offset, words.read(from + offset));
}
} // namespace fsb::core::actor_core
