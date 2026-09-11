#include "fsb_core/dialogue.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/actor_fields.hpp"
#include <algorithm>

namespace fsb::core {
void Dialogue::configure_timing(int free_text, int event_text, int delay) {
    const int values[] = {free_text,event_text,delay};
    const Address targets[] = {globals::free_text_wait_scale,globals::event_text_wait_scale,globals::event_dialogue_auto_delay};
    for (unsigned i=0;i<3;++i) { const auto value=unsigned(std::clamp(values[i],0,100)); memory_.write(globals::dialogue_timing_percentages+i*4,value); memory_.write(targets[i],value/2); }
    // Do not synthesize -1 at0x6db57c to enable E2 auto-delay. No original
    // initializer proving that write has been recovered; keep the real state.
}
unsigned Dialogue::style_flags(std::uint32_t packed) {
    if (packed & 0x80000000u) return (packed>>20)&15;
    return ((packed&0xf00)==0x100 ? 8u : 0u) | ((packed>>8)&15);
}
unsigned Dialogue::style_variant(std::uint32_t packed) {
    if (packed&0x80000000u) return (packed>>24)&127;
    if (packed&32) return 30; if (packed&16) return 31;
    const auto family=(packed>>12)&15;
    if (family>3) throw Fault(0x411d4b,"unknown template dialog style family");
    return 32+family*2+((packed>>7)&1);
}
void Dialogue::apply_style(Address d) {
    const auto style=memory_.read(d+dialog_offset::box_style); auto flags=memory_.read(d+dialog_offset::flags_b);
    if (style==0) flags&=~3u;
    else if (style==1||style==2) flags|=3;
    else if (style==3||style==4) flags=(flags&~2u)|1;
    else throw Fault(d,"invalid dialog box style");
    memory_.write(d+dialog_offset::flags_b,flags);
    refresh_gaps(d);
}
void Dialogue::refresh_gaps(Address d) {
    const auto flags=memory_.read(d+dialog_offset::flags_b);
    const auto base=(flags&1) ? 0u : 0xfffffff0u, variant=flags>>1;
    memory_.write(d+0xf4,((flags&2) ? 10u : 0u)-4+base);
    memory_.write(d+0xf8,base+(variant|0xfffffffeu)*2); memory_.write(d+0xf0,base+(variant|0xfffffffeu)*2);
    memory_.write(d+0xdc,(variant<<4|0xffffffe4u)+base);
    memory_.write(d+0x110,base+(variant&1)*8); memory_.write(d+0x108,base+(variant&1)*8);
    memory_.write(d+0xe0,base+((variant&1)<<3|0xffffffe4u)); memory_.write(d+0xd8,base+((variant&1)<<3|0xffffffe4u));
}
void Dialogue::initialize_chunk(Address d, Address text) {
    const auto get=[&](unsigned word){return memory_.read(d+word*4);};
    const auto put=[&](unsigned word,std::uint32_t value){memory_.write(d+word*4,value);};
    const auto profile=get(dialog_word::width_profile); if (profile>=6) throw Fault(d,"invalid dialog width profile");
    put(dialog_word::text,text); put(dialog_word::trimmed_columns,0);
    const auto measured=measure_dialog(memory_,text,signed32(memory_.read(tables::dialogue_width_profiles+profile*4)*2-4),signed32(get(dialog_word::first_indent)),signed32(get(dialog_word::continuation_indent)),signed32(get(dialog_word::min_lines)),signed32(get(dialog_word::max_lines)),signed32(get(dialog_word::forced_lines)));
    put(dialog_word::column_capacity,std::uint32_t(measured.columns)); put(dialog_word::line_capacity,std::uint32_t(measured.lines)); put(dialog_word::trimmed_columns,std::uint32_t(measured.trim));
    const auto width=std::uint32_t((measured.columns/2+2)*16),height=std::uint32_t(measured.lines*22+32);
    put(dialog_word::width,width);put(dialog_word::height,height);put(dialog_word::source_left,0);put(dialog_word::source_top,0);put(dialog_word::source_right,width);put(dialog_word::source_bottom,height);
    put(dialog_word::cursor,0);put(dialog_word::line,0);put(dialog_word::explicit_line_start,1);put(dialog_word::column,get(dialog_word::first_indent)-get(dialog_word::trimmed_columns));
    const auto row=memory_.read(0x57edb0+get(dialog_word::box_style)*4)+get(dialog_word::style_variant)*2;
    put(dialog_word::foreground,memory_.read(0x57edc8+row*32,2)|0x1000000);put(dialog_word::outline,memory_.read(0x57edcc+row*32,2)|0x1000000);
    put(dialog_word::pause_count,0);put(dialog_word::glyph_time_ms,memory_.read(globals::frame_time_ms));
}
Handle Dialogue::create(std::uint32_t id, Address text, int facing) {
    Address actor=0,x=0,y=0;unsigned variant=0;
    if (id!=0xffffffffu) {
        actor=lookup_actor(memory_,id);
        if (!memory_.read(0x76894c)&&!actor) throw Fault(id,"actor-bound dialog needs a materialized actor");
        if (id<65536) variant=style_variant(memory_.read(tables::actor_dialogue_styles+id*68));
        if (!memory_.read(0x76894c)) {x=actor+actor_offset::screen_anchor_x;y=actor+actor_offset::screen_anchor_y;} else {x=0x6d66a8;y=0x6d66ac;}
    }
    const auto handle=create_at(x,y,text,variant),object=*resolve_compact(memory_,handle),d=memory_.read(object+compact_offset::state_pointer);
    if (id!=0xffffffffu) {
        memory_.write(object+vm_offset::actor_id,id);
        if (id<65536) memory_.write(d+dialog_offset::text_style,memory_.read(d+dialog_offset::text_style)|style_flags(memory_.read(tables::actor_dialogue_styles+id*68)));
        memory_.write(d+dialog_offset::flags_b,memory_.read(d+dialog_offset::flags_b)|8);memory_.write(d+dialog_offset::box_style,1);
        if (signed32(memory_.read(globals::current_event_id))>=0&&memory_.read(globals::event_dialogue_auto_delay)) memory_.write(d+dialog_offset::flags_b,memory_.read(d+dialog_offset::flags_b)|128);
        if (facing!=-1) {
            constexpr unsigned order[]={0x1ce,0x360,0x274,0x34a,0x1ce,0x360,0x274,0x34a};
            if (facing<0||facing>7) throw Fault(object,"invalid dialog facing selector");
            memory_.write(d+dialog_offset::position_order,order[facing]);
        }
    }
    return handle;
}
Handle Dialogue::create_at(Address x,Address y,Address text,unsigned variant){
    memory_.write(globals::dialogue_abort_requested,0);
    const auto handle=Arena(memory_).allocate_after(memory_.read(globals::group5_append_link),routines::dialogue_tick,0x410000,0),object=*resolve_compact(memory_,handle);
    const auto d=memory_.allocate_zeroed(0x1c8);memory_.write(object+compact_offset::state_pointer,d);memory_.write(globals::live_dialogue_count,memory_.read(globals::live_dialogue_count)+1);
    memory_.write(d+dialog_offset::position_order,0x354);memory_.write(d+dialog_offset::box_style,1);memory_.write(d+dialog_offset::text_style,0x20);
    memory_.write(d+dialog_offset::wait_scale,memory_.read(signed32(memory_.read(globals::current_event_id))>=0?globals::event_text_wait_scale:globals::free_text_wait_scale));
    memory_.write(d+dialog_offset::first_indent,2); apply_style(d);
    memory_.write(d,x);memory_.write(d+dialog_offset::anchor_y_pointer,y);memory_.write(d+dialog_offset::style_variant,std::min(variant,45u));memory_.write(d+dialog_offset::width_profile,2);initialize_chunk(d,text);
    return handle;
}
Handle Dialogue::spawn_markup(std::uint32_t id, Address text, std::uint32_t channel) {
    if (!text) return 0;
    const auto actor=id==0xffffffffu?0:lookup_actor(memory_,id);
    if (id!=0xffffffffu&&!actor) throw Fault(id,"markup dialogue actor is missing");
    const auto handle=create(id,text,actor?signed32(memory_.read(actor+actor_offset::facing)):-1),object=*resolve_compact(memory_,handle);
    if (actor) memory_.write(actor+actor_offset::dialogue_handle,handle);
    memory_.write(object+vm_offset::message_channel,channel);const auto d=memory_.read(object+compact_offset::state_pointer);memory_.write(d+0x159,memory_.read(d+0x159,1)|32,1);
    return handle;
}
Address Dialogue::state(Handle handle) const {
    const auto object=resolve_compact(memory_,handle);
    if (!object||memory_.read(*object+compact_offset::callback)!=routines::dialogue_tick) throw Fault(handle,"not a live dialogue controller");
    return memory_.read(*object+compact_offset::state_pointer);
}
void Dialogue::release_reference(Address slot) {
    if (const auto target=resolve_compact(memory_,memory_.read(slot))) memory_.write(*target+0x20,0xffffffff);
    memory_.write(slot,0);
}
void Dialogue::clear_sequence(Address slot) {
    if (const auto sequence=resolve_compact(memory_,memory_.read(slot))) {release_reference(*sequence+0xdc);release_reference(*sequence+0xd0);release_reference(slot);} else memory_.write(slot,0);
}
void Dialogue::enqueue_message(Handle handle, std::uint32_t target, std::uint32_t code) {
    const auto d=state(handle),ctrl=*resolve_compact(memory_,handle),channel=memory_.read(ctrl+vm_offset::message_channel);
    if (!channel) throw Fault(ctrl,"dialog message sender has no compact channel id");
    if (!target) target=memory_.read(d+dialog_offset::default_message_target);
    queue_.enqueue({target,channel,code,handle});
}
bool Dialogue::process_waiting_message(Handle handle) {
    const auto d=state(handle),ctrl=*resolve_compact(memory_,handle);
    if (!(memory_.read(d+dialog_offset::flags_c)&16)) return false;
    memory_.write(d+dialog_offset::flags_c,memory_.read(d+dialog_offset::flags_c)&~0x800u);
    const auto index=queue_.find(memory_.read(ctrl+vm_offset::message_channel),0);
    if (!index) return false;
    const auto message=queue_.at(*index);
    if (!(message.code&255)) {
        const auto filter=memory_.read(d+dialog_offset::waiting_message_channel);
        if (filter&&filter!=message.channel) return false;
        queue_.remove(*index);memory_.write(d+dialog_offset::flags_c,memory_.read(d+dialog_offset::flags_c)&~16u);
        if (memory_.read(d+dialog_offset::flags_b)&0x2000) memory_.write(globals::dialogue_turn_counter,memory_.read(globals::dialogue_turn_counter)+1);
        return true;
    }
    if ((message.code&1)&&(memory_.read(d+dialog_offset::flags_a)&1)&&((memory_.read(d+dialog_offset::flags_c)&3)!=2))
        memory_.write(d+dialog_offset::flags_c,(memory_.read(d+dialog_offset::flags_c)&~1u)|6);
    auto a=memory_.read(d+dialog_offset::flags_a);
    if (message.code&2) a|=32; if (message.code&16) a|=16; if (message.code&32) a&=~16u;
    if (message.code&64) a|=8; if (message.code&128) a&=~8u;
    memory_.write(d+dialog_offset::flags_a,a);queue_.remove(*index);return false;
}
} // namespace fsb::core
