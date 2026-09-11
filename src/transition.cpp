#include "fsb_core/transition.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/progress_timer.hpp"
#include "fsb_core/actor_fields.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fsb::core {
namespace {
Rect rect(const Memory& m,Address at){return {signed32(m.read(at)),signed32(m.read(at+4)),signed32(m.read(at+8)),signed32(m.read(at+12))};}
void rect(Memory& m,Address at,Rect r){m.write(at,std::uint32_t(r.left));m.write(at+4,std::uint32_t(r.top));m.write(at+8,std::uint32_t(r.right));m.write(at+12,std::uint32_t(r.bottom));}
double real(const Memory& m,Address at){const auto bits=std::uint64_t(m.read(at))|(std::uint64_t(m.read(at+4))<<32);return std::bit_cast<double>(bits);}
void real(Memory& m,Address at,double value){const auto bits=std::bit_cast<std::uint64_t>(value);m.write(at,std::uint32_t(bits));m.write(at+4,std::uint32_t(bits>>32));}
int power_curve(unsigned progress,unsigned power){double value=1;for(unsigned i=0;i<power;++i){value*=progress;if(i)value/=units::progress_complete;}return int(value);} //404343, x87 precision53/truncate.
constexpr Address reveal_tick=0x401fd4,scatter_tick=0x40213b,scatter_points=0x6e0e10;
constexpr int scatter_grid=12,scatter_random_span=3000;
}
void Transition::buffers(){
    for(auto at:{globals::rect_effect_target_surface,globals::rect_effect_sub_surface})if(!memory_.read(at))memory_.write(at,surfaces_.create(memory_.read(globals::framebuffer_width),memory_.read(globals::framebuffer_height)));
}
void Transition::zoom(unsigned mode,Rect source,std::uint32_t duration,std::optional<Rect> destination){
    if(memory_.read(globals::disable_zoom))return;buffers();const auto bounds=rect(memory_,globals::framebuffer_rect);
    const int width=source.right-source.left,height=source.bottom-source.top;
    source.left=bounds.left<source.left?std::min(source.left,bounds.right-width):bounds.left;
    source.top=bounds.top<source.top?std::min(source.top,bounds.bottom-height):bounds.top;source.right=source.left+width;source.bottom=source.top+height;
    rect(memory_,globals::rect_effect_source_rect,source);rect(memory_,globals::rect_effect_target_rect,destination.value_or(rect(memory_,globals::clip_left)));
    memory_.write(globals::frame_idle_callback,routines::point_zoom_tick);
    begin(mode,duration);
}
void Transition::begin(unsigned mode,std::uint32_t duration){
    const auto video=memory_.read(globals::video_mode_flags),state=memory_.read(globals::surface_state_flags);
    const bool use_target=(!(state&surface_flag::blit_backbuffer_path)&&(video&video_flag::default_probe))||((state&surface_flag::blit_backbuffer_path)&&(video&video_flag::system_safe));memory_.write(globals::rect_effect_uses_target,use_target);
    if(use_target){const auto target=memory_.read(globals::rect_effect_target_surface),active=memory_.read(globals::render_target_surface);if(target!=active)surfaces_.blit(target,0,0,active,rect(memory_,globals::framebuffer_rect));memory_.write(globals::render_target_surface,target);}
    ProgressTimer(memory_,globals::rect_effect_timer).start(duration,mode==0,true,memory_.read(globals::frame_time_ms));memory_.write(globals::rect_effect_busy,1);
}
void Transition::start(unsigned kind,unsigned mode,std::uint32_t duration,Address actor){
    if(kind>4)throw Fault(kind,"rectangle transition kind is not connected");
    if(kind==0&&memory_.read(globals::disable_zoom))return;
    if(!actor&&(kind==0||kind==4))throw Fault(kind,"rectangle transition needs an actor anchor");
    buffers();
    const int x=actor?signed32(memory_.read(actor+actor_offset::screen_anchor_x)):0,y=actor?signed32(memory_.read(actor+actor_offset::screen_anchor_y)):0;
    if(kind){rectangle(kind,mode,rect(memory_,globals::viewport_left),duration,actor?actor+actor_offset::screen_anchor_x:0);return;}
    if(kind==0){
        const auto bounds=rect(memory_,globals::framebuffer_rect);
        const auto left=std::max(bounds.left,std::min(x,bounds.right-1)),top=std::max(bounds.top,std::min(y,bounds.bottom-1));
        rect(memory_,globals::rect_effect_source_rect,{left,top,left+1,top+1});rect(memory_,globals::rect_effect_target_rect,rect(memory_,globals::clip_left));
        memory_.write(globals::frame_idle_callback,routines::point_zoom_tick);
    }
    begin(mode,duration);
}
void Transition::rectangle(unsigned kind,unsigned mode,Rect view,std::uint32_t duration,Address anchor_pointer){
    if(kind<1||kind>4)throw Fault(kind,"invalid rectangle transition kind");
    buffers();rect(memory_,globals::rect_effect_source_rect,view);
    if(kind==1||kind==2){
        memory_.write(globals::frame_idle_callback,kind==1?reveal_tick:scatter_tick);
        if(kind==2){
            const int width=(view.right-view.left)/scatter_grid,height=(view.bottom-view.top)/scatter_grid;
            memory_.write(globals::rect_effect_cell_width,unsigned(width));memory_.write(globals::rect_effect_row_height,unsigned(height));
            if(mode==1)for(unsigned i=0;i<scatter_grid*scatter_grid;++i){
                const int dx=int(crt_rand(memory_)%scatter_random_span)-scatter_random_span/2,dy=int(crt_rand(memory_)%scatter_random_span)-scatter_random_span/2;
                memory_.write(scatter_points+i*8,unsigned((dx<0?view.left-width:view.right)+dx));
                memory_.write(scatter_points+i*8+4,unsigned((dy<0?view.top-height:view.bottom)+dy));
            }
        }
    }else if(kind==3){
        memory_.write(0x6da534,unsigned(view.left));memory_.write(0x6da53c,unsigned(view.right));
        memory_.write(globals::rect_effect_cell_width,unsigned(view.right-view.left));memory_.write(globals::rect_effect_row_height,unsigned(view.bottom-view.top));
        memory_.write(globals::frame_idle_callback,0x4022b3);
    }else{
        const int x=anchor_pointer?signed32(memory_.read(anchor_pointer)):(view.right-view.left)/2+view.left;
        const int y=anchor_pointer?signed32(memory_.read(anchor_pointer+4)):(view.bottom-view.top)/2+view.top;
        memory_.write(globals::rect_effect_cell_width,std::uint32_t(view.right-view.left));memory_.write(globals::rect_effect_row_height,std::uint32_t(view.bottom-view.top));
        memory_.write(globals::rect_effect_anchor_x,std::uint32_t(x));memory_.write(globals::rect_effect_anchor_y,std::uint32_t(y));memory_.write(globals::rect_effect_anchor_pointer,anchor_pointer?anchor_pointer:globals::rect_effect_anchor_x);
        if(memory_.read(globals::rect_effect_cached_mode)==mode||!memory_.read(globals::rect_effect_remaining)){
            const double dx=mode?std::max(x-view.left,view.right-x):memory_.read(globals::viewport_width);
            const double dy=mode?std::max(y-view.top,view.bottom-y):memory_.read(globals::viewport_height);
            real(memory_,globals::rect_effect_max_radius,std::sqrt(dx*dx+dy*dy));
        }
        memory_.write(globals::frame_idle_callback,routines::radial_reveal_tick);
    }
    begin(mode,duration);
}
unsigned Transition::tick(std::uint32_t now){
    const auto progress=ProgressTimer(memory_,globals::rect_effect_timer).tick(now);const bool up=memory_.read(globals::rect_effect_counts_up)!=0;
    if(!progress&&!up)memory_.write(globals::rect_effect_busy,0);
    if(progress==units::progress_complete&&up){
        memory_.write(globals::rect_effect_busy,0);memory_.write(globals::frame_idle_callback,0);
        const auto active=memory_.read(globals::render_target_surface),back=memory_.read(globals::back_surface);const auto r=rect(memory_,globals::rect_effect_source_rect);
        if(active!=back){surfaces_.blit(back,r.left,r.top,active,r);memory_.write(globals::render_target_surface,back);}
    }
    return progress;
}
bool Transition::present(std::uint32_t now){
    const auto callback=memory_.read(globals::frame_idle_callback);if(!callback)return false;
    if(callback!=routines::point_zoom_tick&&callback!=routines::radial_reveal_tick&&callback!=0x4022b3&&callback!=reveal_tick&&callback!=scatter_tick)throw Fault(callback,"unknown frame-idle effect");
    const auto progress=tick(now),primary=memory_.read(globals::primary_surface);
    if(callback==routines::point_zoom_tick){
        const auto from=rect(memory_,globals::rect_effect_source_rect),to=rect(memory_,globals::rect_effect_target_rect);
        const auto edge=[&](int a,int b){return signed32(std::uint32_t(a)+sequence_alu(alu::signed_divide,(std::uint32_t(b)-std::uint32_t(a))*progress,units::progress_complete));};
        const Rect source{edge(from.left,to.left),edge(from.top,to.top),edge(from.right,to.right),edge(from.bottom,to.bottom)};
        const bool target_path=!(memory_.read(globals::video_mode_flags)&video_flag::windowed)&&memory_.read(globals::rect_effect_uses_target);
        if(target_path){
            const auto back=memory_.read(globals::back_surface);surfaces_.stretch(back,to,memory_.read(globals::rect_effect_target_surface),source);
            surfaces_.blit(primary,0,0,back,rect(memory_,globals::framebuffer_rect));
        }else surfaces_.stretch(primary,to,memory_.read(globals::render_target_surface),source);
    }else if(callback==reveal_tick){
        const auto view=rect(memory_,globals::rect_effect_source_rect);const int eased=power_curve(progress,4);
        const Rect small{0,0,std::max(1,(view.right-view.left)*eased/int(units::progress_complete)),std::max(1,(view.bottom-view.top)*eased/int(units::progress_complete))};
        const auto sub=memory_.read(globals::rect_effect_sub_surface);
        surfaces_.stretch(sub,small,memory_.read(globals::render_target_surface),view);
        surfaces_.stretch(primary,view,sub,small);
    }else if(callback==scatter_tick){
        const auto view=rect(memory_,globals::rect_effect_source_rect);const int eased=int(units::progress_complete)-power_curve(units::progress_complete-progress,2);
        const auto sub=memory_.read(globals::rect_effect_sub_surface);surfaces_.clear(sub,view,0);
        const int width=signed32(memory_.read(globals::rect_effect_cell_width)),height=signed32(memory_.read(globals::rect_effect_row_height));
        for(unsigned i=0;i<scatter_grid*scatter_grid;++i){
            const int left=view.left+int(i%scatter_grid)*width,top=view.top+int(i/scatter_grid)*height;
            const int sx=signed32(memory_.read(scatter_points+i*8)),sy=signed32(memory_.read(scatter_points+i*8+4));
            const int x=sx-(sx-left)*eased/int(units::progress_complete),y=sy-(sy-top)*eased/int(units::progress_complete);
            surfaces_.blit(sub,x,y,memory_.read(globals::render_target_surface),{left,top,left+width,top+height},false,view);
        }
        surfaces_.blit(primary,view.left,view.top,sub,view);
    }else if(callback==0x4022b3){
        const auto view=rect(memory_,globals::rect_effect_source_rect);const auto sub=memory_.read(globals::rect_effect_sub_surface);
        const int remaining=30030-int(progress),height=view.bottom-view.top,width=view.right-view.left;
        const double scale=double(signed32(fixed_sin(memory_,unsigned(remaining*16384/30030))))*real(memory_,0x4a2460);
        surfaces_.clear(sub,view,0);int bottom=height;
        for(int index=0;index<48;++index){
            const int top=(47-index)*height/48,inset=(width*index/96)*remaining/30030;
            const Rect source{view.left,view.top+top,view.right,view.top+bottom};
            const Rect target{view.left+inset,view.top+top+int(double(height-top)*scale),view.right-inset,view.top+bottom+int(double(height-bottom)*scale)};
            memory_.write(0x6da538,unsigned(source.top));memory_.write(0x6da540,unsigned(source.bottom));
            surfaces_.stretch(sub,target,memory_.read(globals::render_target_surface),source);bottom=top;
        }
        surfaces_.blit(primary,view.left,view.top,sub,view);
    }else{
        const auto view=rect(memory_,globals::rect_effect_source_rect);const auto sub=memory_.read(globals::rect_effect_sub_surface),anchor=memory_.read(globals::rect_effect_anchor_pointer);
        surfaces_.clear(sub,view,0);
        const double radius=double(progress)*real(memory_,globals::rect_effect_max_radius)*real(memory_,tables::rect_progress_reciprocal);
        const int ax=signed32(memory_.read(anchor)),ay=signed32(memory_.read(anchor+4));int bottom=view.bottom;
        for(int offset=view.bottom-view.top-2;offset>=0;offset-=2){
            const auto top=view.top+offset;const double dy=top+1-ay;
            if(radius>0&&dy>=-radius&&dy<=radius){
                // Actual498504 is ASIN despite its decompiler's acos name.
                const int half=int(std::cos(std::asin(dy/radius))*radius);
                const Rect strip{std::max(view.left,ax-half),top,std::min(view.right,ax+half),bottom};
                surfaces_.blit(sub,strip.left,strip.top,memory_.read(globals::render_target_surface),strip);
            }
            bottom=top;
        }
        surfaces_.blit(primary,view.left,view.top,sub,view);
    }
    return true;
}
} // namespace fsb::core
