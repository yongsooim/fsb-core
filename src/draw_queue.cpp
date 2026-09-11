#include "fsb_core/draw_queue.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/actors.hpp"
#include <algorithm>

namespace fsb::core {
Address DrawQueue::record(unsigned pass, unsigned index) {
    //428 is the original pass stride.454cb5/4577d6 also address records past
    //that stride in the shared pool (dense battle effects). Keep their address
    //calculation while rejecting writes beyond the complete four-pass pool.
    if (pass>=capacity::draw_passes || std::uint64_t(pass)*layout::draw_pass_stride+index>=capacity::draw_records)
        throw Fault(globals::draw_records,"draw record outside the original shared pool");
    return globals::draw_records+(pass*layout::draw_pass_stride+index)*layout::draw_record_size;
}
void DrawQueue::reset() {
    memory_.write(globals::draw_queue_count,0); for (unsigned i=0;i<capacity::draw_passes;++i) memory_.write(globals::draw_pass_counts+i*4,0);
}
void DrawQueue::enqueue(unsigned pass, unsigned index) {
    const auto count=memory_.read(globals::draw_queue_count);
    if (count>=capacity::draw_queue) throw Fault(globals::draw_queue_count,"global draw queue exceeds800 entries");
    memory_.write(globals::draw_queue+count*4,record(pass,index)); memory_.write(globals::draw_queue_count,count+1);
}
void DrawQueue::flush(unsigned pass) {
    record(pass,0);
    for (unsigned i=0;i<memory_.read(globals::draw_pass_counts+pass*4);++i) enqueue(pass,i);
}
void DrawQueue::sort(unsigned pass, unsigned first) {
    record(pass,0);
    const auto count=memory_.read(globals::draw_pass_counts+pass*4), end=first+count;
    if (first>capacity::draw_queue || count>capacity::draw_queue-first || end>memory_.read(globals::draw_queue_count)) throw Fault(first,"invalid appended draw sort range");
    for (unsigned i=first;i+1<end;++i) {
        auto least=i;
        for (unsigned j=i+1;j<end;++j)
            if (signed32(memory_.read(memory_.read(globals::draw_queue+j*4)+12))<signed32(memory_.read(memory_.read(globals::draw_queue+least*4)+12))) least=j;
        if (least!=i) {
            const auto a=memory_.read(globals::draw_queue+i*4), b=memory_.read(globals::draw_queue+least*4);
            memory_.write(globals::draw_queue+i*4,b); memory_.write(globals::draw_queue+least*4,a);
        }
    }
}
Address DrawQueue::actor(unsigned index) {
    const auto object=Actors::slot(index), state=memory_.read(object+actor_offset::layer_q16), pass=sequence_alu(alu::arithmetic_shift_right,state,15);
    const auto count=memory_.read(globals::draw_pass_counts+pass*4), command=record(pass,count);
    memory_.write(command,index>0x59?0xffffffffu:2u);
    const auto selector=memory_.read(object+actor_offset::sprite_selector), frame=memory_.read(object+actor_offset::sprite_frame), family=selector&0xffff;
    SpriteSurface sprite;
    switch (selector>>16) {
    case sprite_bank_kind::character:
        if (family>=335) throw Fault(selector,"character family outside original index");
        sprite=sprites_.character(memory_.read(globals::character_family_first_frame+family*4)+frame); break;
    case sprite_bank_kind::indexed: sprite=sprites_.indexed(family,frame); break;
    case sprite_bank_kind::active_effect: sprite=sprites_.indexed(memory_.read(object+actor_offset::sprite_base)+family,frame); break;
    case sprite_bank_kind::overlay: sprite=sprites_.overlay(frame);break;
    default: throw Fault(selector,"overlay actor sprite family is not connected");
    }
    memory_.write(command+draw_offset::surface,sprite.surface);
    const auto set_rect=[&](Address at,Rect rect) { memory_.write(at,rect.left);memory_.write(at+4,rect.top);memory_.write(at+8,rect.right);memory_.write(at+12,rect.bottom); };
    set_rect(command+draw_offset::source_left,sprite.rect);
    memory_.write(command+draw_offset::type,draw_kind::clipped_cached_surface); memory_.write(command+draw_offset::actor,object); memory_.write(command+draw_offset::layer_state,state);
    memory_.write(command+draw_offset::sort_key,std::uint32_t(signed32(memory_.read(object+actor_offset::world_y))/units::q16_one));
    const auto world_x=std::uint32_t(signed32(memory_.read(object+actor_offset::world_x)+memory_.read(object+actor_offset::draw_offset_x))/units::q16_one);
    const auto world_y=std::uint32_t(signed32(memory_.read(object+actor_offset::world_y)+memory_.read(object+actor_offset::draw_offset_y)-memory_.read(object+actor_offset::elevation))/units::q16_one);
    const auto x=world_x+std::uint32_t(signed32(memory_.read(globals::viewport_width))/2)-std::uint32_t(sprite.origin_x)-memory_.read(globals::camera_x)+memory_.read(globals::viewport_left);
    const auto y=world_y+std::uint32_t(signed32(memory_.read(globals::viewport_height))/2)-std::uint32_t(sprite.origin_y)-memory_.read(globals::camera_y)+memory_.read(globals::viewport_top);
    memory_.write(command+draw_offset::screen_x,x); memory_.write(command+draw_offset::screen_y,y);
    memory_.write(object+actor_offset::screen_anchor_x,x+sprite.origin_x); memory_.write(object+actor_offset::screen_anchor_y,y+sprite.origin_y-38u);
    const auto left=world_x-std::uint32_t(sprite.origin_x), top=world_y-std::uint32_t(sprite.origin_y);
    set_rect(command+draw_offset::world_left,{signed32(left),signed32(top),signed32(left+sprite.rect.right-sprite.rect.left),signed32(top+sprite.rect.bottom-sprite.rect.top)});
    memory_.write(command+draw_offset::blit_flags,1); memory_.write(globals::draw_pass_counts+pass*4,count+1); return command;
}
unsigned DrawQueue::tile(unsigned owner, Address object, std::uint32_t key, std::uint32_t state,
                         std::uint32_t x, std::uint32_t y, Address sheet, std::uint32_t frame, std::uint32_t flags) {
    const auto pass=sequence_alu(alu::arithmetic_shift_right,state,15);
    record(pass,0); const auto count=memory_.read(globals::draw_pass_counts+pass*4);
    if (!sheet) throw Fault(globals::draw_records,"tile command has no sheet definition");
    //4549ef returns the current index without writing or advancing a full
    //pass. Callers still receive that index, as in the original shadow pass.
    if(signed32(count)>=int(capacity::generic_draw_records_per_pass))return count;
    const auto command=record(pass,count);
    memory_.write(command,owner); memory_.write(command+draw_offset::type,draw_kind::clipped_sheet); memory_.write(command+draw_offset::actor,object);
    memory_.write(command+draw_offset::sort_key,key); memory_.write(command+draw_offset::screen_x,x); memory_.write(command+draw_offset::screen_y,y);
    memory_.write(command+draw_offset::sheet,sheet); if (frame!=0xffffffffu) memory_.write(command+draw_offset::frame,frame);
    memory_.write(command+draw_offset::blit_flags,flags); memory_.write(command+0x48,0);
    memory_.write(globals::draw_pass_counts+pass*4,count+1); return count;
}
void DrawQueue::background(const std::vector<TileCommand>& commands, Address sheet) {
    for (const auto& c:commands){
        const auto index=tile(0,0,c.sort_key,c.layer<<16,c.x,c.y,sheet,c.tile,c.flags);
        if(c.checker_fill&&index<capacity::generic_draw_records_per_pass){const auto command=record(c.pass,index);memory_.write(command+draw_offset::type,draw_kind::checker_tile);memory_.write(command+draw_offset::checker_fill,*c.checker_fill);}
        enqueue(c.pass,index);
    }
}
void DrawQueue::shadows(unsigned pass, Address definition) {
    record(pass,0); const auto count=memory_.read(globals::draw_pass_counts+pass*4);
    sprites_.load_sheet(definition);
    const auto size=sprites_.sheet(definition,0).rect;
    const auto width=size.right-size.left, height=size.bottom-size.top;
    for (unsigned i=0;i<count;++i) {
        const auto source=record(pass,i), object=memory_.read(source+0x10), flags=memory_.read(object+actor_offset::flags);
        if (!(flags&8)) continue;
        auto frame=signed32((flags&7)-std::uint32_t(signed32(memory_.read(object+actor_offset::elevation))/0x600000)); if(frame<0)frame=0;
        const auto world_x=std::uint32_t(signed32(memory_.read(object+actor_offset::world_x))/units::q16_one), world_y=std::uint32_t(signed32(memory_.read(object+actor_offset::world_y))/units::q16_one);
        const auto left=world_x-std::uint32_t(width/2), top=world_y-std::uint32_t(height/2);
        const auto x=std::uint32_t(signed32(memory_.read(globals::viewport_width))/2)+left-memory_.read(globals::camera_x)+memory_.read(globals::viewport_origin_x);
        const auto y=std::uint32_t(signed32(memory_.read(globals::viewport_height))/2)+top-memory_.read(globals::camera_y)+memory_.read(globals::viewport_origin_y);
        const auto index=tile(1,object,0,memory_.read(source+8),x,y,definition,std::uint32_t(frame),1);
        enqueue(pass,index); const auto command=record(pass,index);
        memory_.write(command,1); memory_.write(command+draw_offset::world_left,left); memory_.write(command+draw_offset::world_top,top);
        memory_.write(command+draw_offset::world_right,left+width); memory_.write(command+draw_offset::world_bottom,top+width); // Original uses width here, even for bottom.
    }
}
void DrawQueue::execute() {
    const auto destination=memory_.read(globals::render_target_surface), count=memory_.read(globals::draw_queue_count);
    if(count>capacity::draw_queue)throw Fault(count,"invalid global draw count");
    const Rect clip{signed32(memory_.read(globals::clip_left)),signed32(memory_.read(globals::clip_top)),signed32(memory_.read(globals::clip_right)),signed32(memory_.read(globals::clip_bottom))};
    for(unsigned i=0;i<count;++i){
        const auto command=memory_.read(globals::draw_queue+i*4), flags=memory_.read(command+draw_offset::blit_flags); if(flags==0xffffffffu)continue;
        const auto type=memory_.read(command+draw_offset::type); SpriteSurface sprite;
        //45473c subtracts these two dwords, then TEST/JLE before reading the
        //source. Saturated shadow requests can enqueue a still-zero type0
        //record; it is a valid no-op even with no source sheet attached.
        if(type==draw_kind::surface_if_dirty&&signed32(memory_.read(command+draw_offset::source_top)-memory_.read(command+draw_offset::source_bottom))<=0)continue;
        if(type==draw_kind::checker_tile){
            sprite=sprites_.sheet(memory_.read(command+draw_offset::sheet),memory_.read(command+draw_offset::frame));
            const auto fill=std::uint8_t(memory_.read(command+draw_offset::checker_fill,1));
            if(!flags||!fill)draw_checker_tile(surfaces_.get(destination),surfaces_.get(sprite.surface),sprite.rect,signed32(memory_.read(command+draw_offset::screen_x)),signed32(memory_.read(command+draw_offset::screen_y)),flags!=0,fill,clip);
            continue;
        }
        const auto rectangle=[&](Address at){return Rect{signed32(memory_.read(at)),signed32(memory_.read(at+4)),signed32(memory_.read(at+8)),signed32(memory_.read(at+12))};};
        bool clipped=false,stretched=false;
        switch(type){
        case draw_kind::clipped_surface:clipped=true;[[fallthrough]];
        case draw_kind::surface_if_dirty:
            sprite={memory_.read(memory_.read(command+draw_offset::sheet)+0x40),rectangle(command+draw_offset::source_left),0,0};break;
        case draw_kind::clipped_sheet:clipped=true;[[fallthrough]];
        case draw_kind::sheet:
            sprite=sprites_.sheet(memory_.read(command+draw_offset::sheet),memory_.read(command+draw_offset::frame));break;
        case draw_kind::clipped_stretch_sheet:clipped=true;[[fallthrough]];
        case draw_kind::stretch_sheet:
            stretched=true;sprite={memory_.read(memory_.read(command+draw_offset::sheet)+0x40),rectangle(command+draw_offset::source_left),0,0};break;
        case draw_kind::clipped_stretch_frame:clipped=true;[[fallthrough]];
        case draw_kind::stretch_frame:
            stretched=true;sprite=sprites_.sheet(memory_.read(command+draw_offset::sheet),memory_.read(command+draw_offset::frame));break;
        case draw_kind::clipped_cached_surface:clipped=true;[[fallthrough]];
        case draw_kind::cached_surface:
            sprite={memory_.read(command+draw_offset::surface),rectangle(command+draw_offset::source_left),0,0};break;
        case draw_kind::clipped_stretch_cached_surface:clipped=true;[[fallthrough]];
        case draw_kind::stretch_cached_surface:
            stretched=true;sprite={memory_.read(command+draw_offset::surface),rectangle(command+draw_offset::source_left),0,0};break;
        default:continue; //4546f4's out-of-table command types advance without drawing.
        }
        if(stretched){
            if(flags&~(blit_flags::rectangle_wait|blit_flags::rectangle_source_key))throw Fault(command,"unsupported original rectangle blit effect");
            surfaces_.stretch(destination,rectangle(command+draw_offset::world_left),sprite.surface,sprite.rect,(flags&blit_flags::rectangle_source_key)!=0,clipped?std::optional<Rect>(clip):std::nullopt);
        }else{
            if(flags&~(blit_flags::fast_wait|blit_flags::fast_source_key))throw Fault(command,"unsupported original fast blit effect");
            surfaces_.blit(destination,signed32(memory_.read(command+draw_offset::screen_x)),signed32(memory_.read(command+draw_offset::screen_y)),sprite.surface,sprite.rect,(flags&blit_flags::fast_source_key)!=0,clipped?std::optional<Rect>(clip):std::nullopt);
        }
    }
}
void DrawQueue::foreground(unsigned layer,bool battle) {
    const auto pass=layer*2+1, tile_plane=layer==memory_.read(globals::background_layer_count)?layer-1:layer;
    record(pass,0);
    // Clearing all but the low byte of 4096 cells through read()/write() costs
    // 8192 address lookups for what is three byte stores per cell. The guest is
    // little-endian, so &0xff is exactly "keep byte 0, zero the other three".
    {
        const auto cells=memory_.span(globals::foreground_occlusion,globals::foreground_occlusion_end-globals::foreground_occlusion);
        for(std::size_t at=0;at<cells.size();at+=4){cells[at+1]=0;cells[at+2]=0;cells[at+3]=0;}
    }
    const auto stride=signed32(memory_.read(globals::grid_row_stride)), grid_height=signed32(memory_.read(globals::grid_height));
    if(stride<1||grid_height<1||std::int64_t(stride)*grid_height>4096)throw Fault(layer,"invalid foreground grid");
    const auto count=memory_.read(globals::draw_pass_counts+pass*4);
    for(unsigned i=0;i<count;++i){
        const auto command=record(pass,i), object=memory_.read(command+draw_offset::actor);
        int x0=signed32(memory_.read(command+draw_offset::world_left))/64, y0=signed32(memory_.read(command+draw_offset::world_top))/48;
        int x1=signed32(memory_.read(command+draw_offset::world_right)-1)/64, y1=signed32(memory_.read(command+draw_offset::world_bottom)-1)/48;
        const auto depth=signed32(memory_.read(object+actor_offset::world_y))/units::q16_one;
        x0=std::max(x0,0);y0=std::max(y0,0);x1=std::min(x1,stride-1);y1=std::min(y1,grid_height-1);
        for(int y=y1;y>=y0;--y)for(int x=x0;x<=x1;++x){
            const auto cell=std::uint32_t(y*stride+x), flags=memory_.read(globals::foreground_occlusion+cell*4);
            if(!(flags&0x100)){
                const auto attribute=memory_.read(globals::tile_attributes+(layer*4096+cell)*4);
                if(depth<=int(((attribute>>2)&7)+1+std::uint32_t(y))*48)memory_.write(globals::foreground_occlusion+cell*4,flags|0x100);
            }
        }
    }
    const auto width=signed32(memory_.read(globals::viewport_width)), height=signed32(memory_.read(globals::viewport_height));
    const auto camera_x=signed32(memory_.read(globals::camera_x)), camera_y=signed32(memory_.read(globals::camera_y));
    const auto left=camera_x-width/2, top=camera_y-height/2;
    const auto x0=left/64-1, x1=(width+left)/64+1, y0=top/48-1, y1=(height+top)/48+1;
    if(width<1||height<1||width>4096||height>4096)throw Fault(layer,"invalid foreground viewport");
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        // Original scan deliberately has no separate edge clamp. Camera bounds
        // provide its margin. Guest memory still validates every derived address.
        const auto cell=std::uint32_t(y)*std::uint32_t(stride)+std::uint32_t(x);
        if(!(memory_.read(globals::foreground_occlusion+cell*4)&0x100))continue;
        const auto attribute=memory_.read(globals::tile_attributes+(layer*4096+cell)*4);if(!(attribute&1))continue;
        const auto command=record(pass,memory_.read(globals::draw_pass_counts+pass*4));
        memory_.write(command,3);memory_.write(command+0x4c,0);memory_.write(command+draw_offset::type,battle?4:3);
        memory_.write(command+draw_offset::sort_key,(((attribute>>2)&7)+1+std::uint32_t(y))*48);
        memory_.write(command+draw_offset::screen_x,std::uint32_t(width/2)-std::uint32_t(camera_x)+memory_.read(globals::viewport_origin_x)+std::uint32_t(x)*64);
        memory_.write(command+draw_offset::screen_y,std::uint32_t(height/2)-std::uint32_t(camera_y)+memory_.read(globals::viewport_origin_y)+std::uint32_t(y)*48);
        const auto tile=memory_.read(globals::map_tile_codes+cell*4+tile_plane*0x8028);
        if(tile>=4096)throw Fault(tile,"foreground tile outside material tables");
        memory_.write(command+draw_offset::frame,tile);
        const auto secondary=memory_.read(0x7dcca0+tile*4);
        const bool use_secondary=(attribute&3)==3&&secondary!=0xffffffffu;
        memory_.write(command+draw_offset::sheet,use_secondary?0x804d54:0x804d10);
        memory_.write(command+draw_offset::blit_flags,use_secondary?secondary:memory_.read(0x7d8ca0+tile*4));
        memory_.write(globals::draw_pass_counts+pass*4,memory_.read(globals::draw_pass_counts+pass*4)+1);
    }
}
} // namespace fsb::core
