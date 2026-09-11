#include "fsb_core/vm.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
Yield Vm::ifc_command(const Instruction& ins){
    Actors actor_fallback(memory_);auto& actors=env_.actors?*env_.actors:actor_fallback;const auto get=[&](unsigned i){return ins.operand(memory_,i).get(memory_,object_);};
    // 430b1e/430ba9 use unchecked 32-bit address arithmetic. Event22 itself
    // restores slot0x90 at63678c (although it staged slot0); do not silently
    // correct the authored operand or impose a 64-row guard absent in x86.
    const auto row=[](unsigned slot){return 0x768aa0u+slot*36;};
    const auto place=[&](unsigned id,unsigned z,unsigned x,unsigned y,unsigned facing){
        const auto actor=lookup_actor(memory_,id);if(!actor)throw Fault(id,"IFC placement actor is absent");
        set_actor_tile_position(memory_,actor,signed32(x),signed32(y),signed32(z));
        if(facing!=0xffffffffu){memory_.write(actor+actor_offset::facing,facing);memory_.write(actor+actor_offset::target_facing,facing);actors.clear_frame(actor);}
    };
    if(ins.opcode==opcode::stage_actor){
        const auto slot=get(0),id=get(1);const auto actor=lookup_actor(memory_,id);
        if(slot!=0xffffffffu){
            const auto stage=row(slot);memory_.write(stage,id);memory_.write(stage+4,actor);
            if(actor){
                const auto flags=memory_.read(actor+actor_offset::flags);memory_.write(stage+8,(flags>>6)&1);memory_.write(stage+12,(flags>>16)&1);
                memory_.write(stage+16,sequence_alu(alu::arithmetic_shift_right,memory_.read(actor+actor_offset::layer_q16),16));
                const auto tx=std::uint32_t(signed32(memory_.read(actor+actor_offset::world_x))/64),ty=std::uint32_t(signed32(memory_.read(actor+actor_offset::world_y))/48);
                memory_.write(actor+actor_offset::tile_x_q16,tx);memory_.write(actor+actor_offset::tile_y_q16,ty);
                memory_.write(actor+actor_offset::tile_x,sequence_alu(alu::arithmetic_shift_right,tx,16));memory_.write(actor+actor_offset::tile_y,sequence_alu(alu::arithmetic_shift_right,ty,16));
                memory_.write(stage+20,memory_.read(actor+actor_offset::tile_x));memory_.write(stage+24,memory_.read(actor+actor_offset::tile_y));memory_.write(stage+28,memory_.read(actor+actor_offset::facing));memory_.write(stage+32,0);
            }
        }
        if(id>=65536)throw Fault(ins.pc,"IFC command requires a template ID");
        const auto z=get(2),x=get(3),y=get(4),facing=get(5);
        if(actor&&(memory_.read(actor+actor_offset::flags)&64)&&ins.subop==0){
            set_actor_tile_state(memory_,actor,0);const auto handle=actors.follow_path(id,signed32(x),signed32(y),facing,0,false,false,true);
            if(slot!=0xffffffffu)memory_.write(row(slot)+32,handle);
        }else{
            const auto destination=actor?actor:actors.spawn_sequence(id);
            actors.visible(destination,true);set_actor_tile_state(memory_,destination,0);place(id,z,x,y,facing);
        }
    }else{
        const auto stage=row(get(0)),id=memory_.read(stage),actor=memory_.read(stage+4);
        if(!actor){
            if(const auto current=lookup_actor(memory_,id)){
                if(!env_.dialogue)throw Fault(ins.pc,"IFC cleanup requires dialogue service");
                env_.dialogue->clear_sequence(current+actor_offset::sequence_handle);env_.dialogue->release_reference(current+actor_offset::dialogue_handle);actors.finalize(current);
            }
        }else if(!ins.subop){actors.visible(actor,memory_.read(stage+8)!=0);set_actor_tile_state(memory_,actor,memory_.read(stage+12));}
        else if(!memory_.read(stage+8)){
            actors.visible(actor,false);set_actor_tile_state(memory_,actor,memory_.read(stage+12));place(id,memory_.read(stage+16),memory_.read(stage+20),memory_.read(stage+24),memory_.read(stage+28));
        }else if(memory_.read(stage+8)==1){
            actors.visible(actor,true);const auto handle=actors.follow_path(id,signed32(memory_.read(stage+20)),signed32(memory_.read(stage+24)),memory_.read(stage+28),0,false,false,true);
            memory_.write(stage+32,handle);if(!handle)throw Fault(ins.pc,"IFC followup has no child for state restoration");
            memory_.write(*resolve_compact(memory_,handle)+0x1a0,memory_.read(stage+12));
        }else throw Fault(ins.pc,"invalid IFC commit mode");
    }
    next(ins);return Yield::Continue;
}
} // namespace fsb::core
