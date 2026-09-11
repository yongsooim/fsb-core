#pragma once
#include "surfaces.hpp"
#include <functional>

namespace fsb::core {
class Sprites;
class Palette;
struct DialogGlyph {
    std::uint16_t cp949;
    unsigned font_index;
    Rect rect;
    std::uint32_t style, flags_b;
    std::uint8_t foreground, outline;
    unsigned radius;
    bool no_prefix=false;
    std::optional<Rect> clip=std::nullopt;
};
class DialogGraphics {
public:
    DialogGraphics(Memory& memory,Surfaces& surfaces) : memory_(memory),surfaces_(surfaces) {}
    void load_skin(const std::vector<std::uint8_t>& pcx);
    void load_arrows(const std::vector<std::uint8_t>& pcx);
    void layout(Address state);
    void destroy(Address state);
    void restore_tail(Address state);
    void position(Address controller,Address state);
    void draw_glyph(Address state,const DialogGlyph& glyph);
    void inline_sprite(Address state,unsigned id,bool face);
    void reveal(Address state,unsigned progress,bool paragraph);
    void capture_reveal(Address state);
    void body(Address state,Rect destination);
    void tail(Address state);
    void occupy(Address state);
    void draw_speaker_name(Address state,bool occupying);
    void reaction(unsigned variant,int anchor_x,int anchor_y);
    void show_direct_text(Address text);
    void clear_direct_text();
    void tick_direct_text(Address object,unsigned jobs);
    void menu_text(Address surface,int x,int y,unsigned font,std::uint32_t foreground,std::uint32_t outline,const std::string& text);
    unsigned plain_text(Address surface,Rect box,unsigned font,std::uint8_t color,const std::string& text,unsigned flags=0,std::optional<Rect> clip=std::nullopt,std::optional<std::uint8_t> background=std::nullopt);
    void queue_text_overlay(int x,int y,std::uint32_t color,std::uint32_t background,const std::string& text,unsigned layer);
    void text_overlays(const Palette& palette);
    Sprites* sprites=nullptr;
    std::function<void(Image8&,const DialogGlyph&)> glyph_renderer;
    std::function<int(unsigned,std::uint16_t)> glyph_advance;
private:
    Memory& memory_;Surfaces& surfaces_;Address skin_=0,arrows_=0;
    void sprite(Address destination,int x,int y,Address source,unsigned frame,bool key=true,int source_x=0,int source_y=0);
    std::optional<Rect> clip(Address destination)const;
};
} // namespace fsb::core
