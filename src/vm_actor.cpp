#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/dialogue.hpp"

namespace fsb::core {
Yield Vm::actor_command(const Instruction& ins) {
    Actors actor_fallback(memory_);auto& actors=env_.actors?*env_.actors:actor_fallback;
    const auto get = [&](unsigned i) { return ins.operand(memory_, i).get(memory_, object_); };
    const auto resolve = [&](std::uint32_t selector) { return selector == 0xffffffffu ? memory_.read(object_ + vm_offset::actor_object) : lookup_actor(memory_, selector); };
    if(ins.opcode==opcode::query_step||ins.opcode==opcode::apply_step_height){
        const auto actor=memory_.read(object_+vm_offset::actor_object);if(!actor)throw Fault(ins.pc,"step attributes need a bound actor");
        if(ins.opcode==opcode::query_step)memory_.write(object_+vm_offset::cached_step_flags,actors.step_attribute(actor,ins.subop?get(0):memory_.read(actor+actor_offset::target_facing))^1u);
        else{const auto flags=memory_.read(object_+vm_offset::cached_step_flags);if(!(flags&1)&&(flags&12)){
            if((flags&12)==12)throw Fault(ins.pc,"conflicting cached step height flags");
            memory_.write(actor+actor_offset::layer_q16,memory_.read(actor+actor_offset::layer_q16)+((flags&4)?0x10000u:0xffff0000u));
        }}
    }else if(ins.opcode==opcode::timed_displacement){
        auto wait=memory_.read(object_+vm_offset::wait_count);
        if(!wait){
            wait=get(0);if(signed32(wait)>0)wait+=memory_.read(object_+vm_offset::frame_bias)+memory_.read(globals::animation_frame_bias);memory_.write(object_+vm_offset::wait_count,wait);
            const auto actor=memory_.read(object_+vm_offset::actor_object);if(!actor)throw Fault(ins.pc,"timed movement needs a bound actor");
            if(ins.subop==0){const auto dx=get(1),dy=get(2);memory_.write(actor+actor_offset::draw_offset_x,memory_.read(actor+actor_offset::draw_offset_x)+dx*units::q16_one);memory_.write(actor+actor_offset::draw_offset_y,memory_.read(actor+actor_offset::draw_offset_y)+dy*units::q16_one);}
            else if(ins.subop!=1||!(memory_.read(object_+vm_offset::cached_step_flags,1)&1)){
                const auto selected=ins.subop==8?lookup_actor(memory_,get(0)):actor;
                const auto dy=get(ins.subop==8?3:2),dx=get(ins.subop==8?2:1),scale=ins.subop==2?1u:65536u;
                set_actor_raw_position(memory_,selected,memory_.read(selected+actor_offset::world_x)+dx*scale,memory_.read(selected+actor_offset::world_y)+dy*scale);
            }
            if(signed32(wait)<1||env_.input_pause||env_.dialog_busy){memory_.write(object_+vm_offset::wait_count,0);next(ins);return Yield::Continue;}
        }
        memory_.write(object_+vm_offset::wait_count,--wait);if(!wait)next(ins);return Yield::Normal;
    }else if(ins.opcode==opcode::walk_actor||ins.opcode==opcode::follow_path){
        const auto result=ins.opcode==opcode::follow_path?ins.operand(memory_,0):Operand{};
        const bool indirect=ins.opcode==opcode::follow_path&&(result.descriptor&operand_flag::indirect),blocking=ins.opcode==opcode::walk_actor?(ins.subop&1):(!indirect&&result.payload==1);
        if(blocking&&memory_.read(object_+vm_offset::blocking_child)){if(alive(memory_.read(object_+vm_offset::blocking_child)))return Yield::Forced;memory_.write(object_+vm_offset::blocking_child,0);next(ins);return Yield::Continue;}
        Handle child_handle;
        if(ins.opcode==opcode::walk_actor){
            if(!(ins.subop&2)){const auto count=get(2),direction=get(1),id=get(0);child_handle=actors.start_walk(id,direction,count,ins.subop>=4);}
            else{
                const auto id=get(0),actor=lookup_actor(memory_,id);if(!actor)throw Fault(ins.pc,"walk pose needs actor");
                child_handle=Arena(memory_).clone_event(ins.subop>=4?scripts::alternate_walk_pose:scripts::walk_pose,1);const auto child=*resolve_compact(memory_,child_handle);
                memory_.write(child+vm_offset::actor_id,id);memory_.write(child+vm_offset::actor_object,actor);memory_.write(actor+actor_offset::target_facing,get(1));memory_.write(child+0xe8,get(2));
            }
        }else{
            if(!indirect&&result.payload!=0&&result.payload!=1&&result.payload!=0xffffffffu)throw Fault(ins.pc,"invalid path result slot");
            const auto id=get(1),actor=lookup_actor(memory_,id);if(!actor)throw Fault(ins.pc,"path needs actor");
            if(ins.subop==10){
                const auto player=lookup_actor(memory_,actors.player_id());
                const auto x=sequence_alu(alu::arithmetic_shift_right,memory_.read(player+actor_offset::tile_x_q16),16),y=sequence_alu(alu::arithmetic_shift_right,memory_.read(player+actor_offset::tile_y_q16),16);
                child_handle=actors.follow_path(id,signed32(x),signed32(y),get(2),0,false,false,false);
            }else{
            auto x=get(2),y=get(3);
            if(ins.subop>=4){x+=std::uint32_t(signed32(memory_.read(actor+actor_offset::world_x))/units::tile_width_q16);y+=std::uint32_t(signed32(memory_.read(actor+actor_offset::world_y))/units::tile_height_q16);}
            const auto facing=get(4);const bool extra=(ins.subop&2)!=0;
            //4226da: the fixed-flag subops 0/1/4/5 all pass flag10=flag20=1 and
            //wait_mode=1. Only the subop parity picks the follow-up variant, so
            //the alternate one must not seed the child's hide-on-arrival at19c.
            const bool party=extra?get(5)!=0:true,objects=extra?get(6)!=0:true,wait_mode=extra?get(7)!=0:true;
            child_handle=actors.follow_path(id,signed32(x),signed32(y),facing,ins.subop&1,party,objects,wait_mode);
            }
        }
        if(blocking){memory_.write(object_+vm_offset::blocking_child,child_handle);return Yield::Continue;}
        if(indirect)result.set(memory_,object_,child_handle);
        else if(ins.opcode==opcode::walk_actor||result.payload==0)memory_.write(object_+vm_offset::async_child,child_handle);
    }else if(ins.opcode==opcode::read_path_command||ins.opcode==opcode::next_path_command){
        const auto actor=memory_.read(object_+vm_offset::actor_object);if(!actor)throw Fault(ins.pc,"path cursor needs a bound actor");
        const auto cursor=memory_.read(actor+actor_offset::path_cursor);
        if(ins.opcode==opcode::next_path_command)memory_.write(actor+actor_offset::path_cursor,cursor+1);
        else{
            std::uint32_t value=0;
            if(signed32(cursor)<signed32(memory_.read(actor+actor_offset::path_count))){if(cursor>=100)throw Fault(ins.pc,"path cursor outside actor buffer");value=memory_.read(actor+actor_offset::path_commands+cursor*2,2);}
            else memory_.write(actor+actor_offset::path_count,0);
            ins.operand(memory_,0).set(memory_,object_,value);
        }
    }else if(ins.opcode==opcode::bind_actor){
        if(ins.subop==0)memory_.write(object_+vm_offset::actor_object,lookup_actor(memory_,memory_.read(object_+vm_offset::actor_id)));
        else memory_.write(object_+vm_offset::result,lookup_actor(memory_,get(0)));
    }else if(ins.opcode==opcode::actor_on_screen){
        const auto actor=lookup_actor(memory_,get(0));if(!actor)throw Fault(ins.pc,"screen query needs actor");
        const int x=signed32((memory_.read(actor+actor_offset::world_x)>>16)+memory_.read(globals::viewport_width)/2-memory_.read(globals::camera_x)+memory_.read(globals::viewport_left));
        const int y=signed32((memory_.read(actor+actor_offset::world_y)>>16)+memory_.read(globals::viewport_height)/2-memory_.read(globals::camera_y)+memory_.read(globals::viewport_top));
        memory_.write(object_+vm_offset::result,x>=signed32(memory_.read(globals::viewport_left))-128&&x<signed32(memory_.read(globals::viewport_right))+128&&y>=signed32(memory_.read(globals::viewport_top))-144&&y<signed32(memory_.read(globals::viewport_bottom))+144);
    }else if(ins.opcode==opcode::release_actor){
        const auto actor=lookup_actor(memory_,get(0));
        if(actor){
            if(!env_.dialogue)throw Fault(ins.pc,"actor release needs its sequence/dialogue subsystem");
            env_.dialogue->clear_sequence(actor+actor_offset::sequence_handle);env_.dialogue->release_reference(actor+actor_offset::dialogue_handle);actors.finalize(actor);
        }
        //43033d leaves the caller's selector untouched, unlike58/1.
    }else if (ins.opcode == opcode::allocate_actor) {
      if(ins.subop>=5){
        const auto destination=lookup_actor(memory_,get(0)),source=lookup_actor(memory_,get(1));
        if(!destination||!source)throw Fault(ins.pc,"actor pose copy needs both actors");
        if(destination!=source)for(auto offset:{8,12,16,32,36,40,20,24,28,0x128,0x12c,0x110})memory_.write(destination+offset,memory_.read(source+offset));
        if(ins.subop==6){
            const auto facing=get(2);actors.visible(destination,true);
            if(facing!=0xffffffffu){memory_.write(destination+actor_offset::facing,facing);actors.clear_frame(destination);}
        }
      }else{
        const auto source=ins.subop==4?lookup_actor(memory_,get(1)):0;
        if(ins.subop==4&&!source)throw Fault(ins.pc,"actor clone has no source actor");
        const auto actor=actors.allocate_extra_slot();if(!actor)throw Fault(ins.pc,"extra actor slots exhausted");
        memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)&~8u);
        if(ins.subop==0)memory_.write(actor+actor_offset::layer_q16,0x8000);
        else for(auto offset:{0x130,0x110,0x114,8,12,16,32,36,40,20,24,28,0x128,0x12c})memory_.write(actor+offset,memory_.read(source+offset));
        ins.operand(memory_,0).set(memory_,object_,actor);
      }
    } else if (ins.opcode == opcode::reset_actor_frame) {
        actors.clear_frame(ins.subop == 0 ? memory_.read(object_ + vm_offset::actor_object) : lookup_actor(memory_, get(0)));
    } else if (ins.opcode == opcode::tween_actor) {
        const auto id = get(1); const auto result = ins.operand(memory_, 0);
        const bool indirect = (result.descriptor & operand_flag::indirect) != 0, blocking = !indirect && result.payload == 1;
        if (!indirect && result.payload != 0 && result.payload != 1 && result.payload != 0xffffffffu) throw Fault(ins.pc, "invalid tween result slot");
        if (blocking && memory_.read(object_ + vm_offset::blocking_child)) {
            if (alive(memory_.read(object_ + vm_offset::blocking_child))) return Yield::Forced;
            memory_.write(object_ + vm_offset::blocking_child, 0); next(ins); return Yield::Continue;
        }
        const auto actor = lookup_actor(memory_, id);
        if (!actor) throw Fault(ins.pc, "tween needs a materialized actor");
        Handle handle;
        if(ins.subop==6){
            const auto player=lookup_actor(memory_,actors.player_id());const auto tile=[&](Address object,unsigned offset){return sequence_alu(alu::arithmetic_shift_right,memory_.read(object+offset),16);};
            const auto dx=tile(player,actor_offset::tile_x_q16)-tile(actor,actor_offset::tile_x_q16),dy=tile(player,actor_offset::tile_y_q16)-tile(actor,actor_offset::tile_y_q16);
            const auto arc_y=get(4),arc_x=get(3),duration=get(2);handle=actors.tween_tiles(id,dx,dy,duration,arc_x,arc_y,true,env_.input_pause||env_.dialog_busy);
        }else if (ins.subop <= 1) {
            auto x = get(2), y = get(3);
            if (ins.subop == 0) {
                x -= sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::tile_x_q16), 16);
                y -= sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::tile_y_q16), 16);
            }
            const auto arc_y = get(6), arc_x = get(5), duration = get(4);
            handle = actors.tween_tiles(id, x, y, duration, arc_x, arc_y, false, env_.input_pause || env_.dialog_busy);
        } else {
            auto x = get(2), y = get(3);
            if (ins.subop == 8) { x = x * 64u + 32u; y = y * 48u + 24u; }
            else { x += sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::world_x), 16); y += sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::world_y), 16); }
            const auto duration = get(4);
            handle = actors.tween_raw(get(1), x, y, duration, env_.input_pause || env_.dialog_busy);
        }
        if (indirect) result.set(memory_, object_, handle);
        else if (result.payload == 0) memory_.write(object_ + vm_offset::async_child, handle);
        else if (blocking) { memory_.write(object_ + vm_offset::blocking_child, handle); if (handle) return Yield::Continue; }
    } else if (ins.opcode == opcode::turn_actor) {
        if (ins.subop == 0 && memory_.read(object_ + vm_offset::blocking_child)) {
            if (alive(memory_.read(object_ + vm_offset::blocking_child))) return Yield::Forced;
            memory_.write(object_ + vm_offset::blocking_child, 0);
        } else {
            const auto extra = get(2), direction = get(1), id = get(0);
            const auto handle = actors.start_turn(id, direction, extra);
            memory_.write(object_ + (ins.subop == 0 ? 0xcc : 0xd0), handle);
            if (ins.subop == 0) return Yield::Continue; // Including invalid direction's original same-PC retry.
        }
    } else if (ins.opcode == opcode::current_actor_direction) {
        const auto direction = get(ins.subop == 2 ? 1 : 0);
        const auto actor = ins.subop == 2 ? lookup_actor(memory_, get(0)) : memory_.read(object_ + vm_offset::actor_object);
        if (!actor) throw Fault(ins.pc, "direction setter needs a materialized actor");
        if (ins.subop != 1) memory_.write(actor + actor_offset::facing, direction);
        memory_.write(actor + actor_offset::target_facing, direction); // 0x430197/0x4301b9 do not clear sprite frames.
    } else if (ins.opcode == opcode::party) {
        switch (ins.subop) {
        case 0: actors.replace_with_party_actor(get(0));break;
        case 1: ins.operand(memory_, 0).set(memory_, object_, actors.player_id()); break;
        case 2: actors.swap_player(get(0)); break;
        case 3: memory_.write(object_ + vm_offset::result, actors.in_party(get(0))); break;
        case 4: actors.reset_party(); break;
        case 8: case 9: actors.set_party_mask(get(0), ins.subop == 8); break;
        case 10:if(!env_.dialogue)throw Fault(ins.pc,"event cleanup needs dialogue subsystem");actors.cleanup_event(*env_.dialogue);break;
        case 16: actors.hide_party(); break;
        case 20: memory_.write(globals::party_panel_visible_mask,memory_.read(globals::party_panel_visible_mask)|(1u<<(get(0)&31)));break;
        }
    } else if (ins.opcode == opcode::place_actor) {
        const auto id = get(0), z = get(1), x = get(2), y = get(3), direction = get(4);
        const auto actor = lookup_actor(memory_, id);
        if (!actor) throw Fault(ins.pc, "actor placement requires a materialized actor");
        set_actor_tile_position(memory_, actor, signed32(x), signed32(y), signed32(z));
        // Wrapper takes signed high words rather than copying the input integers.
        memory_.write(actor + actor_offset::tile_x, sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::tile_x_q16), 16));
        memory_.write(actor + actor_offset::tile_y, sequence_alu(alu::arithmetic_shift_right, memory_.read(actor + actor_offset::tile_y_q16), 16));
        if (direction != 0xffffffffu) { memory_.write(actor + actor_offset::facing, direction); memory_.write(actor + actor_offset::target_facing, direction); actors.clear_frame(actor); }
        if (ins.subop == 1) actors.visible(lookup_actor(memory_, get(0)), true);
    } else if (ins.opcode == opcode::effect_lifecycle) {
        if(ins.subop==1){
            const auto actor=get(0);if(!actor)throw Fault(ins.pc,"effect deletion has null object");
            if(!env_.dialogue)throw Fault(ins.pc,"effect reference cleanup needs dialogue/sequence subsystem");
            env_.dialogue->clear_sequence(actor+actor_offset::sequence_handle);env_.dialogue->release_reference(actor+actor_offset::dialogue_handle);actors.finalize(actor);
            ins.operand(memory_,0).set(memory_,object_,0);
        }else{
            const auto effect = get(1), direction = get(5), z = get(2), y = get(4), x = get(3);
            const auto actor = actors.spawn_effect(signed32(x), signed32(y), signed32(z), direction, effect);
            actors.visible(actor, true); actors.clear_frame(actor); ins.operand(memory_, 0).set(memory_, object_, actor);
        }
    } else if (ins.opcode == opcode::actor_vector) {
        const auto selector = get(0), flags = get(1), x = get(2), y = get(3), z = get(4);
        const auto actor = resolve(selector);
        if (!actor || !(flags & 15) || ((flags & 0xf00) != 0x100 && (flags & 0xf00) != 0x200)) throw Fault(ins.pc, "invalid actor vector command");
        const auto base = actor + ((flags & 0xf00) == 0x100 ? 8 : 32);
        if (flags & 1) memory_.write(base, x);
        if (flags & 2) memory_.write(base + 4, y);
        if (flags & 4) memory_.write(base + 8, z);
    } else {
        const auto id = get(0), actor = resolve(id);
        if (actor) switch (ins.subop) {
        case 0: case 1: actors.visible(actor, ins.subop == 1); break;
        case 2: {
            actors.visible(actor, true); const auto value = get(1);
            if (value != 0xffffffffu) { memory_.write(actor + actor_offset::facing, value); actors.clear_frame(actor); }
            break;
        }
        case 4: case 5: set_actor_tile_state(memory_, actor, ins.subop == 5); break;
        case 8: memory_.write(object_ + vm_offset::result, memory_.read(lookup_actor(memory_, get(0)) + 0x130)); break;
        case 9: { const auto target = lookup_actor(memory_, get(0)); memory_.write(target + 0x130, get(1)); break; }
        case 10: { const auto target = lookup_actor(memory_, get(0)); memory_.write(object_ + vm_offset::result, target ? (memory_.read(target + 4) >> 6) & 1 : 0); break; }
        case 20: memory_.write(actor + actor_offset::flags, memory_.read(actor + actor_offset::flags) & ~8u); break;
        case 21: memory_.write(actor + actor_offset::flags, memory_.read(actor + actor_offset::flags) | 8u); break;
        }
    }
    next(ins); return Yield::Continue;
}
Yield Vm::vector_command(const Instruction& ins) {
    const auto get = [&](unsigned i) { return ins.operand(memory_, i).get(memory_, object_); };
    if (ins.opcode == opcode::copy_vector) {
        const auto selector = get(0), flags = get(1);
        const auto actor = selector == 0xffffffffu ? memory_.read(object_ + vm_offset::actor_object) : lookup_actor(memory_, selector);
        if (!actor) throw Fault(ins.pc, "vector copy needs a materialized actor");
        Address sequence;
        switch (flags & 0xf0) {
        case 0x10: sequence = object_ + 0x11c; break;
        case 0x20: sequence = object_ + 0x140; break;
        case 0x40: sequence = object_ + 0x134; break;
        case 0x80: sequence = object_ + 0x128; break;
        default: throw Fault(ins.pc, "invalid sequence vector selector");
        }
        if ((flags & 0xf00) != 0x100 && (flags & 0xf00) != 0x200) throw Fault(ins.pc, "invalid actor vector selector");
        const auto actor_vector = actor + ((flags & 0xf00) == 0x100 ? 8 : 32);
        const auto from = ins.subop == 0 ? sequence : actor_vector, to = ins.subop == 0 ? actor_vector : sequence;
        for (unsigned i = 0; i < 3; ++i) if (flags & (1u << i)) memory_.write(to + i * 4, memory_.read(from + i * 4));
    } else {
        const auto flags = get(0);
        for (unsigned i = 0; i < 3; ++i) if (flags & (1u << i)) {
            const auto axis = i * 4;
            auto current = memory_.read(object_ + 0x11c + axis);
            if (ins.subop == 0) {
                auto step = memory_.read(object_ + 0x128 + axis);
                if (signed32(memory_.read(object_ + 0x140 + axis)) <= signed32(current)) step = 0u - step;
                const auto accumulator = memory_.read(object_ + 0x134 + axis) + step;
                memory_.write(object_ + 0x134 + axis, accumulator); current += accumulator;
            } else {
                const auto phase = memory_.read(object_ + 0x14c + axis);
                const auto offset = (i == 0 ? fixed_sin(memory_, phase) : fixed_cos(memory_, phase)) * memory_.read(object_ + 0x158 + axis);
                if (flags & 8) current = memory_.read(object_ + 0x140 + axis);
                current += i == 1 ? 0u - offset : offset;
            }
            memory_.write(object_ + 0x11c + axis, current);
        }
    }
    next(ins); return Yield::Continue;
}
} // namespace fsb::core
