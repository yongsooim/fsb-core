#include "fsb_core/dialogue.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/progress_timer.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/audio.hpp"
#include <algorithm>

namespace fsb::core {
bool Dialogue::rebuild(Address d,unsigned mode){
    auto c=memory_.read(d+dialog_offset::flags_c);
    if(mode==2){if(!(memory_.read(d+dialog_offset::flags_a)&1)||(c&3)==2)return false;memory_.write(d+dialog_offset::flags_c,(c&~1u)|6);return true;}
    if(mode!=1)throw Fault(d,"invalid dialogue rebuild mode");
    if(!memory_.read(d+dialog_offset::text_surface))graphics->layout(d);
    if((memory_.read(d+dialog_offset::flags_a)&1)||(c&3)==1)return false;
    memory_.write(d+dialog_offset::flags_c,(c&~2u)|5);return true;
}
void Dialogue::delay(Address d,std::uint32_t duration){
    // dlgbox_noop_411d12 is literally return0 in this executable, not active-dialog count.
    if(!memory_.read(globals::shift_key_state)&&!memory_.read(globals::numpad5_held)&&!(memory_.read(d+0x159,1)&128)){
        memory_.write(d+dialog_offset::flags_c,memory_.read(d+dialog_offset::flags_c)|128);ProgressTimer(memory_,d+dialog_offset::delay_timer).start(duration,true,false,memory_.read(globals::frame_time_ms));
    }
}
bool Dialogue::segment_wait(Address d,Address cursor){
    if(memory_.read(cursor,1)==';'||!(memory_.read(d+dialog_offset::flags_a)&4)){
        if(!(memory_.read(d+dialog_offset::flags_b)&128))memory_.write(d+dialog_offset::flags_c,memory_.read(d+dialog_offset::flags_c)|0x820);
        else delay(d,memory_.read(globals::event_dialogue_auto_delay)*100);
        memory_.write(d+dialog_offset::pause_count,memory_.read(d+dialog_offset::line));return true;
    }
    return false;
}
bool Dialogue::advance_line(Address d,bool explicit_line){
    auto line=memory_.read(d+dialog_offset::line)+1;const auto count=memory_.read(d+dialog_offset::line_capacity);
    memory_.write(d+dialog_offset::column,explicit_line?memory_.read(d+dialog_offset::first_indent)-memory_.read(d+dialog_offset::trimmed_columns):memory_.read(d+dialog_offset::continuation_indent));memory_.write(d+dialog_offset::explicit_line_start,explicit_line);
    const bool more=signed32(line)<signed32(count);memory_.write(d+dialog_offset::line,more?line:count-1);
    if(!more){graphics->capture_reveal(d);memory_.write(d+0x15d,memory_.read(d+0x15d,1)|9,1);ProgressTimer(memory_,d+dialog_offset::delay_timer).start(160,true,false,memory_.read(globals::frame_time_ms));}
    return more;
}
bool Dialogue::bypass_delay(Address d)const{
    if(memory_.read(globals::shift_key_state)||memory_.read(globals::numpad5_held)||(memory_.read(d+0x159,1)&128))return true;
    const auto corner=memory_.read(globals::dialogue_input_corner_flags);
    return (!(corner&1)&&memory_.read(globals::enter_held))||(!(corner&2)&&memory_.read(globals::left_mouse_held,1))||memory_.read(globals::space_held)||memory_.read(globals::right_mouse_held,1)||memory_.read(globals::x_held)||memory_.read(globals::numpad1_held);
}
bool Dialogue::confirm_advance(){
    const auto event=memory_.read(globals::input_message),key=memory_.read(globals::input_key),flags=memory_.read(globals::input_flags);
    const auto packed=((flags>>1)&0x800000)|(flags&0xff0000);
    const bool confirm=event==input_message::key_down&&!(flags&0x40000000)&&(key==13||key==32||key=='X'||packed==0x4f0000);
    const bool cancel=event==input_message::mouse_button&&(key&0x81)==0x81;
    const auto old=memory_.read(globals::dialogue_input_corner_flags);memory_.write(globals::dialogue_input_corner_flags,(old&~3u)|unsigned(confirm)|(unsigned(cancel)<<1));
    return confirm||cancel||(event==input_message::mouse_button&&(key&0x41)==0x41)||(memory_.read(globals::space_held)&&memory_.read(globals::enter_held));
}
void Dialogue::glyph(Address d,Address cursor,unsigned bytes){
    const auto first=memory_.read(cursor,1),next=memory_.read(cursor+bytes,1);
    const auto style=memory_.read(d+dialog_offset::text_style),b=memory_.read(d+dialog_offset::flags_b),family=memory_.read(d+dialog_offset::box_style);
    const auto scale=unsigned(std::clamp(signed32(memory_.read(d+dialog_offset::wait_scale)),0,4999));
    auto wait=memory_.read(d+dialog_offset::wait_time);bool draw=true,advance=true;
    const auto cp=bytes==2?(first<<8)|memory_.read(cursor+1,1):first;
    if(bytes==2){wait+=scale;if(cp==0xa1a6)wait+=scale*800/100;else if(cp==0xa1ad)wait+=scale*1900/100;}
    else if(!next||next==';')wait-=scale;
    else if(first==' '){wait+=scale*2;if(!(style&0x200)){draw=false;if(!memory_.read(d+dialog_offset::explicit_line_start)&&memory_.read(d+dialog_offset::column)==memory_.read(d+dialog_offset::continuation_indent))advance=false;}}
    else if(first=='~'){draw=false;wait=0xffffffff;}
    else if(first=='!')wait+=(next!='!'?1300u:0u)*scale/100;
    else if(first==',')wait+=scale*7;
    else if(first=='.')wait+=scale*((next=='.'||next==','||next=='<')?100u:1100u)/100;
    else if(first=='?')wait+=scale*17;
    memory_.write(d+dialog_offset::wait_time,wait);
    if(draw){
        unsigned font=5+((b&64)?1:(style&7));if((style&8)||(b&64))font+=4;
        const bool large=style&0x3000;if(large)font+=8;
        const int x=signed32(memory_.read(d+dialog_offset::source_left))+16+signed32(memory_.read(d+dialog_offset::column))*8;
        const int y=signed32(memory_.read(d+dialog_offset::source_top))+19+signed32(memory_.read(d+dialog_offset::line))*22;
        Rect rect{x-1,y,x+(large?32:15),y+16};if(style&0x1000)rect.top-=16;else if(style&0x2000)rect.bottom=y+32;
        const auto use_state=((b&32)&&(style&32))?(style&256):((~style>>8)&1);
        auto foreground=use_state?memory_.read(d+dialog_offset::foreground):0x1000078u,outline=memory_.read(d+dialog_offset::outline);
        if(!use_state&&family>=2)outline=memory_.read(0x57ede4+(memory_.read(0x57edb8)+memory_.read(d+dialog_offset::style_variant)*2)*32,2)|0x1000000;
        graphics->draw_glyph(d,{std::uint16_t(cp),font,rect,style,b,std::uint8_t(foreground),std::uint8_t(outline),large?2u:family<2?2u:1u});
    }
    if(advance)memory_.write(d+dialog_offset::column,memory_.read(d+dialog_offset::column)+bytes);
}
void Dialogue::tick(Address ctrl){
    if(!graphics)throw Fault(ctrl,"dialogue CPU graphics not attached");
    const auto d=memory_.read(ctrl+compact_offset::state_pointer);if(!d)throw Fault(ctrl,"dialogue controller has no state");
    const auto get=[&](unsigned i){return memory_.read(d+i*4);};const auto put=[&](unsigned i,std::uint32_t v){memory_.write(d+i*4,v);};
    const auto now=memory_.read(globals::frame_time_ms);bool tween_ran=false;
    const auto reaction=[&](){
        const auto flags=get(dialog_word::flags_a);
        if(flags&8)graphics->reaction(0,signed32(get(dialog_word::anchor_x)),signed32(get(dialog_word::anchor_y)));
        if(flags&16)graphics->reaction(1,signed32(get(dialog_word::anchor_x)),signed32(get(dialog_word::anchor_y)));
        if(flags&32){
            for(const auto entry:{unsigned(scripts::sweat_left),unsigned(scripts::sweat_right)}){
                const auto handle=Arena(memory_).clone_event(entry,4),effect=*resolve_compact(memory_,handle);
                memory_.write(effect+compact_offset::flags,memory_.read(effect+compact_offset::flags)|0x1000000);
                memory_.write(effect+0x164,get(dialog_word::anchor_x_pointer)); // Pointer to the actor's screen-anchor pair, not its actor id.
                memory_.write(effect+0x180,memory_.read(ctrl+0x180));memory_.write(effect+0x184,memory_.read(ctrl+0x184));memory_.write(effect+0x19c,1);
            }
            put(dialog_word::flags_a,get(dialog_word::flags_a)&~32u);
        }
    };
    const auto tail=[&](){const bool occupying=(get(dialog_word::flags_a)&1)||(get(dialog_word::flags_c)&3)==1;graphics->draw_speaker_name(d,occupying);if(occupying)graphics->occupy(d);graphics->tail(d);reaction();};
    if(memory_.read(ctrl+compact_offset::lifecycle)==0xffffffffu){
        if(!(get(dialog_word::flags_b)&0x1000)){graphics->destroy(d);memory_.release_allocation(d);memory_.write(ctrl+compact_offset::state_pointer,0);
            if(memory_.read(ctrl+0x19c))set_actor_tile_state(memory_,memory_.read(ctrl+vm_offset::actor_id),1);
            memory_.write(ctrl+compact_offset::lifecycle,0);memory_.write(globals::live_dialogue_count,memory_.read(globals::live_dialogue_count)-1);return;}
        put(dialog_word::flags_b,get(dialog_word::flags_b)&~0x1000u);const auto text=get(dialog_word::text)+get(dialog_word::cursor);initialize_chunk(d,text);graphics->layout(d);memory_.write(ctrl+compact_offset::lifecycle,1);reaction();return;
    }
    if(memory_.read(globals::dialogue_abort_requested)){queue_.clear();memory_.write(ctrl+compact_offset::lifecycle,0xffffffff);return;}
    if((get(dialog_word::flags_b)&0x800)&&!(get(dialog_word::flags_c)&3)){put(dialog_word::flags_b,get(dialog_word::flags_b)&~0x800u);memory_.write(ctrl+compact_offset::lifecycle,0xffffffff);reaction();return;}
    graphics->position(ctrl,d);
    if(memory_.read(globals::input_message)==0x101&&memory_.read(globals::input_key)==13)memory_.write(globals::dialogue_input_corner_flags,memory_.read(globals::dialogue_input_corner_flags)&~1u);
    else if(memory_.read(globals::input_message)==0x403&&(memory_.read(globals::input_key)&0x81)==0x81)memory_.write(globals::dialogue_input_corner_flags,memory_.read(globals::dialogue_input_corner_flags)&~2u);
    for(unsigned instructions=0;instructions<100000;++instructions){
        auto c=get(dialog_word::flags_c),b=get(dialog_word::flags_b),a=get(dialog_word::flags_a);
        if(c&4){put(dialog_word::flags_c,c&~4u);put(dialog_word::flags_a,a&~2u);const auto phase=c&3;
            if(phase!=1&&phase!=2)throw Fault(ctrl,"dialog tween setup without an opening/closing phase");
            const bool timed=!(b&0x4000)&&!memory_.read(globals::shift_key_state)&&!memory_.read(globals::numpad5_held)&&!(b&0x8000);
            if(phase==1){for(unsigned i=0;i<4;++i)put(dialog_word::current_rect+i,get(dialog_word::target_rect+i));if(timed)ProgressTimer(memory_,d+dialog_offset::box_timer).start(memory_.read(tables::dialogue_open_duration),true,true,now);else{put(dialog_word::flags_a,get(dialog_word::flags_a)|1);put(dialog_word::flags_c,get(dialog_word::flags_c)&~3u);}}
            else{put(dialog_word::flags_a,get(dialog_word::flags_a)&~1u);if(timed)ProgressTimer(memory_,d+dialog_offset::box_timer).start(memory_.read(tables::dialogue_close_duration),false,true,now);else put(dialog_word::flags_c,get(dialog_word::flags_c)&~3u);}
        }
        c=get(dialog_word::flags_c);
        if((c&3)&&!tween_ran){
            tween_ran=true;const auto progress=ProgressTimer(memory_,d+dialog_offset::box_timer).tick(now);const bool opening=(c&3)==1;
            if((opening&&progress==units::progress_complete)||(!opening&&progress==0)){put(dialog_word::flags_c,c&~3u);if(opening)put(dialog_word::flags_a,get(dialog_word::flags_a)|1);}
            else{
                const auto width=get(dialog_word::width),height=get(dialog_word::height);if(!height)throw Fault(d,"zero dialog box height");
                auto vertical=std::uint32_t(std::uint64_t(progress)*width/height);
                if(opening&&vertical>units::progress_complete){if(!(get(dialog_word::flags_a)&2)){put(dialog_word::flags_a,get(dialog_word::flags_a)|2);ProgressTimer(memory_,d+dialog_offset::box_timer).scale_speed(1,1,now);}vertical=units::progress_complete;}
                else if(!opening){if(vertical<units::progress_complete){if(!(get(dialog_word::flags_a)&2)){put(dialog_word::flags_a,get(dialog_word::flags_a)|2);ProgressTimer(memory_,d+dialog_offset::box_timer).scale_speed(2,5,now);}}else vertical=units::progress_complete;}
                const int ax=signed32(get(dialog_word::anchor_x)),ay=signed32(get(dialog_word::anchor_y));
                const auto axis=[&](unsigned field,int origin,unsigned value){return int((std::int64_t(signed32(get(field)))-origin)*value/units::progress_complete)+origin;};
                graphics->body(d,{axis(0x31,ax,progress),axis(0x32,ay,vertical),axis(0x33,ax,progress),axis(0x34,ay,vertical)});
            }
            if((get(dialog_word::flags_c)&3)&&((get(dialog_word::flags_c)&3)!=1||!(memory_.read(d+0x159,1)&2))){tail();return;}
        }
        c=get(dialog_word::flags_c);
        if(c&0x400){if(c&0x800){put(dialog_word::flags_c,c&~0x800u);tail();return;}
            if(resolve_compact(memory_,memory_.read(ctrl+vm_offset::auxiliary_child))){tail();return;}
            memory_.write(ctrl+vm_offset::auxiliary_child,0);put(dialog_word::flags_c,get(dialog_word::flags_c)&~0x400u);if(get(dialog_word::flags_b)&0x2000)memory_.write(globals::dialogue_turn_counter,memory_.read(globals::dialogue_turn_counter)+1);
        }
        if(get(dialog_word::flags_c)&16){if(!process_waiting_message(compact_handle(memory_,ctrl))){tail();return;}}
        c=get(dialog_word::flags_c);
        if(c&32){put(dialog_word::flags_c,c&~0x800u);if(confirm_advance()||auto_advance_){put(dialog_word::flags_c,get(dialog_word::flags_c)&~32u);put(dialog_word::flags_a,get(dialog_word::flags_a)|4);}tail();return;}
        if(c&64)if(!update_selection(ctrl,d)){tail();return;}
        if(c&128){auto progress=ProgressTimer(memory_,d+dialog_offset::delay_timer).tick(now);if(memory_.read(globals::shift_key_state)||memory_.read(globals::numpad5_held))progress=units::progress_complete;if(progress!=units::progress_complete){tail();return;}
            put(dialog_word::flags_c,get(dialog_word::flags_c)&~128u);if(get(dialog_word::flags_b)&128)put(dialog_word::flags_a,get(dialog_word::flags_a)|4);}
        if(get(dialog_word::flags_c)&256){put(dialog_word::flags_c,get(dialog_word::flags_c)&~0x800u);auto progress=ProgressTimer(memory_,d+dialog_offset::delay_timer).tick(now);if(memory_.read(globals::shift_key_state)||memory_.read(globals::numpad5_held)||(get(dialog_word::flags_b)&0x8000))progress=units::progress_complete;
            graphics->reveal(d,progress,false);if(progress!=units::progress_complete){rebuild(d,1);tail();return;}put(dialog_word::flags_c,get(dialog_word::flags_c)&~256u);if(signed32(get(dialog_word::pause_count))>0)put(dialog_word::pause_count,get(dialog_word::pause_count)-1);}
        if(get(dialog_word::flags_c)&512){put(dialog_word::flags_c,get(dialog_word::flags_c)&~0x800u);auto progress=ProgressTimer(memory_,d+dialog_offset::delay_timer).tick(now);if(!(get(dialog_word::flags_a)&1)&&!(get(dialog_word::flags_c)&3))progress=units::progress_complete;
            graphics->reveal(d,progress,true);if(progress!=units::progress_complete){tail();return;}put(dialog_word::flags_c,get(dialog_word::flags_c)&~512u);put(dialog_word::pause_count,0);}
        if(((get(dialog_word::flags_a)&1)||(get(dialog_word::flags_c)&3))&&!bypass_delay(d)&&now>=get(dialog_word::glyph_time_ms)){
            const auto scale=unsigned(std::clamp(signed32(get(dialog_word::wait_scale)),0,4999));if(get(dialog_word::wait_time)+get(dialog_word::glyph_time_ms)+scale>now){tail();return;}}
        put(dialog_word::wait_time,0);if(get(dialog_word::flags_b)&0x800){tail();return;}
        const auto cursor=get(dialog_word::text)+get(dialog_word::cursor);const auto token=tokenize_markup(memory_,cursor);
        const auto advance=[&](){put(dialog_word::cursor,get(dialog_word::cursor)+token.bytes);};
        if(!token.code){
            if(!(get(dialog_word::flags_b)&1024)){if(rebuild(d,1)){tail();return;}}
            else if((get(dialog_word::flags_a)&1)||(get(dialog_word::flags_c)&3)){if(!get(dialog_word::text_surface))graphics->layout(d);rebuild(d,2);tail();return;}
            if(signed32(get(dialog_word::column)+token.bytes)>signed32(get(dialog_word::column_capacity))){
                const bool last=signed32(get(dialog_word::line)+1)>=signed32(get(dialog_word::line_capacity));if(last&&!get(dialog_word::pause_count)&&segment_wait(d,cursor)){put(dialog_word::pause_count,get(dialog_word::line_capacity)-1);tail();return;}
                if(!advance_line(d,false)){tail();return;}
            }
            glyph(d,cursor,token.bytes);advance();put(dialog_word::flags_a,get(dialog_word::flags_a)&~4u);put(dialog_word::glyph_time_ms,now);
            if(get(dialog_word::wait_time)==0xffffffffu)continue;
            if((get(dialog_word::flags_b)&1024)&&!(get(dialog_word::flags_a)&1)&&!(get(dialog_word::flags_c)&3)&&!(get(dialog_word::flags_b)&0x800))continue;
            if((memory_.read(globals::shift_key_state)||memory_.read(globals::numpad5_held)||(get(dialog_word::flags_b)&0x8000))&&!(get(dialog_word::flags_c)&0xe70)&&!(memory_.read(d+0x159,1)&8))continue;
            tail();return;
        }
        const auto arg=token.argument;
        const auto bit_b=[&](unsigned mask,unsigned shift){put(dialog_word::flags_b,(get(dialog_word::flags_b)&~mask)|((arg&1)<<shift));};
        switch(token.code){
        case 1:rebuild(d,2);[[fallthrough]];
        case 2:if(token.code==2)put(dialog_word::flags_a,get(dialog_word::flags_a)&~1u);advance();put(dialog_word::flags_b,(get(dialog_word::flags_b)&~0x1000u)|(token.code==2?0x1000u:0u)|0x800);tail();return;
        case 3:
            if(signed32(get(dialog_word::line)+1)<signed32(get(dialog_word::line_capacity))||get(dialog_word::pause_count)||!segment_wait(d,cursor)){if(signed32(get(dialog_word::line)+1)<signed32(get(dialog_word::line_capacity)))put(dialog_word::flags_a,get(dialog_word::flags_a)&~4u);advance_line(d,true);}
            else{put(dialog_word::pause_count,get(dialog_word::line_capacity)-1);tail();return;}break;
        case 4:segment_wait(d,cursor);break;
        case 5:put(dialog_word::line,0);put(dialog_word::explicit_line_start,1);put(dialog_word::column,get(dialog_word::first_indent)-get(dialog_word::trimmed_columns));put(dialog_word::flags_c,get(dialog_word::flags_c)|0xa00);ProgressTimer(memory_,d+dialog_offset::delay_timer).start(320,true,false,now);break;
        case 6:put(dialog_word::flags_b,get(dialog_word::flags_b)^32);break;case 7:put(dialog_word::flags_b,get(dialog_word::flags_b)^64);break;
        case 8:case 9:rebuild(d,token.code==8?2:1);break;case 10:bit_b(1024,10);break;
        case 11:put(dialog_word::flags_a,(get(dialog_word::flags_a)&~8u)|((arg&1)<<3));break;case 12:put(dialog_word::flags_a,(get(dialog_word::flags_a)&~16u)|((arg&1)<<4));break;case 13:put(dialog_word::flags_a,get(dialog_word::flags_a)|32);break;
        case 14:put(dialog_word::box_style,arg);if(arg==3)put(dialog_word::style_variant,8);else if(arg==4)put(dialog_word::style_variant,9);apply_style(d);break;
        case 15:put(dialog_word::width_profile,arg);break;case 16:put(dialog_word::min_lines,arg);break;case 17:put(dialog_word::max_lines,arg);break;case 18:put(dialog_word::forced_lines,arg);break;case 19:put(dialog_word::style_variant,unsigned(std::clamp(signed32(arg),0,45)));break;
        case 20:bit_b(2,1);refresh_gaps(d);break;case 21:bit_b(16,4);break;
        case 22:{
            unsigned length=0;while(length<capacity::speaker_name_bytes&&memory_.read(arg+length,1)!='>')++length;
            memory_.write(d+dialog_offset::speaker_name_length,length);
            for(unsigned i=0;i<length;++i)memory_.write(d+dialog_offset::speaker_name+i,memory_.read(arg+i,1),1);
            put(dialog_word::flags_b,get(dialog_word::flags_b)|16);break;
        }
        case 23:bit_b(128,7);break;case 24:bit_b(256,8);break;case 25:put(dialog_word::position,arg);put(dialog_word::flags_b,(get(dialog_word::flags_b)&~4u)|((token.auxiliary&1)<<2));break;
        case 26:put(dialog_word::position_order,arg);break;case 27:bit_b(8,3);break;
        case 28:put(dialog_word::text_style,(get(dialog_word::text_style)&~7u)|(arg&7));if((arg>>16)==1)put(dialog_word::text_style,get(dialog_word::text_style)&~8u);else if((arg>>16)==2)put(dialog_word::text_style,get(dialog_word::text_style)|8);break;
        case 29:put(dialog_word::text_style,arg?get(dialog_word::text_style)|8:get(dialog_word::text_style)&~8u);break;
        case 30:put(dialog_word::choice_scroll,get(dialog_word::choice_scroll)+1);break;case 31:start_selection(ctrl,d);break;
        case 32:put(dialog_word::first_indent,arg);break;case 33:put(dialog_word::continuation_indent,arg);break;case 34:if(!arg)put(dialog_word::saved_text_style,get(dialog_word::text_style));else if(arg==1)put(dialog_word::text_style,get(dialog_word::saved_text_style));break;
        case 35:put(dialog_word::text_style,arg==10?(get(dialog_word::text_style)&~16u)^32:(get(dialog_word::text_style)&~0xf0u)|arg);break;
        case 36:{auto flags=get(dialog_word::text_style);switch(arg){case 0:flags&=0xffff00ffu;break;case 1:flags|=256;break;case 2:flags&=~256u;break;case 3:flags^=256;break;case 4:flags|=0x1000;break;case 5:flags|=0x2000;break;case 6:flags&=~0x3000u;break;case 7:flags|=512;break;case 8:flags&=~512u;break;case 9:flags|=1024;break;case 10:flags&=~1024u;break;case 11:flags&=0xffffc9ffu;break;default:throw Fault(ctrl,"invalid text display modifier");}put(dialog_word::text_style,flags);break;}
        case 37:bit_b(512,9);break;case 38:bit_b(32768,15);break;
        case 39:if(!(get(dialog_word::flags_a)&1)||!memory_.read(cursor+token.bytes,1))delay(d,arg*100);else put(dialog_word::wait_time,arg*100);break;
        case 40:put(dialog_word::wait_scale,arg);break;case 41:put(dialog_word::wait_scale,get(dialog_word::wait_scale)+arg);break;case 42:bit_b(8192,13);break;
        case 43:memory_.write(ctrl+vm_offset::message_channel,arg);break;case 44:put(dialog_word::default_message_target,arg);break;case 45:enqueue_message(compact_handle(memory_,ctrl),arg,token.auxiliary);break;
        case 46:put(dialog_word::flags_c,get(dialog_word::flags_c)|0x810);put(dialog_word::waiting_message_channel,arg==0xffffffffu?get(dialog_word::default_message_target):arg);break;
        case 48:case 49:
            graphics->inline_sprite(d,arg,token.code==49);
            //40ee1b..40ee27 pushes(debug_dialog_voice_6db584&3)+4 before
            // joining the normal sound helper. The decompiler lost this arg.
            if(token.code==48&&(get(dialog_word::text_style)&0x3000)){if(!audio)throw Fault(cursor,"large item cue needs audio service");audio->play_cue((memory_.read(0x6db584)&3)+4);}
            break;
        case 50:put(dialog_word::flags_c,get(dialog_word::flags_c)|0xc00);break;
        case 51:memory_.write(ctrl+vm_offset::auxiliary_child,Actors(memory_).start_turn(memory_.read(ctrl+vm_offset::actor_id),token.auxiliary,arg));break;
        case 52:memory_.write(ctrl+vm_offset::auxiliary_child,Actors(memory_).start_walk(memory_.read(ctrl+vm_offset::actor_id),token.auxiliary,arg>>16,0,arg&0xffff));break;
        case 53:graphics->show_direct_text(0x7683e0);break;
        case 54:if(!audio)throw Fault(ctrl,"dialog sound subsystem not attached");audio->play_cue(arg);break;
        case 55:if(!audio)throw Fault(ctrl,"dialog BGM subsystem not attached");audio->apply_bgm(arg);break;
        default:throw Fault(cursor,"recognized markup command is not implemented");
        }
        advance();
    }
    throw Fault(ctrl,"dialog token watchdog; no forced cursor advance");
}
} // namespace fsb::core
