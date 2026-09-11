#include "fsb_core/camera.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
void Camera::line_focus(unsigned from,unsigned to,unsigned frames){
    if(!frames)throw Fault(0x4544cf,"zero-duration camera line");
    const auto source=Actors::slot(from),target=Actors::slot(to);
    memory_.write(globals::camera_focus_actor_index,765);memory_.write(0x7873d8,15);memory_.write(0x7873dc,from);memory_.write(0x7873e0,to);
    for(unsigned axis=0;axis<2;++axis){
        memory_.write(0x7873ec+axis*4,std::uint32_t(signed32(memory_.read(source+8+axis*4))/units::q16_one));
        memory_.write(0x7873f4+axis*4,std::uint32_t(signed32(memory_.read(target+8+axis*4))/units::q16_one));
    }
    memory_.write(0x7873fc,frames);memory_.write(0x787400,0);memory_.write(0x7873b8,1);
}
void Camera::line_focus(unsigned to,unsigned frames){
    auto from=memory_.read(globals::camera_focus_actor_index);
    if(from==765){
        const auto remaining=std::uint32_t(std::max(1,signed32(memory_.read(0x7873fc)-memory_.read(0x787400))));
        const auto target=Actors::slot(memory_.read(0x7873e0)),point_x=memory_.read(0x7873e4),point_y=memory_.read(0x7873e8),slot=0x7873d8u+44;
        memory_.write(slot,5);memory_.write(slot+8,memory_.read(0x7873e0));memory_.write(slot+12,point_x);memory_.write(slot+16,point_y);memory_.write(slot+20,point_x);memory_.write(slot+24,point_y);
        memory_.write(slot+28,std::uint32_t(signed32(memory_.read(target+8))/units::q16_one));memory_.write(slot+32,std::uint32_t(signed32(memory_.read(target+12))/units::q16_one));memory_.write(slot+36,remaining);memory_.write(slot+40,0);from=766;
    }
    line_focus(from,to,frames);
}
void Camera::tick_lines(){
    for(int i=2;i>=0;--i){
        const auto slot=0x7873d8+unsigned(i)*44,flags=memory_.read(slot);if(!(flags&1))continue;
        for(unsigned endpoint=0;endpoint<2;++endpoint)if(flags&(2u<<endpoint)){
            const auto object=Actors::slot(memory_.read(slot+4+endpoint*4));
            for(unsigned axis=0;axis<2;++axis)memory_.write(slot+20+endpoint*8+axis*4,std::uint32_t(signed32(memory_.read(object+8+axis*4))/units::q16_one));
        }
        const auto elapsed=memory_.read(slot+40)+1,duration=memory_.read(slot+36);memory_.write(slot+40,elapsed);
        if(signed32(duration)<=signed32(elapsed)){memory_.write(slot,flags&~1u);if((flags&12)==12){memory_.write(globals::camera_focus_actor_index,memory_.read(slot+8));memory_.write(0x7873b8,0);}}
        const auto object=Actors::slot(765+unsigned(i));
        for(unsigned axis=0;axis<2;++axis){
            const auto start=memory_.read(slot+20+axis*4),target=memory_.read(slot+28+axis*4),value=start+sequence_alu(alu::signed_divide,(target-start)*elapsed,duration);
            memory_.write(slot+12+axis*4,value);
            const auto logical=sequence_alu(alu::signed_divide,value<<16,axis?48:64);memory_.write(object+20+axis*4,logical);memory_.write(object+8+axis*4,logical*(axis?48:64));
        }
        const auto layer=std::uint32_t(signed32(memory_.read(object+28))/units::q16_one);memory_.write(object+28,layer*65536+0x8000);
    }
}
} // namespace fsb::core
