#include "fsb_core/actor_core/session_state.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/symbols.hpp"
#include <utility>

namespace fsb::core::actor_core {

bool probe_save_slot(const ReadSlotHeader& read_header, std::uint32_t slot) {
    return read_header && read_header(slot, save_slot_headers + slot * save_slot_header_bytes,
                                      save_slot_header_bytes);
}

std::uint32_t refresh_save_slot_presence(Memory& memory, const ReadSlotHeader& read_header) {
    std::uint32_t present = 0;
    for (auto slot = first_save_slot; slot < save_slot_limit; ++slot) {
        const auto exists = probe_save_slot(read_header, slot);
        memory.write(save_slot_present + slot, exists ? 1 : 0, 1);
        if (exists) ++present;
    }
    return present;
}

std::uint32_t release_cim_blob(Memory& memory, const ReleaseBuffer& release,
                               const DoubleReleaseReport& report) {
    const auto buffer = memory.read(cim_blob_pointer);
    if (!buffer) {
        if (report) report();
        return 0;
    }
    if (release) release(buffer);
    memory.write(cim_blob_pointer, 0);
    memory.write(cim_blob_size, 0);
    return 1;
}

std::uint32_t capture_transition_surface(Memory& memory, const SurfaceCapture& surface) {
    const auto phase = memory.read(transition_phase);
    if (phase == 0) {
        const auto cache = memory.read(transition_cache_surface);
        const auto [width, height] = surface.measure ? surface.measure(cache)
                                                     : std::pair<std::int32_t, std::int32_t>{0, 0};
        // The source rectangle is the one the video mode may letterbox; the
        // destination is always the whole surface.
        std::int32_t left = 0, top = 0, right = width, bottom = height;
        if ((memory.read(globals::video_mode_flags) & transition_adjust_flag) && surface.adjust)
            surface.adjust(left, top, right, bottom);
        if (surface.copy)
            surface.copy(cache, left, top, right, bottom, memory.read(globals::primary_surface));
        memory.write(transition_phase, phase + 1);
        memory.write(transition_menu_rows, 0);
        refresh_view_anchor_records(memory);
        return transition_working;
    }
    if (phase == 1) {
        if (surface.release_border) surface.release_border();
        memory.write(transition_phase, phase + 1);
        return transition_working;
    }
    return transition_done;
}

OverlayBlit overlay_sprite_blit(const Memory& memory, std::uint32_t index) {
    const auto entry = overlay_sprite_table + index * overlay_sprite_entry_bytes;
    const auto word = [&](unsigned at) { return signed32(memory.read(entry + at)); };
    const auto left = word(4), top = word(8);
    return {memory.read(overlay_sheet_table + memory.read(entry) * overlay_sheet_bytes),
            left, top, left + word(0xc), top + word(0x10), memory.read(entry + 0x14)};
}
} // namespace fsb::core::actor_core
