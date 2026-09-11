#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/sprites.hpp"
#include "fsb_core/progress_timer.hpp"
#include <algorithm>

namespace fsb::core {
void DialogGraphics::reaction(unsigned variant,int x,int y){
    if(variant>1||!sprites)throw Fault(0x4315e7,"reaction sprite bank is not connected");
    const auto frame=sprites->character(memory_.read(globals::reaction_family_first_frame)+variant);
    const auto target=memory_.read(globals::render_target_surface);
    surfaces_.blit(target,signed32(std::uint32_t(x)-std::uint32_t(frame.origin_x)),signed32(std::uint32_t(y)-std::uint32_t(frame.origin_y)),frame.surface,frame.rect,true,clip(target));
}
void DialogGraphics::load_skin(const std::vector<std::uint8_t>& pcx) {
    auto image=Image8::pcx(pcx);if(image.width!=512||image.height!=480)throw Fault(0,"expected original WHDLGBOX512x480");
    if(skin_)surfaces_.release(skin_);skin_=surfaces_.insert(std::move(image));memory_.write(globals::dialogue_skin_surface,skin_);
}
void DialogGraphics::load_arrows(const std::vector<std::uint8_t>& pcx){auto image=Image8::pcx(pcx);if(image.width!=512||image.height!=64)throw Fault(0,"expected original WHARROW512x64");if(arrows_)surfaces_.release(arrows_);arrows_=surfaces_.insert(std::move(image));memory_.write(globals::dialogue_arrow_surface,arrows_);}
std::optional<Rect> DialogGraphics::clip(Address dst)const{if(dst!=memory_.read(globals::render_target_surface))return std::nullopt;return Rect{signed32(memory_.read(globals::clip_left)),signed32(memory_.read(globals::clip_top)),signed32(memory_.read(globals::clip_right)),signed32(memory_.read(globals::clip_bottom))};}
void DialogGraphics::destroy(Address d) {for(auto offset:{0x88,0x8c,0x90}){surfaces_.release(memory_.read(d+offset));memory_.write(d+offset,0);}}
void DialogGraphics::sprite(Address dst,int x,int y,Address src,unsigned frame,bool key,int source_x,int source_y) {
    if(frame>=64)throw Fault(frame,"dialog sprite outside atlas table");
    const auto at=tables::dialogue_atlas_frames+frame*32;
    const int sx=signed32(memory_.read(at+4)),sy=signed32(memory_.read(at+8)),w=signed32(memory_.read(at+12)),h=signed32(memory_.read(at+16));
    surfaces_.blit(dst,x-signed32(memory_.read(at+20)),y-signed32(memory_.read(at+24)),src,{sx+source_x,sy+source_y,sx+w+source_x,sy+h+source_y},key,clip(dst));
}
void DialogGraphics::layout(Address d) {
    if(!skin_)throw Fault(d,"dialog skin is not loaded");
    destroy(d);const auto width=memory_.read(d+dialog_offset::width),height=memory_.read(d+dialog_offset::height),family=memory_.read(d+dialog_offset::box_style);
    if(family>4||width<32||width>512||height<=10)throw Fault(d,"invalid dialog surface layout");
    const auto atlas=surfaces_.create(512,112),body=surfaces_.create(width,height),reveal=surfaces_.create(width-31,height-10);
    memory_.write(d+dialog_offset::skin_surface,atlas);memory_.write(d+dialog_offset::text_surface,body);memory_.write(d+dialog_offset::reveal_surface,reveal);
    constexpr int rows[]={192,96,0,288,384};const int row=rows[family];
    surfaces_.blit(atlas,0,0,skin_,{0,row,512,row+96});
    auto& pixels=surfaces_.get(atlas).pixels;
    const auto remap=[&](unsigned from,unsigned to){for(auto& pixel:pixels)if(pixel==from)pixel=std::uint8_t(to);};
    remap(240,memory_.read(d+dialog_offset::foreground,1));remap(241,memory_.read(d+dialog_offset::outline,1));
    if(!family){remap(242,0);remap(243,0);}
    else if(family<=2) {
        const auto base=memory_.read(0x57edb0+family*4)+memory_.read(d+dialog_offset::style_variant)*2;
        remap(242,memory_.read(0x57edd4+base*32,1));
        remap(243,memory_.read(0x57edd4+(base*8-1+((memory_.read(d+dialog_offset::text_style)&256)?2:0))*4,1));
        for(unsigned i=0;i<3;++i)remap(244+i,memory_.read(0x57eddc+(base*8+i)*4,1));
    }
    if(const auto length=memory_.read(d+dialog_offset::speaker_name_length)){
        //4117fa: font9, eight one-pixel outline passes, then fill; keep the
        //name in the atlas's96..112 band so normal tail rendering can reuse it.
        std::string name;
        for(unsigned i=0;i<length;++i)name.push_back(char(memory_.read(d+dialog_offset::speaker_name+i,1)));
        const Rect band{0,96,int(length)*8,112};surfaces_.clear(atlas,band,0);
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)if(x||y)
            plain_text(atlas,{x,96+y,band.right+x,112+y},9,254,name,0x125);
        plain_text(atlas,band,9,255,name,0x125);
    }
    const int left=signed32(memory_.read(d+dialog_offset::source_left)),top=signed32(memory_.read(d+dialog_offset::source_top)),right=left+int(width)-16,bottom=signed32(memory_.read(d+dialog_offset::source_bottom))-16;
    for(auto pair:{std::pair<int,int>{top,0},{bottom,48}}) {
        surfaces_.blit(body,left,pair.first,atlas,{0,pair.second,int(width)-16,pair.second+16});
        surfaces_.blit(body,right,pair.first,atlas,{496,pair.second,512,pair.second+16});
    }
    for(unsigned line=0;line<memory_.read(d+dialog_offset::line_capacity);++line) {
        surfaces_.blit(body,left,top+16+int(line)*22,atlas,{0,16,int(width)-16,38});
        surfaces_.blit(body,right,top+16+int(line)*22,atlas,{496,16,512,38});
    }
    surfaces_.blit(reveal,0,int(height)-10-22,atlas,{15,16,int(width)-16,38});
}
void DialogGraphics::restore_tail(Address d) {
    const auto frame=memory_.read(d+dialog_offset::saved_tail_frame);if(!frame)return;
    sprite(memory_.read(d+dialog_offset::text_surface),signed32(memory_.read(d+dialog_offset::saved_tail_x)),signed32(memory_.read(d+dialog_offset::saved_tail_y)),memory_.read(d+dialog_offset::skin_surface),frame);
    memory_.write(d+dialog_offset::saved_tail_frame,0);
}
void DialogGraphics::draw_glyph(Address d,const DialogGlyph& glyph) {
    if(!glyph_renderer)throw Fault(d,"portable glyph rasterizer is not attached");
    glyph_renderer(surfaces_.get(memory_.read(d+dialog_offset::text_surface)),glyph);
}
void DialogGraphics::inline_sprite(Address d,unsigned id,bool face){
    if(!sprites)throw Fault(d,"inline sprite bank is not connected");
    const auto destination=memory_.read(d+dialog_offset::text_surface);if(!destination)throw Fault(d,"inline sprite requires a text surface");
    const int x=signed32(memory_.read(d+dialog_offset::source_left))+16+signed32(memory_.read(d+dialog_offset::column))*8;
    const int y=signed32(memory_.read(d+dialog_offset::source_top))+19+signed32(memory_.read(d+dialog_offset::line))*22;
    if(face){
        if(id<107)throw Fault(id,"inline face id outside original metadata table");
        const auto record=0x5d1da0+(id-107)*24,surface=memory_.read(0x804d0c+memory_.read(record)*68);
        const int left=signed32(memory_.read(record+4)),top=signed32(memory_.read(record+8));
        surfaces_.blit(destination,x+4,y,surface,{left,top,left+signed32(memory_.read(record+12)),top+signed32(memory_.read(record+16))},true);
    }else{
        const auto sprite=sprites->sheet(0x804ddc,id);const auto style=memory_.read(d+dialog_offset::text_style);
        Rect target{x+2,y-2,x+22,y+18};
        if(style&0x1000)target={x+2,y-23,x+42,y+17};else if(style&0x2000)target={x+2,y-1,x+42,y+39};
        sprites->upload_inline_item_palette();surfaces_.stretch(destination,target,sprite.surface,sprite.rect,true);
    }
}
void DialogGraphics::body(Address d,Rect dst) {
    const Rect src{signed32(memory_.read(d+dialog_offset::source_left)),signed32(memory_.read(d+dialog_offset::source_top)),signed32(memory_.read(d+dialog_offset::source_right)),signed32(memory_.read(d+dialog_offset::source_bottom))};
    surfaces_.stretch(memory_.read(globals::render_target_surface),dst,memory_.read(d+dialog_offset::text_surface),src,true,clip(memory_.read(globals::render_target_surface)));
}
void DialogGraphics::capture_reveal(Address d) {
    const Rect rect{signed32(memory_.read(d+dialog_offset::source_left))+15,signed32(memory_.read(d+dialog_offset::source_top))+16,signed32(memory_.read(d+dialog_offset::source_right))-16,signed32(memory_.read(d+dialog_offset::source_bottom))-16};
    surfaces_.blit(memory_.read(d+dialog_offset::reveal_surface),0,0,memory_.read(d+dialog_offset::text_surface),rect);
}
void DialogGraphics::reveal(Address d,unsigned progress,bool paragraph) {
    const int width=signed32(memory_.read(d+dialog_offset::width)),height=signed32(memory_.read(d+dialog_offset::height));
    int offset=int(std::uint64_t(progress)*22/units::progress_complete);
    if(memory_.read(d+dialog_offset::box_style)>=1&&memory_.read(d+dialog_offset::box_style)<=3)offset=offset/2*2;
    if(!paragraph) surfaces_.blit(memory_.read(d+dialog_offset::text_surface),signed32(memory_.read(d+dialog_offset::source_left))+15,signed32(memory_.read(d+dialog_offset::source_top))+16,memory_.read(d+dialog_offset::reveal_surface),{0,offset,width-31,height-32+offset});
    else{
        const int bottom=int(std::uint64_t(progress)*22/units::progress_complete)+16;memory_.write(0x57f988,unsigned(width-15));memory_.write(0x57f98c,unsigned(bottom));
        const Rect source{signed32(memory_.read(0x57f980)),signed32(memory_.read(0x57f984)),width-15,bottom};
        for(unsigned line=0;line<memory_.read(d+dialog_offset::line_capacity);++line)surfaces_.blit(memory_.read(d+dialog_offset::text_surface),signed32(memory_.read(d+dialog_offset::source_left))+15,signed32(memory_.read(d+dialog_offset::source_top))+16+int(line)*22,memory_.read(d+dialog_offset::skin_surface),source);
    }
}
void DialogGraphics::occupy(Address d) {
    const auto count=memory_.read(globals::occupied_dialogue_count);if(count>=32)return;
    const auto at=globals::occupied_dialogue_rects+count*16;
    for(unsigned i=0;i<4;++i)memory_.write(at+i*4,memory_.read(d+dialog_offset::target_rect+i*4)+(i<2?16u:0xfffffff0u));
    memory_.write(globals::occupied_dialogue_count,count+1);
}
void DialogGraphics::draw_speaker_name(Address d,bool occupying){
    const auto atlas=memory_.read(d+dialog_offset::skin_surface),length=memory_.read(d+dialog_offset::speaker_name_length);
    if(!atlas||!length||!(memory_.read(d+dialog_offset::flags_b)&16))return;
    const bool actor_anchor=memory_.read(d)!=0;
    if(!actor_anchor&&!occupying)return;
    const int x=signed32(memory_.read(d+dialog_offset::anchor_x))-int(length)*4+1;
    const int y=actor_anchor?signed32(memory_.read(d+dialog_offset::anchor_y))+32:signed32(memory_.read(d+dialog_offset::target_rect+12))+2;
    const auto target=memory_.read(globals::render_target_surface);
    surfaces_.blit(target,x,y,atlas,{0,96,int(length)*8,112},true,clip(target));
}
void DialogGraphics::tail(Address d){
    if(!(memory_.read(d+dialog_offset::flags_a)&1))return;
    const auto target=memory_.read(globals::render_target_surface),body_id=memory_.read(d+dialog_offset::text_surface),atlas=memory_.read(d+dialog_offset::skin_surface);
    const int left=signed32(memory_.read(d+dialog_offset::current_rect)),top=signed32(memory_.read(d+0xc8));
    const int tx=signed32(memory_.read(d+dialog_offset::tail_x)),ty=signed32(memory_.read(d+dialog_offset::tail_y));
    const Rect body_source{signed32(memory_.read(d+dialog_offset::source_left)),signed32(memory_.read(d+dialog_offset::source_top)),signed32(memory_.read(d+dialog_offset::source_right)),signed32(memory_.read(d+dialog_offset::source_bottom))};
    const auto draw_body=[&](){surfaces_.blit(target,left,top,body_id,body_source,true,clip(target));};
    const auto frame=memory_.read(d+dialog_offset::tail_frame);
    if(!frame){memory_.write(d+dialog_offset::previous_tail_frame,0);draw_body();}
    else{
        if(frame<26||frame>33)throw Fault(d,"invalid dialog tail frame");
        if(memory_.read(d+dialog_offset::previous_tail_frame)!=frame){ProgressTimer(memory_,d+dialog_offset::tail_timer).start(memory_.read(tables::dialogue_tail_duration),false,false,memory_.read(globals::frame_time_ms));memory_.write(d+dialog_offset::flags_c,memory_.read(d+dialog_offset::flags_c)|8);memory_.write(d+dialog_offset::previous_tail_frame,frame);}
        if(memory_.read(d+dialog_offset::flags_c)&8){const auto progress=ProgressTimer(memory_,d+dialog_offset::tail_timer).tick(memory_.read(globals::frame_time_ms));
            if(!progress)memory_.write(d+dialog_offset::flags_c,memory_.read(d+dialog_offset::flags_c)&~8u);
            else{draw_body();const auto at=tables::dialogue_atlas_frames+frame*32;const int move=int(progress*14/units::progress_complete),sx=signed32(memory_.read(at+4)),sy=signed32(memory_.read(at+8));
                Rect sr{sx,sy,sx+signed32(memory_.read(at+12)),sy+signed32(memory_.read(at+16))};int dx=tx-signed32(memory_.read(at+20)),dy=ty-signed32(memory_.read(at+24));
                if(frame<=28)sr.top+=move;else if(frame==29)sr.left+=move;else if(frame==30){sr.right-=move;dx+=move;}else{sr.bottom-=move;dy+=move;}
                surfaces_.blit(target,dx,dy,atlas,sr,true,clip(target));
            }
        }
        if(!(memory_.read(d+dialog_offset::flags_c)&8)){
            const int dx=tx-left,dy=ty-top;memory_.write(d+dialog_offset::saved_tail_x,unsigned(dx));memory_.write(d+dialog_offset::saved_tail_y,unsigned(dy));Rect mouth{};unsigned mouth_frame;
            if(frame<=28){mouth={dx,dy-3,dx+16,dy};mouth_frame=35;}
            else if(frame==29){mouth={dx-3,dy,dx,dy+16};mouth_frame=37;}
            else if(frame==30){mouth={dx+16,dy,dx+19,dy+16};mouth_frame=36;}
            else{mouth={dx,dy+16,dx+16,dy+19};mouth_frame=34;}
            surfaces_.clear(body_id,mouth);memory_.write(d+dialog_offset::saved_tail_frame,mouth_frame);
            const auto family=memory_.read(d+dialog_offset::box_style);const int offset=(family>0&&family<3&&((dx+dy)&1))?160:0;
            //160 is an alternate atlas X offset, not alpha/transparency.
            sprite(target,tx,ty,atlas,frame,true,offset);draw_body();
        }
    }
    const auto flags=memory_.read(d+dialog_offset::flags_c);const int column=signed32(memory_.read(d+dialog_offset::column)),line=signed32(memory_.read(d+dialog_offset::line));
    if((flags&3)!=2&&!(flags&0x7f0)&&!(memory_.read(d+dialog_offset::flags_a)&4))sprite(target,left+16+column*8,top+line*22+19,atlas,39);
    if((flags&32)&&!(flags&0x800)){
        if(!arrows_)throw Fault(d,"continue indicator sheet is not loaded");
        const int capacity=signed32(memory_.read(d+dialog_offset::column_capacity)),col=std::min(column,capacity-2);
        const auto blink=memory_.read(globals::frame_time_ms)/100%6+4;
        sprite(target,left+16+col*8,top+19+line*22,arrows_,blink,true,column+2>capacity?64:128,memory_.read(d+dialog_offset::box_style)>=2?16:0);
    }
    if((flags&64)&&!(flags&0x800)){
        if(!arrows_)throw Fault(d,"choice indicator sheet is not loaded");
        sprite(target,left+16,top+(line+signed32(memory_.read(d+dialog_offset::choice_cursor))-signed32(memory_.read(d+dialog_offset::choice_scroll)))*22+42,arrows_,24);
    }
}
} // namespace fsb::core
