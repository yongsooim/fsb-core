#include "fsb_core/viewport.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/arena.hpp"
#include <algorithm>

namespace fsb::core {
namespace {
Rect read_rect(const Memory& m, Address at) { return {signed32(m.read(at)),signed32(m.read(at + 4)),signed32(m.read(at + 8)),signed32(m.read(at + 12))}; }
void write_rect(Memory& m, Address at, Rect rect) {
    m.write(at, std::uint32_t(rect.left)); m.write(at + 4, std::uint32_t(rect.top)); m.write(at + 8, std::uint32_t(rect.right)); m.write(at + 12, std::uint32_t(rect.bottom));
}
}
Rect Viewport::requested() const { return read_rect(memory_, globals::requested_viewport_rect); }
void Viewport::configure_framebuffer(int width, int height) {
    if (width < 1 || height < 1 || width > 8192 || height > 8192) throw Fault(0x4072ab, "invalid logical framebuffer configuration");
    memory_.write(globals::framebuffer_width, unsigned(width)); memory_.write(globals::framebuffer_height, unsigned(height));
    write_rect(memory_,globals::framebuffer_rect,{0,0,width,height});
    center(width,height,true,false);
}
Rect Viewport::centered(int width, int height) const {
    if (width < 1 || height < 1 || width > 8192 || height > 8192) throw Fault(0x406ce2, "invalid viewport dimensions");
    const auto left = (signed32(memory_.read(globals::framebuffer_width)) - width) / 2, top = (signed32(memory_.read(globals::framebuffer_height)) - height) / 2;
    return {left, top, left + width, top + height};
}
void Viewport::borders(Address block, Rect old, Rect rect) {
    Rect intersection{std::max(old.left,rect.left),std::max(old.top,rect.top),std::min(old.right,rect.right),std::min(old.bottom,rect.bottom)};
    if (intersection.left >= intersection.right || intersection.top >= intersection.bottom) intersection = {}; // Win32 IntersectRect empty output.
    const Rect strips[] = {{old.left,old.top,old.right,intersection.top}, {old.left,intersection.bottom,old.right,old.bottom},
                           {old.left,intersection.top,intersection.left,intersection.bottom}, {intersection.right,intersection.top,old.right,intersection.bottom}};
    const bool active[] = {old.top < intersection.top, intersection.bottom < old.bottom, old.left < intersection.left, intersection.right < old.right};
    for (unsigned i = 0; i < 4; ++i) if (active[i]) { memory_.write(block + i * 4, 1); write_rect(memory_, block + 16 + i * 16, strips[i]); }
}
void Viewport::publish(Rect rect, bool clip, bool border) {
    const auto old = read_rect(memory_, globals::viewport_left); write_rect(memory_, globals::requested_viewport_rect, rect);
    const Rect active{std::max(0,rect.left),std::max(0,rect.top),std::min(signed32(memory_.read(globals::framebuffer_width)),rect.right),std::min(signed32(memory_.read(globals::framebuffer_height)),rect.bottom)};
    write_rect(memory_, globals::viewport_left, active);
    memory_.write(globals::viewport_width, std::uint32_t(active.right - active.left)); memory_.write(globals::viewport_height, std::uint32_t(active.bottom - active.top));
    memory_.write(globals::viewport_center_x, std::uint32_t((rect.right - rect.left) / 2 + rect.left)); memory_.write(globals::viewport_center_y, std::uint32_t((rect.bottom - rect.top) / 2 + rect.top));
    memory_.write(globals::viewport_origin_x, std::uint32_t(active.left)); memory_.write(globals::viewport_origin_y, std::uint32_t(active.top)); memory_.write(globals::viewport_far_x, std::uint32_t(active.right)); memory_.write(globals::viewport_far_y, std::uint32_t(active.bottom));
    if (clip) write_rect(memory_, globals::clip_left, active);
    if (border) {
        const auto handle = Arena(memory_).allocate_after(memory_.read(globals::group0_append_link), routines::viewport_border_tick, 0x10000, 0), object = *resolve_compact(memory_, handle);
        const auto block = memory_.allocate_zeroed(80); memory_.write(object + compact_offset::state_pointer, block); borders(block, old, active);
        memory_.write(object + 0x2c, memory_.read(globals::presentation_page_count));
    }
}
void Viewport::center(int width, int height, bool clip, bool border) { publish(centered(width,height),clip,border); }
void Viewport::tween_center(unsigned duration, int width, int height, bool clip, bool border) {
    const auto target = centered(width,height);
    const auto handle = Arena(memory_).allocate_after(memory_.read(globals::group0_append_link), routines::viewport_tween_tick, 0, 0), object = *resolve_compact(memory_, handle);
    const auto block = memory_.allocate_zeroed(44); memory_.write(object + compact_offset::state_pointer, block);
    write_rect(memory_, block, requested()); write_rect(memory_, block + 16, target);
    memory_.write(block + 32, signed32(duration) < 1 ? 1 : duration); memory_.write(block + 36, clip); memory_.write(block + 40, border);
}
void Viewport::event_view(unsigned subop){
    if(subop>4)throw Fault(0x42a2f4,"invalid map/dialog viewport route");
    if(subop<2&&(memory_.read(0x768a98)||memory_.read(globals::pending_event_id)!=0xffffffffu))return;
    const bool resume=subop==2||subop==3||(subop<2&&memory_.read(0x5c4f5c+memory_.read(globals::current_map_id)*68)==1);
    if(resume||subop==4){
        // 4328ab(0): keep the OSD object alive for its own fade-out callback.
        if(const auto osd=resolve_compact(memory_,memory_.read(globals::debug_osd_handle)))if(memory_.read(*osd+compact_offset::lifecycle)!=4){
            memory_.write(*osd+0x2c,memory_.read(*osd+compact_offset::lifecycle)==1?20-memory_.read(*osd+0x2c):0);
            memory_.write(*osd+compact_offset::lifecycle,4);
        }
    }
    if(subop!=4){
        // 411feb only operates on the independent direct-text controller.
        if(const auto text=resolve_compact(memory_,memory_.read(0x76892c))){
            if(memory_.read(*text+compact_offset::lifecycle)==3){memory_.write(*text+compact_offset::lifecycle,4);memory_.write(*text+0x2c,0);}
            else{memory_.write(*text+compact_offset::lifecycle,0xffffffffu);memory_.write(0x76892c,0);}
        }
    }
    int width,height;unsigned mode;
    if(subop==4){width=signed32(memory_.read(0x4a2648));height=signed32(memory_.read(0x4a264c));mode=2;}
    else if(resume&&!(memory_.read(0x6da2e0)&2)){width=signed32(memory_.read(0x4a2640));height=signed32(memory_.read(0x4a2644));mode=1;}
    else{
        const auto clamp=[](int value,int low,int high){return value<=low?low:value>=high?high:value;};
        width=clamp(signed32(memory_.read(0x5ab8f8)),640,signed32(memory_.read(globals::framebuffer_width)));
        height=clamp(signed32(memory_.read(0x5ab8fc)),480,signed32(memory_.read(globals::framebuffer_height)));
        memory_.write(0x5ab8f8,std::uint32_t(width));memory_.write(0x5ab8fc,std::uint32_t(height));mode=0;
    }
    if(subop==1||subop==3)center(width,height);else tween_center(30,width,height);
    memory_.write(0x769440,mode);
}
void Viewport::blit_borders(std::uint32_t color){
    const auto handle=Arena(memory_).allocate_after(memory_.read(globals::group0_append_link),routines::blit_border_tick,0x10000,0),object=*resolve_compact(memory_,handle);
    const auto block=memory_.allocate_zeroed(80);memory_.write(object+compact_offset::state_pointer,block);
    borders(block,read_rect(memory_,globals::framebuffer_rect),read_rect(memory_,globals::viewport_left));memory_.write(object+0xe8,color);memory_.write(object+0x2c,memory_.read(globals::presentation_page_count));
}
void Viewport::tick(Address object, unsigned jobs) {
    const auto callback = memory_.read(object + compact_offset::callback), block = memory_.read(object + compact_offset::state_pointer);
    if (callback != routines::viewport_border_tick && callback != routines::viewport_tween_tick && callback != routines::blit_border_tick) throw Fault(callback, "not a viewport callback");
    if (!block) throw Fault(object, "viewport callback has no state allocation");
    if (memory_.read(object + compact_offset::lifecycle) == 0xffffffffu) {
        memory_.release_allocation(block); memory_.write(object + compact_offset::state_pointer, 0); memory_.write(object + compact_offset::lifecycle, 0); return;
    }
    if (callback == routines::viewport_tween_tick) {
        if (!memory_.read(object + compact_offset::lifecycle)) { memory_.write(object + 0x2c, 0); memory_.write(object + compact_offset::lifecycle, 1); }
        const auto duration = memory_.read(block + 32); auto elapsed = memory_.read(object + 0x2c) + jobs;
        if (signed32(duration) < signed32(elapsed)) elapsed = duration; memory_.write(object + 0x2c, elapsed);
        std::array<int,4> values{};
        for (unsigned i = 0; i < 4; ++i) {
            const auto start = memory_.read(block + i * 4), target = memory_.read(block + 16 + i * 4);
            values[i] = signed32(sequence_alu(alu::signed_divide, (target - start) * elapsed, duration) + start);
        }
        publish({values[0],values[1],values[2],values[3]}, memory_.read(block + 36) != 0, memory_.read(block + 40) != 0);
        if (signed32(duration) <= signed32(memory_.read(object + 0x2c))) memory_.write(object + compact_offset::lifecycle, 0xffffffff);
    } else {
        for (unsigned i = 0; i < 4; ++i) if (memory_.read(block + i * 4)) {
            memory_.write(globals::surface_state_flags, memory_.read(globals::surface_state_flags) | 16); auto rect = read_rect(memory_, block + 16 + i * 16);
            const auto target = memory_.read(globals::render_target_surface), back = memory_.read(globals::back_surface);
            const auto color=callback==routines::blit_border_tick?std::uint8_t(memory_.read(object+0xe8)):std::uint8_t(0);
            fills_.push_back({target,rect,color});if(callback==routines::blit_border_tick)continue;
            if (target != back) fills_.push_back({back,rect,0});
            if (memory_.read(globals::frame_idle_callback)) {
                if (memory_.read(globals::video_mode_flags) & video_flag::windowed) { const auto x = signed32(memory_.read(0x6d66f0)), y = signed32(memory_.read(0x6d66f4)); rect.left += x; rect.right += x; rect.top += y; rect.bottom += y; }
                fills_.push_back({memory_.read(globals::primary_surface),rect,0});
            }
        }
        const auto remaining = memory_.read(object + 0x2c) - 1; memory_.write(object + 0x2c, remaining);
        if (signed32(remaining) < 1) memory_.write(object + compact_offset::lifecycle, 0xffffffff);
    }
}
std::vector<FillCommand> Viewport::take_fills() { auto result = std::move(fills_); fills_.clear(); return result; }
void Viewport::fill(Image8& target, const FillCommand& command) {
    if (target.pixels.size() != std::size_t(target.width) * target.height) throw Fault(command.surface, "invalid fill surface");
    for (int y = std::max(0,command.rect.top), end_y = std::min(int(target.height),command.rect.bottom); y < end_y; ++y)
        for (int x = std::max(0,command.rect.left), end_x = std::min(int(target.width),command.rect.right); x < end_x; ++x) target.set_pixel(std::size_t(y) * target.width + x,command.index);
}
} // namespace fsb::core
