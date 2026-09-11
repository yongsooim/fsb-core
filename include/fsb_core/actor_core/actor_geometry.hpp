#pragma once
#include "actor_slots.hpp"

// Actor placement and the derived coordinate lanes.
//
// An actor carries the same position in three forms:
//   +0x08/+0x0c  world pixels, Q16
//   +0x14/+0x18/+0x1c  logical tile and layer, Q16
//   +0x128/+0x12c      integer tile cache used by sorting and lookups
// A tile is64 pixels wide and48 tall. Q16 tile coordinates carry the0x8000
// half-tile bias, so an actor placed on a tile sits at its centre.
namespace fsb::core::actor_core {

struct TilePosition { std::int32_t x = 0, y = 0, layer = 0; };
struct PixelPair { std::int32_t x = 0, y = 0; };

inline constexpr std::int32_t tile_width_pixels = 0x40;
inline constexpr std::int32_t tile_height_pixels = 0x30;
inline constexpr std::uint32_t tile_centre_bias = 0x8000;
// 43023e lifts the dialogue tail this many pixels above the actor anchor.
inline constexpr std::int32_t dialog_anchor_rise = 0x26;

// 45d82d: tile Q16 to world-pixel Q16, applied per axis with the tile size.
// A pure value conversion; the caller owns both endpoints, which may be actor
// record fields or a caller stack frame.
PixelPair tile_to_pixels(std::int32_t tile_x, std::int32_t tile_y);

// 43029b: rebuild the logical tile lanes and the integer cache from the world
// pixel position. Signed division truncates toward zero; the cache uses SAR16.
void refresh_tile_from_pixels(const GuestWords& words, Address object);

// 4301e1: write a world pixel position, then refresh the tile lanes from it.
void place_at_pixels(const GuestWords& words, Address object, std::uint32_t x, std::uint32_t y);

// 45d774: place a record on a tile. The layer is skipped when it is0xffffffff.
// Writes the Q16 tile lanes with the half-tile bias, the world pixel position
// derived from them, and the integer tile cache taken from the arguments.
void place_record_on_tile(const GuestWords& words, Address object,
                          std::uint32_t x, std::uint32_t y, std::uint32_t layer);

// 4303e5: place an actor on a tile and optionally turn it. The original passes
// the layer ahead of the tile axes; a facing of0xffffffff leaves the pose
// alone, and the tile cache is re-derived from the Q16 lanes afterwards.
void place_at_tile(const GuestWords& words, std::uint32_t selector, std::uint32_t layer,
                   std::uint32_t x, std::uint32_t y, std::uint32_t facing,
                   const ResolveActor& resolve, const MissingActorReport& report = {});

// 43023e: project the actor anchor into viewport space for the dialogue tail.
void refresh_screen_anchor(Memory& memory, const GuestWords& words, Address object);

// 430aa6/430ae2: script-facing readers. Each output address is optional, and a
// null one skips that axis rather than writing zero. The record itself may be a
// caller stack copy, so both sides go through the same word access.
void read_tile_position(const GuestWords& words, std::uint32_t selector,
                        Address out_x, Address out_y, Address out_layer,
                        const ResolveActor& resolve);
void read_pixel_position(const GuestWords& words, std::uint32_t selector,
                         Address out_x, Address out_y, Address out_elevation,
                         const ResolveActor& resolve);

// 45dc7f: re-anchor the live screen rectangles onto the current viewport
// centre. The original rewrites records1 and2 only; record0 is left alone.
void refresh_view_anchor_records(Memory& memory);

// 430385: copy one actor's placement and facing onto another.
void copy_placement(const GuestWords& words, std::uint32_t destination, std::uint32_t source,
                    const ResolveActor& resolve, const MissingActorReport& report = {});

} // namespace fsb::core::actor_core
