#pragma once
#include "primitives.hpp"

namespace fsb::core {
struct Rgb { std::uint8_t r = 0, g = 0, b = 0; };
struct Rect { int left = 0, top = 0, right = 0, bottom = 0; };
// Optional presentation detail. The original indexed pixels remain the
// authoritative640x480 output; only cells touched by high-resolution ink need
// a tile. Palette indices preserve palette animation and colour-key semantics.
struct SubpixelImage {
    static constexpr unsigned scale=4;
    using Tile=std::array<std::uint8_t,scale*scale>;
    std::vector<unsigned> cells,free_tiles;
    std::vector<Tile> tiles;
};
struct Image8 {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    std::array<Rgb, 256> palette{};
    std::optional<SubpixelImage> detail;
    bool has_detail()const{return detail&&detail->tiles.size()!=detail->free_tiles.size();}
    // The scaled blit calls these once per destination pixel, so they are inline
    // here rather than across a translation unit boundary; the bodies are the
    // originals unchanged.
    const SubpixelImage::Tile* detail_cell(std::size_t index) const {
        if(!detail||!detail->cells[index])return nullptr;return &detail->tiles[detail->cells[index]-1];
    }
    void set_detail_cell(std::size_t index,const SubpixelImage::Tile& tile){
        if(!detail){detail.emplace();detail->cells.resize(pixels.size());}
        auto& id=detail->cells[index];
        if(!id){if(detail->free_tiles.empty()){detail->tiles.push_back(tile);id=unsigned(detail->tiles.size());return;}id=detail->free_tiles.back();detail->free_tiles.pop_back();}
        detail->tiles[id-1]=tile;
    }
    void clear_detail_cell(std::size_t index){
        if(detail&&detail->cells[index]){detail->free_tiles.push_back(detail->cells[index]);detail->cells[index]=0;}
    }
    void set_pixel(std::size_t index,std::uint8_t value);
    void set_detail_pixel(unsigned x,unsigned y,std::uint8_t value);
    std::uint8_t detail_pixel(unsigned x,unsigned y) const;
    static Image8 pcx(const std::vector<std::uint8_t>& bytes);
    static Image8 bmp(const std::vector<std::uint8_t>& bytes);
};
struct SpriteFrame {
    // 64-byte effect/character-frame records, not the 0x44-byte actor catalog.
    std::string resource;
    std::uint32_t bank = 0;
    int x = 0, y = 0, width = 0, height = 0, origin_x = 0, origin_y = 0;
    static SpriteFrame read(const Memory& memory, Address record);
};
void draw_sprite(Image8& target, const Image8& source, const SpriteFrame& frame, int anchor_x, int anchor_y,
                 bool color_key = true, std::optional<Rect> clip = std::nullopt);
// Original4390e7/439237 checker fill and transparent foreground tile passes.
void draw_checker_tile(Image8& target,const Image8& source,Rect source_rect,int x,int y,
                       bool transparent,std::uint8_t fill,std::optional<Rect> clip=std::nullopt);
} // namespace fsb::core
