#include "fsb_core/sprites.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
SpriteSurface Sprites::overlay(std::uint32_t frame) const {
    const auto row=tables::overlay_sprite_frames+frame*layout::overlay_sprite_frame_size;
    const auto source=memory_.read(globals::overlay_sprite_surfaces+memory_.read(row)*layout::sprite_sheet_size);
    const int x=signed32(memory_.read(row+4)),y=signed32(memory_.read(row+8));
    const int width=signed32(memory_.read(row+12)),height=signed32(memory_.read(row+16));
    surfaces_.get(source);
    return {source,{x,y,signed32(std::uint32_t(x)+std::uint32_t(width)),signed32(std::uint32_t(y)+std::uint32_t(height))},width/2,height/2};
}
namespace {
std::string upper(std::string name) { for (auto& c : name) if (c >= 'a' && c <= 'z') c -= 'a'-'A'; return name; }
int word(const Memory& memory, Address at) { const auto v = memory.read(at,2); return v < 0x8000 ? int(v) : int(v)-65536; }
}
void Sprites::register_pcx(std::string resource, std::vector<std::uint8_t> bytes) {
    register_image(std::move(resource),std::move(bytes));
}
void Sprites::register_image(std::string resource,std::vector<std::uint8_t> bytes){
    assets_[upper(std::move(resource))] = std::move(bytes); // Keep original !/@/ prefixes: they select different resource libraries.
}
std::string Sprites::name(Address record) const {
    std::string result;
    for (unsigned i=0; i<32; ++i) { const auto c=memory_.read(record+i,1); if (!c) return result; result.push_back(char(c)); }
    throw Fault(record,"sprite resource name has no terminator");
}
Image8 Sprites::decode(Address record,unsigned format) const {
    const auto resource=upper(name(record)); const auto it=assets_.find(resource);
    if (it==assets_.end()) throw Fault(record,"sprite asset not registered: "+resource);
    if(format>image_format::pcx)throw Fault(record,"invalid original image format");
    if(format==image_format::automatic)format=resource.ends_with(".BMP")?image_format::bitmap:image_format::pcx;
    return format==image_format::bitmap?Image8::bmp(it->second):Image8::pcx(it->second);
}
void Sprites::refresh_overlay() {
    // 4324a6(-1): title LOAD enables the original40-frame loading sprite.
    if(!memory_.read(0x769414))return;
    const auto frame=(memory_.read(0x769434)+1)%40;memory_.write(0x769434,frame);
    const auto primary=memory_.read(globals::primary_surface);if(!primary)return;
    const auto record=0x514b84+(memory_.read(0x766818)+frame)*64;
    const auto width=signed32(memory_.read(record)),height=signed32(memory_.read(record+4));
    const auto x=signed32(memory_.read(0x74b4ac)-memory_.read(record+8)),y=signed32(memory_.read(0x74b6c0)-memory_.read(record+12));
    surfaces_.blit(primary,x,y,memory_.read(record+16),{0,0,width,height},true);
}
void Sprites::deferred(std::uint32_t value) {
    if (memory_.read(globals::current_event_id)==0xffffffffu) return;
    const auto count=memory_.read(globals::deferred_sprite_count);
    if (count>=1024) throw Fault(globals::deferred_sprite_entries,"sprite deferred-release queue exhausted");
    memory_.write(globals::deferred_sprite_entries+count*4,value); memory_.write(globals::deferred_sprite_count,count+1);
}
bool Sprites::load_sheet(Address definition) {
    if (memory_.read(definition+0x40)) return false;
    const auto columns=word(memory_,definition+0x20), rows=word(memory_,definition+0x22);
    if (columns<=0 || rows<=0) throw Fault(definition,"sprite sheet has invalid grid");
    auto image=decode(definition); const auto width=image.width, height=image.height;
    memory_.write(definition+0x34,width); memory_.write(definition+0x38,height);
    memory_.write(definition+0x30,std::uint32_t(columns)*std::uint32_t(rows),2);
    memory_.write(definition+0x40,surfaces_.insert(std::move(image)));
    memory_.write(definition+0x2c,width/unsigned(columns),2); memory_.write(definition+0x2e,height/unsigned(rows),2);
    return true;
}
void Sprites::cache_sheet(Address definition, Address resource, unsigned width, unsigned height, std::uint32_t tag) {
    if (!width || !height) throw Fault(definition,"sprite sheet has zero cell size");
    const auto text=name(resource); auto image=decode(resource);
    for (unsigned i=0;i<=text.size();++i) memory_.write(definition+i,i<text.size()?std::uint8_t(text[i]):0,1);
    memory_.write(definition+0x34,image.width); memory_.write(definition+0x38,image.height);
    memory_.write(definition+0x2c,width,2); memory_.write(definition+0x2e,height,2);
    memory_.write(definition+0x24,0,2); memory_.write(definition+0x26,0,2);
    const auto columns=image.width/width;
    memory_.write(definition+0x20,columns,2); memory_.write(definition+0x30,columns*(image.height/height),2);
    memory_.write(definition+0x3c,tag); memory_.write(definition+0x40,surfaces_.insert(std::move(image)));
}
void Sprites::replace_sheet(Address definition,const std::string& resource,const std::vector<std::uint8_t>& bytes,unsigned width,unsigned height){
    if(resource.size()>=32)throw Fault(definition,"sheet resource name exceeds original record");
    refresh_overlay();const auto old=memory_.read(definition+0x40);
    if(old){surfaces_.release(old);memory_.write(definition+0x40,0);}
    register_pcx(resource,bytes);
    const auto temporary=memory_.allocate_zeroed(32);
    for(unsigned i=0;i<resource.size();++i)memory_.write(temporary+i,std::uint8_t(resource[i]),1);
    cache_sheet(definition,temporary,width,height);memory_.release_allocation(temporary);
}
void Sprites::initialize_scene_sheets() {
    // 431907 stages SITEM's palette224..247 independently of active palette.
    const auto items=decode(0x5ab7fc);
    for(unsigned i=0;i<24;++i){const auto c=items.palette[224+i];memory_.write(0x7693b0+i*4,c.r|(std::uint32_t(c.g)<<8)|(std::uint32_t(c.b)<<16));}
    // Process-lifetime caches from0x45f682. UI objects created later by that
    // function are separate and must still be connected in the complete boot.
    const Address definitions[]={0x804d98,0x804ccc,0x804ddc};
    for(unsigned i=0;i<3;++i){
        const auto entry=0x5cd3f0+i*16;
        if(!memory_.read(entry)){
            cache_sheet(definitions[i],memory_.read(entry+12),memory_.read(entry+4),memory_.read(entry+8));
            memory_.write(entry,definitions[i]);
        }
    }
    const auto capture=[&](Address resource){
        const auto image=decode(resource);
        for(unsigned i=0;i<256;++i){const auto c=image.palette[i];memory_.write(globals::decoded_image_palette+i*4,c.r|(std::uint32_t(c.g)<<8)|(std::uint32_t(c.b)<<16));}
    };
    capture(memory_.read(0x5cd41c));
    palette_.copy(globals::palette_target+112*4,globals::decoded_image_palette+112*4,136); palette_.copy(globals::overlay_palette_cache+112*4,globals::decoded_image_palette+112*4,112);
    capture(memory_.read(0x5cd40c));
    palette_.copy(globals::palette_target,globals::decoded_image_palette,16); palette_.copy(globals::overlay_palette_cache,globals::decoded_image_palette,16);
    const auto temp=memory_.allocate_zeroed(4); memory_.write(temp,0xffffff); palette_.upload(temp,255,1); memory_.release_allocation(temp);
    memory_.write(0x6e0e08,3);
}
void Sprites::upload_inline_item_palette(){palette_.upload(0x7693b0,224,24);}
Address Sprites::decode_surface(Address resource,Address palette,unsigned format){
    auto image=decode(resource,format);if(palette)for(unsigned i=0;i<256;++i){const auto c=image.palette[i];memory_.write(palette+i*4,c.r|(std::uint32_t(c.g)<<8)|(std::uint32_t(c.b)<<16));}return surfaces_.insert(std::move(image));
}
void Sprites::decode_palette(Address resource,Address destination,unsigned format){const auto image=decode(resource,format);for(unsigned i=0;i<256;++i){const auto c=image.palette[i];memory_.write(destination+i*4,c.r|(std::uint32_t(c.g)<<8)|(std::uint32_t(c.b)<<16));}}
SpriteSurface Sprites::sheet(Address definition, std::uint32_t frame) {
    load_sheet(definition);
    const auto count=std::uint32_t(word(memory_,definition+0x30)); if (frame>=count) frame=0;
    const auto columns=std::uint32_t(word(memory_,definition+0x20));
    if (!columns) throw Fault(definition,"sprite grid divides by zero");
    const auto width=word(memory_,definition+0x2c), height=word(memory_,definition+0x2e);
    const auto x=std::uint32_t(width)*(frame%columns), y=std::uint32_t(height)*(frame/columns);
    return {memory_.read(definition+0x40),{signed32(x),signed32(y),signed32(x+width),signed32(y+height)},word(memory_,definition+0x24),word(memory_,definition+0x26)};
}
SpriteSurface Sprites::indexed(unsigned index, std::uint32_t frame) {
    prepare_indexed(index);return sheet(tables::indexed_sheets+index*68,frame);
}
bool Sprites::prepare_indexed(unsigned index){
    if (index>=1224) throw Fault(index,"indexed sprite bank outside original table");
    const auto definition=tables::indexed_sheets+index*68;
    if (load_sheet(definition)) { refresh_overlay(); deferred(index);return false; }return true;
}
bool Sprites::load_character(unsigned index, bool output) {
    if (index>=335) throw Fault(index,"character sheet bank outside original table");
    const auto record=tables::character_sheets+index*0x42c;
    if (memory_.read(record+0x428)) return false;
    auto image=decode(record);
    memory_.write(record+0x20,image.width); memory_.write(record+0x24,image.height);
    if (output || index>=248) {
        for (unsigned i=0; i<256; ++i) { const auto c=image.palette[i]; memory_.write(record+0x28+i*4,c.r|(std::uint32_t(c.g)<<8)|(std::uint32_t(c.b)<<16)); }
        const auto destination=0x757e78+(index>=248?index-247:0)*96;
        for (unsigned i=0; i<24; ++i) memory_.write(destination+i*4,memory_.read(record+0x3a8+i*4));
    }
    memory_.write(record+0x428,surfaces_.insert(std::move(image))); return true;
}
void Sprites::initialize() {
    load_sheet(0x57eb30);
    for (Address record=tables::indexed_sheets; record<tables::character_frames; record+=68)
        if (!memory_.read(record+0x40) && !memory_.read(record,1))
            for (unsigned i=0; i<68; i+=4) memory_.write(record+i,memory_.read(0x57eb30+i));
    load_character(0,true);
    for (unsigned family=0; family<335; ++family) { memory_.write(globals::character_family_frame_counts+family*4,0); memory_.write(globals::character_family_first_frame+family*4,0); }
    for (unsigned frame=0; frame<6610; ++frame) {
        const auto family=memory_.read(tables::character_frame_family+frame*64);
        if (family>=335) throw Fault(frame,"character frame has invalid family id");
        memory_.write(globals::character_family_frame_counts+family*4,memory_.read(globals::character_family_frame_counts+family*4)+1);
        for (unsigned later=family+1; later<335; ++later) memory_.write(globals::character_family_first_frame+later*4,memory_.read(globals::character_family_first_frame+later*4)+1);
    }
    // 431907 preloads these two families before any event is active. In
    // particular the sweat/reaction children can outlive an event's deferred
    // cache flush, so their surfaces must belong to the bootstrap cache.
    const auto primary=memory_.read(globals::primary_surface);
    memory_.write(globals::primary_surface,0);
    character(memory_.read(0x766818));
    memory_.write(globals::primary_surface,primary);
    character(memory_.read(globals::reaction_family_first_frame));
}
void Sprites::ensure_palette(unsigned sheet_id) {
    const auto frame=memory_.read(globals::presented_frame_counter);
    const auto upload=[&](unsigned id) { palette_.upload(0x757e78+(id-247)*96,224,24); };
    if (memory_.read(0x768370)!=frame) {
        if (!memory_.read(globals::extended_palette_dirty) && (memory_.read(globals::extended_palette_loaded)==1 || memory_.read(globals::extended_palette_family)==0xffffffffu)) {
            memory_.write(globals::extended_palette_family,0); upload(247); memory_.write(globals::extended_palette_loaded,0);
        }
        memory_.write(globals::extended_palette_dirty,0); memory_.write(0x768370,frame);
    }
    if (sheet_id<248) return;
    const auto state=memory_.read(globals::extended_palette_loaded);
    if (!state) { upload(sheet_id); memory_.write(globals::extended_palette_loaded,1); }
    else {
        if (state!=1 || memory_.read(globals::extended_palette_family)==sheet_id || memory_.read(globals::extended_palette_frame)==frame) { memory_.write(globals::extended_palette_dirty,1); return; }
        upload(sheet_id);
    }
    memory_.write(globals::extended_palette_family,sheet_id); memory_.write(globals::extended_palette_frame,frame); memory_.write(globals::extended_palette_dirty,1);
}
void Sprites::cache_frame(unsigned index, unsigned sheet_id) {
    const auto record=tables::character_frames+index*64;
    if (memory_.read(record+60)) return;
    deferred(index|0x10000);
    const auto x=signed32(memory_.read(record+36)), y=signed32(memory_.read(record+40));
    const auto width=memory_.read(record+44), height=memory_.read(record+48);
    const auto surface=surfaces_.create(width,height); memory_.write(record+60,surface);
    surfaces_.blit(surface,0,0,memory_.read(tables::character_sheet_surface+sheet_id*0x42c),{x,y,signed32(std::uint32_t(x)+width),signed32(std::uint32_t(y)+height)});
    refresh_overlay();
}
SpriteSurface Sprites::character(unsigned frame) {
    if (frame>=6610) throw Fault(frame,"character frame outside original table");
    const auto record=tables::character_frames+frame*64, sheet_id=memory_.read(record+32);
    if (!memory_.read(record+60)) {
        load_character(sheet_id); ensure_palette(sheet_id);
        for (int i=int(frame); i>=0 && memory_.read(tables::character_frame_family+unsigned(i)*64)==sheet_id; --i) cache_frame(unsigned(i),sheet_id);
        for (unsigned i=frame+1; i<6610 && memory_.read(tables::character_frame_family+i*64)==sheet_id; ++i) cache_frame(i,sheet_id);
        //0x40bdf9's actual JE clears only an already-null sheet; resident sheets are retained.
    } else ensure_palette(sheet_id);
    return {memory_.read(record+60),{0,0,signed32(memory_.read(record+44)),signed32(memory_.read(record+48))},signed32(memory_.read(record+52)),signed32(memory_.read(record+56))};
}
std::uint32_t Sprites::flush_deferred() {
    const auto count=memory_.read(globals::deferred_sprite_count); unsigned sheets=0, frames=0;
    if (count>1024) throw Fault(globals::deferred_sprite_count,"invalid sprite release count");
    for (unsigned i=0; i<count; ++i) {
        const auto value=memory_.read(globals::deferred_sprite_entries+i*4), index=value&0xffff, type=value>>16;
        if (type>1 || index>=(type?6610u:1224u)) throw Fault(value,"invalid sprite release entry");
        const auto slot=type?tables::character_frame_surface+index*64:tables::indexed_sheet_surface+index*68, surface=memory_.read(slot);
        if (surface) { surfaces_.release(surface); memory_.write(slot,0); if (type) ++frames; else ++sheets; }
    }
    memory_.write(globals::deferred_sprite_count,0); return (frames<<16)|sheets;
}
} // namespace fsb::core
