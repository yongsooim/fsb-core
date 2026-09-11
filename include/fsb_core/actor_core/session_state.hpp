#pragma once
#include "actor_slots.hpp"

// Save slot presence, the CIM blob the save path borrows, and the overlay blit
// the transition uses. These sit in the same address range as the actor work
// and were assigned with it; their file and blit endpoints stay services.
namespace fsb::core::actor_core {

// Slots1..8. Each header is0x30 bytes; the presence byte mirrors it.
inline constexpr Address save_slot_names = 0x5d24e0;
inline constexpr Address save_slot_headers = 0x804ac0;
inline constexpr unsigned save_slot_header_bytes = 0x30;
inline constexpr Address save_slot_present = 0x803840;
inline constexpr unsigned first_save_slot = 1, save_slot_limit = 9;

// 460deb: read one slot header. The caller supplies the file service, which
// returns false when the slot file is absent; nothing is written then.
using ReadSlotHeader = std::function<bool(std::uint32_t slot, Address destination,
                                          unsigned bytes)>;
bool probe_save_slot(const ReadSlotHeader& read_header, std::uint32_t slot);
// 460e35: refresh every slot's presence byte and return how many exist.
std::uint32_t refresh_save_slot_presence(Memory& memory, const ReadSlotHeader& read_header);

// 4616e2: release the CIM blob. The original reports a double release through
// the shared log and answers0, so that stays visible rather than silent.
inline constexpr Address cim_blob_pointer = 0x804c7c;
inline constexpr Address cim_blob_size = 0x804c80;
using ReleaseBuffer = std::function<void(Address buffer)>;
using DoubleReleaseReport = std::function<void()>;
std::uint32_t release_cim_blob(Memory& memory, const ReleaseBuffer& release,
                               const DoubleReleaseReport& report = {});

// 45dc18: one entry of the special overlay table at0x5d1da0. Six words per
// entry: sheet slot, left, top, width, height and blit flags. The rectangle
// the blit wants is the origin plus the extent, so it is built here and the
// caller only has to place it where its blit service expects.
inline constexpr Address overlay_sprite_table = 0x5d1da0;
inline constexpr unsigned overlay_sprite_entry_bytes = 0x18;
inline constexpr Address overlay_sheet_table = 0x804d0c;
inline constexpr unsigned overlay_sheet_bytes = 0x44;
struct OverlayBlit {
    Address surface = 0;
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
    std::uint32_t flags = 0;
};
OverlayBlit overlay_sprite_blit(const Memory& memory, std::uint32_t index);

// 45dcc8: the two-step capture the menu transition runs before it fades. Step
// 0 copies the whole front surface into the tileset cache slot and re-anchors
// the screen rectangles; step1 releases the border. Both answer5, meaning
// "still working"; once the phase is past them the answer is6.
//
// The surface size, the optional letterbox adjust, the copy and the release
// are platform services the caller supplies. Everything this routine decides
// is the phase and the rectangle, which is the whole surface.
inline constexpr Address transition_phase = 0x80384c;
inline constexpr Address transition_cache_surface = 0x804cc8;
inline constexpr std::uint32_t transition_working = 5;
inline constexpr std::uint32_t transition_done = 6;
struct SurfaceCapture {
    // Reports the surface extent; the rectangle is (0,0) to (width, height).
    std::function<std::pair<std::int32_t, std::int32_t>(Address surface)> measure;
    // Applied to the rectangle when the video mode asks for it.
    std::function<void(std::int32_t& left, std::int32_t& top,
                       std::int32_t& right, std::int32_t& bottom)> adjust;
    std::function<void(Address destination, std::int32_t left, std::int32_t top,
                       std::int32_t right, std::int32_t bottom, Address source)> copy;
    std::function<void()> release_border;
};
// Bit3 of the video mode flags selects the letterbox adjust.
inline constexpr std::uint32_t transition_adjust_flag = 8;
// The capture also empties the item menu row list before it re-anchors.
inline constexpr Address transition_menu_rows = 0x802cb8;
std::uint32_t capture_transition_surface(Memory& memory, const SurfaceCapture& surface);

} // namespace fsb::core::actor_core
