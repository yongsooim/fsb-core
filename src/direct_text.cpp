#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/arena.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
void DialogGraphics::show_direct_text(Address text){
    if(!text||!memory_.read(text,1))return;
    if(!glyph_renderer||!glyph_advance)throw Fault(text,"direct text needs font raster and advance metrics");
    if(const auto old=resolve_compact(memory_,memory_.read(0x76892c))){memory_.write(*old+compact_offset::lifecycle,0xffffffffu);memory_.write(0x76892c,0);}
    const auto handle=Arena(memory_).allocate_after(memory_.read(0x6d5a38),0x411f0f,0,0),object=*resolve_compact(memory_,handle);
    unsigned bytes=0;while(memory_.read(text+bytes,1)){if(++bytes>1024)throw Fault(text,"direct text exceeds surface capacity");}
    const int width=int(bytes*8);const auto surface=surfaces_.create(width,16);memory_.write(object+0x188,surface);
    memory_.write(object+0x18c,0);memory_.write(object+0x190,0);memory_.write(object+0x194,width);memory_.write(object+0x198,16);
    // 411de0's asymmetric six DrawTextA passes. Advance/bitmap metrics are
    // provided by the portable font adapter replacing GDI.
    for(int y=1;y>=0;--y)for(int x=2;x>=0;--x){
        int pen=x;const std::uint8_t color=x||y?0xd7:0xd4;
        for(unsigned offset=0;offset<bytes;){
            const auto first=memory_.read(text+offset++,1);unsigned cp=first;
            if(first&128){if(offset>=bytes)throw Fault(text,"truncated CP949 direct text");cp=(first<<8)|memory_.read(text+offset++,1);}
            glyph_renderer(surfaces_.get(surface),{std::uint16_t(cp),9,{pen,y,width+x,16+y},0x10,0,color,color,0,true});
            pen+=glyph_advance(9,std::uint16_t(cp));
        }
    }
    memory_.write(0x76892c,handle);
}
void DialogGraphics::clear_direct_text(){
    if(const auto object=resolve_compact(memory_,memory_.read(0x76892c))){
        if(memory_.read(*object+compact_offset::lifecycle)==3){memory_.write(*object+compact_offset::lifecycle,4);memory_.write(*object+0x2c,0);}
        else{memory_.write(*object+compact_offset::lifecycle,0xffffffffu);memory_.write(0x76892c,0);}
    }
}
void DialogGraphics::tick_direct_text(Address object,unsigned jobs){
    memory_.write(object+0x2c,memory_.read(object+0x2c)+jobs);
    auto state=memory_.read(object+compact_offset::lifecycle);const auto frame=memory_.read(object+0x2c),surface=memory_.read(object+0x188);
    const auto release=[&](){if(surface)surfaces_.release(surface);memory_.write(object+0x188,0);memory_.write(object+compact_offset::lifecycle,0);};
    if(state==0xffffffffu){release();return;}
    if(!state){state=2;memory_.write(object+compact_offset::lifecycle,state);}
    int visible=16;bool cropped=false;
    if(state==2){
        if(signed32(frame)>=30)memory_.write(object+compact_offset::lifecycle,3);
        else{visible=signed32(frame<<4)/30;cropped=true;}
    }else if(state==4){
        if(signed32(frame)>=30){release();return;}
        visible=16-signed32(frame<<4)/30;cropped=true;
    }else if(state!=3)throw Fault(object,"invalid direct text lifecycle");
    Rect source{signed32(memory_.read(object+0x18c)),signed32(memory_.read(object+0x190)),signed32(memory_.read(object+0x194)),signed32(memory_.read(object+0x198))};
    if(cropped)source.bottom=std::max(0,visible-1);
    surfaces_.blit(memory_.read(globals::render_target_surface),signed32(memory_.read(globals::viewport_far_x))-source.right,signed32(memory_.read(globals::viewport_far_y))-visible,surface,source,true,clip(memory_.read(globals::render_target_surface)));
}
} // namespace fsb::core
