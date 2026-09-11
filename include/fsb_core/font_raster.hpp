#pragma once
#include "dialog_graphics.hpp"
#include <memory>

namespace fsb::core {
// Optional portable FreeType renderer. It consumes the original Windows95
// font bytes and CP949 table supplied by the host; it never finds system fonts.
class FontRaster {
public:
    explicit FontRaster(const Memory& memory);
    ~FontRaster();
    FontRaster(const FontRaster&)=delete;
    FontRaster& operator=(const FontRaster&)=delete;
    void load(std::vector<std::uint8_t> gulim_ttc,std::vector<std::uint8_t> batang_ttc,std::vector<std::uint8_t> cp949_table);
    void draw(Image8& image,const DialogGlyph& glyph);
    void set_enhanced(bool enabled); // Adds8x outline samples; legacy metrics/pixels stay unchanged.
    int advance(unsigned font,std::uint16_t cp949);
    unsigned bitmap_glyphs() const;
    unsigned outline_glyphs() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace fsb::core
