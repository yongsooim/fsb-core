#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/arena.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/dialogue.hpp"

namespace fsb::core {
Yield Vm::object_command(const Instruction& ins) {
    const auto op = [&](unsigned n) { return ins.operand(memory_, n); };
    const auto get = [&](unsigned n) { return op(n).get(memory_, object_); };
    const auto width = [&](unsigned n) { return op(n).descriptor & unsigned(operand_flag::width_mask); };
    if(ins.opcode==opcode::actor_arc_child){
        const auto id=get(0),actor=lookup_actor(memory_,id);if(!actor)throw Fault(ins.pc,"arc child needs a materialized actor");
        const auto handle=Arena(memory_).clone_event(0x6ca14f,1),child=*resolve_compact(memory_,handle);
        memory_.write(child+vm_offset::actor_id,id);memory_.write(child+vm_offset::actor_object,actor);
        const auto x=get(1),y=get(2),height=get(3),duration=get(4);
        memory_.write(child+0x168,ins.subop?((x<<16)|0x8000u)*64-memory_.read(actor+actor_offset::world_x):x<<22);
        memory_.write(child+0x16c,ins.subop?((y<<16)|0x8000u)*48-memory_.read(actor+actor_offset::world_y):y*unsigned(units::tile_height_q16));
        memory_.write(child+0x17c,height*48);memory_.write(child+0xf0,duration);memory_.write(object_+0xd4,handle);next(ins);return Yield::Continue;
    }
    if(ins.opcode==opcode::actor_hop_child){
        const auto destination=op(0);const bool indirect=destination.descriptor&operand_flag::indirect,blocking=ins.subop==1&&!indirect&&destination.payload==1;
        if(blocking&&memory_.read(object_+vm_offset::blocking_child)){
            if(alive(memory_.read(object_+vm_offset::blocking_child)))return Yield::Forced;
            memory_.write(object_+vm_offset::blocking_child,0);next(ins);return Yield::Continue;
        }
        const auto id=get(ins.subop?1:0),actor=lookup_actor(memory_,id);if(!actor)throw Fault(ins.pc,"hop child needs a materialized actor");
        const auto handle=Arena(memory_).clone_event(0x6ca07e,1),child=*resolve_compact(memory_,handle);
        memory_.write(child+vm_offset::actor_id,id);memory_.write(child+vm_offset::actor_object,actor);
        const auto height=get(ins.subop?2:1);auto duration=get(ins.subop?3:2);
        if(ins.subop){
            if(!duration)throw Fault(ins.pc,"hop child RNG modulus is zero");
            const auto state=memory_.random_state().next_sequence();duration=state%duration;if(duration<2)duration=1;
        }
        memory_.write(child+0xec,height);memory_.write(child+0xf0,duration);
        if(!ins.subop)memory_.write(object_+0xd4,handle);
        else if(indirect)destination.set(memory_,object_,handle);
        else if(!destination.payload)memory_.write(object_+vm_offset::async_child,handle);
        else if(blocking){memory_.write(object_+vm_offset::blocking_child,handle);return Yield::Continue;}
        else if(destination.payload!=0xffffffffu)throw Fault(ins.pc,"invalid hop child destination selector");
        next(ins);return Yield::Continue;
    }
    if (ins.opcode == opcode::object_field) {
        const auto sub = ins.subop;
        const bool writing = sub == 1 || sub == 3 || sub == 5 || sub == 7 || sub == 9 || sub == 16;
        const auto value = writing ? get(sub == 1 ? 1 : sub == 9 ? 3 : 2) : 0;
        Address address = get(0);
        unsigned size = 4;
        if (sub == 1) size = width(1);
        else if (sub == 2 || sub == 3) { size = width(0); address += size * get(1); }
        else if (sub == 4 || sub == 5) { size = width(1); address += get(1); }
        else if (sub == 6 || sub == 7) address += get(1);
        else if (sub == 8 || sub == 9) address += get(2) * get(1);
        else if (sub == 16) {
            const auto resolved = resolve_compact(memory_, address);
            if (!resolved) throw Fault(ins.pc, "typed field store targets stale compact handle");
            size = width(1); address = *resolved + get(1);
        }
        if (writing) memory_.write(address, value, size);
        else memory_.write(object_ + vm_offset::result, memory_.read(address, size));
        next(ins); return Yield::Continue;
    }
    if (ins.opcode == opcode::actor_sequence) {
        if (ins.subop >= 20) {
            if (ins.subop == 23) {
                if (const auto primary = resolve_compact(memory_, memory_.read(object_ + vm_offset::async_child)))
                    memory_.write(*primary + 0x18, (memory_.read(*primary + 0x18) & ~1u) | unsigned(alive(memory_.read(object_ + vm_offset::auxiliary_child))));
            } else {
                if (ins.subop == 21 || ins.subop == 22) {
                    const auto script = get(0), slot = object_ + (ins.subop == 21 ? 0xdc : 0xd0);
                    if (!script) throw Fault(ins.pc, "reaction child has null script");
                    if (memory_.read(slot)) env_.diagnostics.emplace_back(ins.pc, "reaction child slot already occupied");
                    const auto id = memory_.read(object_ + vm_offset::actor_id), actor = lookup_actor(memory_, id);
                    const auto handle = Arena(memory_).clone_event(get(0), 1), child = *resolve_compact(memory_, handle);
                    memory_.write(child + vm_offset::actor_id, id); memory_.write(child + vm_offset::actor_object, actor); memory_.write(slot, handle);
                }
                if (ins.subop != 22) if (const auto auxiliary = resolve_compact(memory_, memory_.read(object_ + vm_offset::cached_message_sender)))
                    memory_.write(*auxiliary + 0xdc, memory_.read(object_ + vm_offset::auxiliary_child));
            }
            next(ins); return Yield::Continue;
        }
        if (ins.subop>=8) {
            if (!env_.dialogue) throw Fault(ins.pc,"dialogue subsystem is not attached");
            const auto text=ins.subop<=9?get(1):0;
            const auto id=get(0),actor=lookup_actor(memory_,id);
            if (!actor) throw Fault(ins.pc,"actor sequence/dialog operation targets an absent actor");
            env_.dialogue->clear_sequence(actor+actor_offset::sequence_handle);
            if (ins.subop==10||ins.subop==11) {
                if (ins.subop==11) env_.dialogue->release_reference(actor+actor_offset::dialogue_handle);
            } else {
                const auto handle=Arena(memory_).allocate_after(memory_.read(globals::group1_append_link),routines::event_vm_tick,0x10230000,0);
                const auto child=*resolve_compact(memory_,handle);
                memory_.write(child+vm_offset::pc,ins.subop==9||!text?0x6ce342:text);
                memory_.write(child+vm_offset::actor_id,get(0));memory_.write(child+vm_offset::actor_object,actor);
                const auto channel=get(2);memory_.write(child+vm_offset::message_channel,channel);memory_.write(actor+actor_offset::sequence_handle,handle);
                if (ins.subop==9&&text) memory_.write(object_+0xd8,env_.dialogue->spawn_markup(memory_.read(child+vm_offset::actor_id),text,memory_.read(child+vm_offset::message_channel)));
            }
            next(ins);return Yield::Continue;
        }
        const auto slot = op(0);
        const bool indirect = (slot.descriptor & operand_flag::indirect) != 0;
        if (!indirect && slot.payload != 0 && slot.payload != 1 && slot.payload != 0xffffffffu)
            throw Fault(ins.pc, "invalid child result-slot selector");
        if (!indirect && slot.payload == 1 && memory_.read(object_ + vm_offset::blocking_child)) {
            if (alive(memory_.read(object_ + vm_offset::blocking_child))) return Yield::Forced;
            memory_.write(object_ + vm_offset::blocking_child, 0); next(ins); return Yield::Continue;
        }
        const auto script = get(2), actor_id = get(1);
        const auto frame_override = ins.subop == 4 ? get(3) : 0;
        Actors fallback(memory_);auto& actors=env_.actors?*env_.actors:fallback;
        // 0x430d1c passes selector 1. These actor-bound children run in EARLY group1.
        const auto handle = actors.attach_child(actor_id,script);
        const auto child = *resolve_compact(memory_, handle);
        if (ins.subop == 4) memory_.write(child + vm_offset::frame_bias, frame_override);
        if (indirect) slot.set(memory_, object_, handle);
        else if (slot.payload == 0) memory_.write(object_ + vm_offset::async_child, handle);
        else if (slot.payload == 1) {
            memory_.write(object_ + vm_offset::blocking_child, handle);
            return Yield::Continue; // Same PC: next dispatch polls the newly created child.
        }
        next(ins); return Yield::Continue;
    }
    if (ins.opcode == opcode::sprite_frame) {
        const auto selector = get(1) | (ins.subop == 3 ? 0x20000u : 0u);
        const auto actor = lookup_actor(memory_, get(0));
        if (!actor) throw Fault(ins.pc, "frame setter needs a materialized actor");
        memory_.write(actor + actor_offset::sprite_selector, selector); memory_.write(actor + actor_offset::sprite_frame, get(2));
        next(ins); return Yield::Continue;
    }
    auto wait = memory_.read(object_ + vm_offset::wait_count);
    if (!wait) {
        wait = memory_.read(object_ + vm_offset::frame_bias) + memory_.read(globals::animation_frame_bias) + get(0);
        memory_.write(object_ + vm_offset::wait_count, wait);
        const auto actor = memory_.read(object_ + vm_offset::actor_object);
        if (!actor) throw Fault(ins.pc, "timed frame setter needs bound current actor");
        const auto family = ins.subop == 4 ? 0 : ins.subop;
        memory_.write(actor + actor_offset::sprite_selector, get(1) | (std::uint32_t(family) << 16));
        memory_.write(actor + actor_offset::sprite_frame, get(2));
        if (signed32(wait) < 1) { memory_.write(object_ + vm_offset::wait_count, 0); next(ins); return Yield::Continue; }
    }
    if (env_.input_pause || env_.dialog_busy) wait = 1;
    memory_.write(object_ + vm_offset::wait_count, --wait);
    if (!wait) next(ins);
    return Yield::Normal;
}
} // namespace fsb::core
