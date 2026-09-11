#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/sprites.hpp"

namespace fsb::core {
Yield Vm::sprite_command(const Instruction& ins){
    const auto get=[&](unsigned i){return ins.operand(memory_,i).get(memory_,object_);};
    auto wait=memory_.read(object_+vm_offset::wait_count);
    if(ins.opcode==opcode::compact_sprite_offset){
        //0x42b6af machine-code body: apply offsets once, then drain. A pause
        // only shortcuts FIRST entry; existing waits jump straight to DEC.
        if(!wait){
            wait=get(0);if(signed32(wait)>0)wait+=memory_.read(object_+vm_offset::frame_bias)+memory_.read(globals::animation_frame_bias);
            memory_.write(object_+vm_offset::wait_count,wait);
            memory_.write(object_+0x180,memory_.read(object_+0x180)+get(1));
            memory_.write(object_+0x184,memory_.read(object_+0x184)+get(2));
            if(signed32(wait)<=0||env_.input_pause||env_.dialog_busy){memory_.write(object_+vm_offset::wait_count,0);next(ins);return Yield::Continue;}
        }
    }else{
        const auto selector=get(1),frame=get(2),family=selector&0xffff;
        if(!wait){
            if(!env_.sprites)throw Fault(ins.pc,"compact sprite bank is not attached");
            wait=memory_.read(object_+vm_offset::frame_bias)+memory_.read(globals::animation_frame_bias)+get(0);memory_.write(object_+vm_offset::wait_count,wait);
            SpriteSurface sprite;
            switch(selector>>16){
            case 0:
                if(family>=335)throw Fault(selector,"compact sprite family outside table");
                sprite=env_.sprites->character(memory_.read(globals::character_family_first_frame+family*4)+frame);break;
            case 1:sprite=env_.sprites->indexed(family,frame);break;
            case 2:sprite=env_.sprites->indexed(family+memory_.read(object_+0x1a0),frame);break;
            default:throw Fault(selector,"compact sprite tag is not connected");
            }
            memory_.write(object_+0x188,sprite.surface);
            memory_.write(object_+0x18c,sprite.rect.left);memory_.write(object_+0x190,sprite.rect.top);
            memory_.write(object_+0x194,sprite.rect.right);memory_.write(object_+0x198,sprite.rect.bottom);
            memory_.write(object_+0x178,sprite.origin_x);memory_.write(object_+0x17c,sprite.origin_y);
            if(signed32(wait)<1){memory_.write(object_+vm_offset::wait_count,0);next(ins);return Yield::Continue;}
        }
        if(env_.input_pause||env_.dialog_busy)wait=1;
    }
    memory_.write(object_+vm_offset::wait_count,--wait);if(!wait)next(ins);return Yield::Normal;
}
} // namespace fsb::core
