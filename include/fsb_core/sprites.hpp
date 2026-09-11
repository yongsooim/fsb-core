#pragma once
#include "surfaces.hpp"
#include "palette.hpp"

namespace fsb::core {
struct SpriteSurface { Address surface; Rect rect; int origin_x, origin_y; };
class Sprites {
public:
    Sprites(Memory& memory, Surfaces& surfaces, Palette& palette) : memory_(memory), surfaces_(surfaces), palette_(palette) {}
    void register_pcx(std::string resource, std::vector<std::uint8_t> bytes);
    void register_image(std::string resource,std::vector<std::uint8_t> bytes);
    void initialize(); // Original noimage, default character palette and family-index setup.
    void initialize_scene_sheets(); // 0x45f682 sheets/palettes, excluding later menu/panel creation.
    void upload_inline_item_palette();
    Address decode_surface(Address resource,Address palette=0,unsigned format=0);
    void decode_palette(Address resource,Address destination,unsigned format=0);
    void cache_sheet(Address definition, Address resource_name, unsigned frame_width, unsigned frame_height, std::uint32_t tag=0);
    void replace_sheet(Address definition, const std::string& resource, const std::vector<std::uint8_t>& bytes, unsigned frame_width, unsigned frame_height);
    SpriteSurface character(unsigned frame);
    SpriteSurface indexed(unsigned sheet, std::uint32_t frame);
    bool prepare_indexed(unsigned sheet); // Original40bb5b: true if already prepared.
    SpriteSurface sheet(Address definition, std::uint32_t frame);
    SpriteSurface overlay(std::uint32_t frame) const; //454cb5:24-byte metadata,68-byte overlay sheet slots.
    bool load_sheet(Address definition);
    void ensure_palette(unsigned sheet);
    std::uint32_t flush_deferred();
private:
    Memory& memory_; Surfaces& surfaces_; Palette& palette_;
    std::map<std::string, std::vector<std::uint8_t>> assets_;
    std::string name(Address record) const;
    Image8 decode(Address record,unsigned format=0) const;
    bool load_character(unsigned sheet, bool palette_output = false);
    void cache_frame(unsigned frame, unsigned sheet);
    void deferred(std::uint32_t value);
    void refresh_overlay();
};
} // namespace fsb::core
