#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/actor_fields.hpp"

namespace fsb::core {
Yield Vm::dialogue_command(const Instruction& ins) {
    const auto get=[&](unsigned i){return ins.operand(memory_,i).get(memory_,object_);};
    if(ins.opcode==opcode::dialogue_spawn_or_wait){
        if(ins.subop==4){if(memory_.read(globals::compact_runtime_references))return Yield::Forced;}
        else{
            if(!env_.dialogue)throw Fault(ins.pc,"dialogue subsystem is not attached");
            const auto channel=get(2),text=get(1),actor=get(0);
            memory_.write(object_+0xd8,env_.dialogue->spawn_markup(actor,text,channel));
        }
        next(ins);return Yield::Continue;
    }
    if (ins.subop==12) {memory_.write(globals::dialogue_abort_requested,1);queue_.clear();}
    else if (ins.subop==6) memory_.write(object_+vm_offset::result,memory_.read(0x768a90));
    else {
        auto handle=get(0);
        if (ins.subop==0||ins.subop==2||ins.subop==4||ins.subop==8) {
            const auto actor=lookup_actor(memory_,handle);
            if (!actor) throw Fault(ins.pc,"dialog control needs an existing actor");
            handle=memory_.read(actor+actor_offset::dialogue_handle);
        }
        const auto ctrl=resolve_compact(memory_,handle);
        if (ins.subop==0||ins.subop==1) {
            if (ctrl) {memory_.write(*ctrl+0x180,get(1));memory_.write(*ctrl+0x184,get(2));}
        } else if (ins.subop==2||ins.subop==3) {
            if (!ctrl||!memory_.read(*ctrl+0x1a4)) throw Fault(ins.pc,"style control has no live dialogue state");
            const auto d=memory_.read(*ctrl+0x1a4);
            memory_.write(d+0x4c,memory_.read(d+0x4c)|Dialogue::style_flags(get(1)));
            memory_.write(d+12,Dialogue::style_variant(get(1)));
        } else if (ins.subop==4||ins.subop==5) {
            if (!ctrl||!memory_.read(*ctrl+0x1a4)) throw Fault(ins.pc,"dialog result routing has no live state");
            memory_.write(*ctrl+0xe8,0x768a90);
        } else if (ins.subop==8||ins.subop==9) {
            if (ctrl) memory_.write(*ctrl+0x20,0xffffffff);
            // Actor-addressed sub8 releases a local copy: actor+13c stays stale.
            if (ins.subop==9) ins.operand(memory_,0).set(memory_,object_,0);
        } else if (ins.subop==10&&ctrl) memory_.write(*ctrl+0x1a0,1);
    }
    next(ins);return Yield::Continue;
}
} // namespace fsb::core
