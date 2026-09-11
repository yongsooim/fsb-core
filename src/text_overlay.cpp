#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
void DialogGraphics::queue_text_overlay(int x,int y,std::uint32_t color,std::uint32_t background,const std::string& text,unsigned layer){
    const auto count=memory_.read(globals::text_overlay_count);
    if(count>=511||layer>1||text.size()>=1024)throw Fault(0x406576,"text overlay exceeds original queue/buffer capacity");
    const auto row=tables::text_overlays+count*36,buffer=memory_.allocate_zeroed(unsigned(text.size())+1);
    for(unsigned i=0;i<text.size();++i)memory_.write(buffer+i,std::uint8_t(text[i]),1);
    memory_.write(row,unsigned(x));memory_.write(row+4,unsigned(y));memory_.write(row+16,color);memory_.write(row+20,background);
    memory_.write(row+24,buffer);memory_.write(row+28,unsigned(text.size()));memory_.write(row+32,layer);
    memory_.write(globals::text_overlay_count,count+1);
}
void DialogGraphics::text_overlays(const Palette& palette){
    const auto count=memory_.read(globals::text_overlay_count);if(!count)return;
    if(count>512)throw Fault(0x407e14,"invalid original text overlay count");
    const auto target=memory_.read(globals::render_target_surface);
    const Rect viewport{signed32(memory_.read(globals::viewport_left)),signed32(memory_.read(globals::viewport_top)),signed32(memory_.read(globals::viewport_right)),signed32(memory_.read(globals::viewport_bottom))};
    if(target)for(unsigned layer=0;layer<2;++layer)for(unsigned i=0;i<count;++i){
        const auto row=tables::text_overlays+i*36;
        if(memory_.read(row+32)!=layer)continue;
        const auto pointer=memory_.read(row+24),length=memory_.read(row+28),background=memory_.read(row+20);
        std::string text;for(unsigned j=0;j<length;++j)text.push_back(char(memory_.read(pointer+j,1)));
        plain_text(target,{signed32(memory_.read(row)),signed32(memory_.read(row+4)),signed32(memory_.read(row+8)),signed32(memory_.read(row+12))},0,palette.index(memory_.read(row+16)),text,0x100,layer?std::optional<Rect>(viewport):std::nullopt,signed32(background)<0?std::nullopt:std::optional<std::uint8_t>(palette.index(background)));
    }
    for(unsigned i=0;i<count;++i)memory_.release_allocation(memory_.read(tables::text_overlays+i*36+24));
    memory_.write(globals::text_overlay_count,0);
}
}
