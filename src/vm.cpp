#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/viewport.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/battle.hpp"
#include "fsb_core/sprites.hpp"
#include "fsb_core/arena.hpp"
#include "fsb_core/transition.hpp"
#include <sstream>

namespace fsb::core {
bool Vm::supported(std::uint8_t op, std::uint8_t sub) {
    if (op == opcode::end) return sub <= 2;
    if(op==opcode::stage_actor||op==opcode::restore_actor_stage)return sub<=1;
    if(op==opcode::refresh_actor_position)return sub==1;
    if(op==opcode::break_block)return sub<=1;
    if(op==0x98)return sub==0||sub==1||sub==4||sub==5;
    if(op==0xc0)return sub==0||sub==3||sub==4;
    if(op==0x99)return sub<=1;
    if(op==0xe4)return sub<=2;
    if(op==0x78||op==0x38)return sub==0;
    if(op==0x31)return sub<=3;
    if(op==0x48)return sub==0;
    if(op==0xea)return sub<=1;
    if(op==opcode::block_count||op==opcode::frame_bias||op==opcode::actor_hop_child||op==opcode::actor_arc_child)return sub<=1;
    if (op == opcode::yield) return sub <= 1;
    if (op == opcode::wait) return sub <= 5 || sub == 8;
    if (op == opcode::assign) return true; // Original handler ignores the subop byte.
    if (op == opcode::compact_sprite || op == opcode::compact_sprite_offset) return true; // Both original sprite handlers ignore subop.
    if (op == opcode::jump || op == opcode::call_table || op == opcode::call || op == opcode::repeat_begin || op == opcode::repeat_end || op == opcode::while_end || op == opcode::if_end) return true;
    if (op == opcode::conditional_jump) return sub<=1;
    if (op == opcode::repeat_call || op == opcode::read_path_command || op == opcode::next_path_command || op == opcode::actor_on_screen) return sub==0;
    if (op == opcode::bind_actor) return sub<=1;
    if (op == opcode::timed_displacement) return sub<=2||sub==8;
    if (op == opcode::query_step) return sub<=1;
    if (op == opcode::apply_step_height) return sub==0;
    if (op == opcode::walk_actor) return sub<=7;
    if (op == opcode::follow_path) return sub<=7||sub==10;
    if (op == opcode::branch_table || op == opcode::if_begin) return sub <= 2;
    if (op == opcode::while_begin || op == opcode::random || op == opcode::progress_counter) return sub <= 1;
    if (op == opcode::native_callback) return sub <= 1;
    if (op == opcode::else_branch || op == opcode::unary_alu) return sub <= 3;
    if (op == opcode::binary_alu) return sub <= 4 || sub == 8 || sub == 9;
    if (op == opcode::standby_object || op == opcode::release_object) return sub == 0;
    if (op == opcode::dialogue_spawn_or_wait) return sub == 0 || sub == 4;
    if (op == opcode::dialogue_control) return sub <= 6 || sub == 8 || sub == 9 || sub == 10 || sub == 12;
    if (op == opcode::object_field) return sub <= 9 || sub == 0x10;
    if (op == opcode::actor_vector) return sub == 0;
    if (op == opcode::copy_vector || op == opcode::advance_vector) return sub <= 1;
    if (op == opcode::evaluate_alu || op == opcode::current_actor_direction) return sub <= 2;
    if (op == opcode::place_actor || op == opcode::turn_actor || op == opcode::reset_actor_frame) return sub <= 1;
    if (op == opcode::tween_actor) return sub <= 1 || sub == 6 || sub == 8 || sub == 9;
    if (op == opcode::actor_visibility) return sub <= 2 || sub == 4 || sub == 5 || sub == 8 || sub == 9 || sub == 10 || sub == 20 || sub == 21;
    if (op == opcode::effect_lifecycle) return sub == 1 || sub == 2;
    if (op == opcode::release_actor) return true; // Original release handler ignores subop.
    if (op == opcode::allocate_actor) return sub == 0 || sub == 4 || sub == 5 || sub == 6;
    if (op == opcode::event_count) return sub <= 2;
    if (op == opcode::party) return sub <= 4 || sub == 8 || sub == 9 || sub == 10 || sub == 16 || sub == 20;
    if (op == opcode::worldmap_slot) return sub<=2||sub==4||sub==8||sub==10||sub==11;
    if (op == opcode::actor_sequence) return sub == 0 || sub == 4 || sub == 8 || sub == 9 || sub == 10 || sub == 11 || (sub >= 20 && sub <= 23);
    if (op == opcode::timed_sprite_frame) return sub <= 4;
    if (op == opcode::sprite_frame) return sub <= 1 || sub == 3;
    if (op == opcode::camera) return sub <= 2;
    if (op == opcode::bgm || op == opcode::audio_control) return sub <= 4;
    if (op == opcode::sound_cue) return sub <= 2;
    if (op == opcode::event_lifecycle) return sub <= 5;
    if (op == opcode::viewport) return sub <= 7;
    if (op == opcode::map_patch_or_special) return sub == 0||sub == 4; // Special selectors still checked at dispatch.
    if (op == opcode::load_map) return sub == 0;
    if (op == opcode::current_map) return sub <= 2;
    if (op == opcode::wait_transition) return sub <= 2;
    if (op == opcode::rectangle_effect) return sub<=4;
    if (op == opcode::palette_buffer) return sub <= 5 || sub == 8 || sub == 10;
    if (op == opcode::palette_fade) return sub <= 5 || sub == 0x10 || sub == 0x11 || sub == 0x20 || sub == 0x21;
    if (op == opcode::message) return sub <= 5 || sub == 7 || sub == 8 || sub == 10;
    return false;
}
Message Vm::cache() const {
    return {memory_.read(object_ + vm_offset::cached_message_target), memory_.read(object_ + vm_offset::cached_message_channel),
            memory_.read(object_ + vm_offset::cached_message_code), memory_.read(object_ + vm_offset::cached_message_sender)};
}
void Vm::cache(Message m) {
    memory_.write(object_ + vm_offset::cached_message_target, m.target); memory_.write(object_ + vm_offset::cached_message_channel, m.channel);
    memory_.write(object_ + vm_offset::cached_message_code, m.code); memory_.write(object_ + vm_offset::cached_message_sender, m.sender);
}
Yield Vm::wait(const Instruction& ins) {
    const auto operand = [&]() { return ins.operand(memory_, 0); };
    if (ins.subop == 0) {
        auto counter = memory_.read(object_ + vm_offset::wait_count);
        if (!counter) counter = operand().get(memory_, object_);
        if (!counter) { next(ins); return Yield::Continue; }
        if (env_.input_pause || env_.dialog_busy) counter = 1;
        memory_.write(object_ + vm_offset::wait_count, --counter);
        if (!counter) next(ins);
        return Yield::Normal;
    }
    if (ins.subop == 1) {
        if (!operand().get(memory_, object_)) return Yield::Forced;
    } else if (ins.subop == 2) {
        if (!env_.key_down || env_.input_high_blocked) return Yield::Forced;
    } else if (ins.subop == 3) {
        if (alive(operand().get(memory_, object_))) return Yield::Forced;
        operand().set(memory_, object_, 0);
    } else if (ins.subop == 4) {
        const auto parent = resolve_compact(memory_, operand().get(memory_, object_));
        if (parent && alive(memory_.read(*parent + 0xd0))) return Yield::Forced;
    } else if (ins.subop == 5) {
        if (memory_.read(object_ + vm_offset::same_pc_count) == 0 || alive(memory_.read(object_ + vm_offset::auxiliary_child))) return Yield::Forced;
        memory_.write(object_ + vm_offset::auxiliary_child, 0);
    } else if (ins.subop == 8) {
        const auto slot=operand().get(memory_,object_);if(slot>=64)throw Fault(ins.pc,"IFC wait slot outside original table");
        const auto parent=0x768ac0+slot*36;
        if(alive(memory_.read(parent)))return Yield::Forced;
        memory_.write(parent,0); // 0x430cc3 also clears stale handles.
    }
    next(ins); return Yield::Continue;
}

Yield Vm::hsm(const Instruction& ins) {
    const auto get = [&](unsigned n) { return ins.operand(memory_, n).get(memory_, object_); };
    if (ins.subop <= 2) {
        const auto target = get(0), channel = get(1), code = ins.subop == 1 ? 0 : get(2);
        std::optional<unsigned> matched, start;
        for (unsigned tries = 0; tries < queue_.size(); ++tries) {
            auto slot = queue_.find(target, channel, start);
            if (!slot) break;
            if (queue_.at(*slot).code == code) { matched = slot; break; }
            start = *slot + 1;
            if ((*start & 31u) == queue_.head()) break;
        }
        if (!matched && ins.subop != 2) return Yield::Forced;
        if (matched) {
            const auto received = queue_.at(*matched);
            queue_.remove(*matched);
            if (ins.subop != 2) {
                auto c = cache(); c.target = target;
                c.channel = ins.subop == 1 ? received.channel : channel;
                c.code = code; cache(c);
            }
        }
        if (ins.subop == 2) memory_.write(object_ + vm_offset::result, matched ? 1 : 0);
    } else if (ins.subop == 3) {
        if (const auto primary=resolve_compact(memory_,memory_.read(object_+vm_offset::async_child))) {
            const auto flags=memory_.read(*primary+0x18);
            memory_.write(*primary+0x18,(flags&~1u)|unsigned(alive(memory_.read(object_+vm_offset::auxiliary_child))));
        }
        const auto matched = queue_.find_flagged(get(0));
        if (!matched) return Yield::Forced;
        const auto received = queue_.at(*matched); queue_.remove(*matched);
        cache(received); memory_.write(object_ + vm_offset::result, received.code & 0xffff7fffu);
    } else if (ins.subop == 4 || ins.subop == 5) {
        const auto channel = get(0), target = get(1), code = ins.subop == 4 ? get(2) : 0;
        queue_.enqueue({target, channel, code, compact_handle(memory_, object_)});
    } else if (ins.subop == 7) {
        queue_.enqueue({get(0), 0xffffffffu, get(1) | 0x8000u, compact_handle(memory_, object_)});
    } else if (ins.subop == 8) {
        const auto c = cache();
        queue_.enqueue({c.channel, c.target, c.code, compact_handle(memory_, object_)});
    } else if (ins.subop == 10) queue_.clear();
    next(ins); return Yield::Continue;
}

Yield Vm::original_command(const Instruction& ins) {
    const auto entry=memory_.read(0x5aa410+unsigned(ins.opcode)*4);
    if(env_.recovered&&RecoveredBattle::has_entry(entry)){
        // 41a042 clears the yield signal before the handler. Original
        // handlers own PC, operand side effects and child lifetimes.
        memory_.write(0x768a8c,0);env_.recovered->callback(entry,{object_});
        const auto signal=memory_.read(0x768a8c);
        if(signal>2)throw Fault(ins.pc,"invalid original VM yield signal");
        return static_cast<Yield>(signal);
    }
    std::ostringstream message;
    message << "unsupported opcode " << std::hex << unsigned(ins.opcode) << '/' << unsigned(ins.subop)
            << " at VM object 0x" << object_;
    throw Fault(ins.pc, message.str());
}

Yield Vm::step() {
    const auto ins = Instruction::decode(memory_, pc());
    if (!supported(ins.opcode, ins.subop)) return original_command(ins);
    if (ins.opcode == opcode::message) return hsm(ins);
    if(ins.opcode==0xea){
        const auto id=ins.operand(memory_,0).get(memory_,object_);if(id>=0x29c)throw Fault(ins.pc,"profile flag ID outside original table");
        const auto at=0x5b356f+id*68;memory_.write(at,ins.subop?memory_.read(at,1)|0x80u:memory_.read(at,1)&0x7fu,1);next(ins);return Yield::Continue;
    }
    if(ins.opcode==0x48){
        const auto actor=lookup_actor(memory_,ins.operand(memory_,0).get(memory_,object_));if(!actor)throw Fault(ins.pc,"position query needs a materialized actor");
        const std::uint32_t values[]={sequence_alu(alu::arithmetic_shift_right,memory_.read(actor+0x14),16),sequence_alu(alu::arithmetic_shift_right,memory_.read(actor+0x18),16),sequence_alu(alu::arithmetic_shift_right,memory_.read(actor+0x1c),16)};
        for(unsigned i=0;i<3;++i)ins.operand(memory_,i+1).set(memory_,object_,values[i]);next(ins);return Yield::Continue;
    }
    if(ins.opcode==0x31){
        //42093c writes camera-world viewport bounds/size/center, not screen
        //pixel mouse coordinates. The half-size operations truncate signed.
        for(unsigned i=0;i<2;++i){const auto size=memory_.read(i?globals::viewport_height:globals::viewport_width),center=memory_.read(i?globals::camera_y:globals::camera_x);std::uint32_t value;
            if(ins.subop==2)value=size;else if(ins.subop==3)value=center;else value=center-std::uint32_t(signed32(size)/2)+(ins.subop?size:0);
            ins.operand(memory_,i).set(memory_,object_,value);}
        next(ins);return Yield::Continue;
    }
    if(ins.opcode==0x78){
        if(!env_.post_message)throw Fault(ins.pc,"sequence frame-message queue is not attached");const auto flags=ins.operand(memory_,1).get(memory_,object_),key=ins.operand(memory_,0).get(memory_,object_);env_.post_message(0x406,key,flags);next(ins);return Yield::Continue;
    }
    if(ins.opcode==0xe4){
        const auto id=ins.operand(memory_,0).get(memory_,object_);if(id>=362)throw Fault(ins.pc,"inventory item outside original table");const auto at=0x806e30+id*4;
        if(ins.subop==2)memory_.write(object_+vm_offset::result,memory_.read(at));
        else{const auto value=ins.operand(memory_,1).get(memory_,object_);memory_.write(at,ins.subop?memory_.read(at)+value:value);
            if(!ins.subop&&id==230&&!value){memory_.write(0x8071c8,0);for(auto field:{0x607cbcu,0x607cc0u})if(memory_.read(field)==230)memory_.write(field,0xffffffffu);memory_.write(0x607cf4,0xffffffffu);}}
        next(ins);return Yield::Continue;
    }
    if(ins.opcode==0x38){
        const auto handle=Arena(memory_).allocate_after(memory_.read(globals::group0_append_link),0x420c84,0,0),child=*resolve_compact(memory_,handle);
        for(unsigned i=0;i<2;++i){const auto value=ins.operand(memory_,i).get(memory_,object_);memory_.write(child+0x168+i*4,signed32(value)<1?1:value);}
        memory_.write(object_+vm_offset::async_child,handle);next(ins);return Yield::Continue;
    }
    if(ins.opcode==0x99){auto map=ins.operand(memory_,0).get(memory_,object_);if(map==0xffffffffu)map=memory_.read(globals::current_map_id);memory_.write(0x5bf4d8+map*4,ins.subop?0:0xffffffffu);next(ins);return Yield::Continue;}
    if(ins.opcode==0x98){
        if(!env_.battle)throw Fault(ins.pc,"battle service is not connected");
        if(!ins.subop)env_.battle->reset_snapshot();
        else if(ins.subop==1){const auto actor=env_.battle->snapshot_actor(ins.operand(memory_,0).get(memory_,object_));memory_.write(actor+0x19c,ins.operand(memory_,1).get(memory_,object_));}
        else if(ins.subop==5)env_.battle->recovered.invoke(0x44e085);
        else{memory_.write(globals::phase_interval_ms,25);env_.battle->trigger_scripted(ins.operand(memory_,0).get(memory_,object_));memory_.write(0x768a98,1);}
        next(ins);return Yield::Continue;
    }
    if(ins.opcode==opcode::stage_actor||ins.opcode==opcode::restore_actor_stage)return ifc_command(ins);
    if(ins.opcode==opcode::refresh_actor_position){
        const auto actor=lookup_actor(memory_,ins.operand(memory_,0).get(memory_,object_));if(!actor)throw Fault(ins.pc,"position cache refresh requires an actor");
        set_actor_raw_position(memory_,actor,memory_.read(actor+actor_offset::world_x),memory_.read(actor+actor_offset::world_y));next(ins);return Yield::Continue;
    }
    if(ins.opcode==opcode::actor_hop_child||ins.opcode==opcode::actor_arc_child)return object_command(ins);
    if(ins.opcode==opcode::block_count){
        const auto d=depth();if(!d)throw Fault(ins.pc,"block count query outside a block");
        const auto count=memory_.read(object_+vm_offset::block_counts+(d-1)*4);if(signed32(count)<1)throw Fault(ins.pc,"block count query requires positive count");
        const auto result=ins.subop?ins.operand(memory_,1).get(memory_,object_)-count:count;
        ins.operand(memory_,0).set(memory_,object_,result);next(ins);return Yield::Continue;
    }
    if(ins.opcode==opcode::frame_bias){
        const auto value=ins.operand(memory_,0).get(memory_,object_);memory_.write(globals::animation_frame_bias,ins.subop?memory_.read(globals::animation_frame_bias)+value:value);next(ins);return Yield::Continue;
    }
    if(ins.opcode==opcode::worldmap_slot){
        //42e591: slot operations and selector remapping are distinct domains.
        const auto get=[&](unsigned i){return ins.operand(memory_,i).get(memory_,object_);};
        if(ins.subop==8){
            constexpr unsigned selectors[]={0,1,2,5,4,9,11,10,13,14,8,12};const auto selector=get(0);
            if(selector>=12)throw Fault(ins.pc,"worldmap effect selector outside original remap");memory_.write(0x5aff7c,selectors[selector]);
        }else{
            const auto parameter=ins.subop==10?get(2):ins.subop==11?get(1):0;
            const auto destination=ins.subop==10?get(1):0,slot=get(0);
            if(slot>=44){if(ins.subop!=4)throw Fault(ins.pc,"worldmap city slot outside original table");}
            else{
                const auto at=globals::worldmap_city_flags+slot*layout::worldmap_city_stride,flags=memory_.read(at);
                if(ins.subop<=2)memory_.write(at,ins.subop==0?flags&~7u:ins.subop==1?(flags&~3u)|4u:flags|6u);
                else if(ins.subop==4){if(memory_.read(at+4)){memory_.write(0x77149c,memory_.read(at+8));memory_.write(0x7714a0,memory_.read(at+12));memory_.write(globals::game_mode,10);}}
                else if(ins.subop==10){memory_.write(at+8,destination);memory_.write(at+12,parameter);}
                else memory_.write(at,parameter?flags|0x10u:flags&~0x10u);
            }
        }
        next(ins);return Yield::Continue;
    }
    if (ins.opcode == opcode::native_callback) return callback_command(ins);
    if (ins.opcode == opcode::dialogue_spawn_or_wait || ins.opcode == opcode::dialogue_control) return dialogue_command(ins);
    if (ins.opcode == opcode::viewport) {
        if (!env_.viewport) throw Fault(ins.pc, "viewport subsystem is not attached");
        if(ins.subop<=4){env_.viewport->event_view(ins.subop);next(ins);return Yield::Continue;}
        if(ins.subop==5)env_.viewport->center(800,450); // Actual42a366 arguments, including clip/border flags.
        else{
            const auto height = signed32(ins.operand(memory_, 1).get(memory_, object_)), width = signed32(ins.operand(memory_, 0).get(memory_, object_));
            if (ins.subop == 6) env_.viewport->tween_center(30, width, height); else env_.viewport->center(width, height);
        }
        memory_.write(0x769440, 2); next(ins); return Yield::Continue;
    }
    if(ins.opcode==opcode::current_map){
        const auto operand=ins.operand(memory_,0);
        if(ins.subop==0)operand.set(memory_,object_,memory_.read(globals::current_map_id));
        else{
            const auto target=operand.get(memory_,object_);
            if(const auto osd=resolve_compact(memory_,memory_.read(globals::debug_osd_handle)))memory_.write(*osd+0x20,0xffffffffu);
            memory_.write(globals::debug_osd_handle,0);
            if(target!=memory_.read(globals::current_map_id)){
                if(!env_.map||!env_.palette)throw Fault(ins.pc,"map resume needs map and palette services");
                Actors actor_fallback(memory_);auto& actors=env_.actors?*env_.actors:actor_fallback;const auto player=actors.player_id(),actor=lookup_actor(memory_,player),state=(memory_.read(actor+4)>>16)&1;
                if((memory_.read(globals::party_selection_mask)>>16)&3){if(!env_.dialogue)throw Fault(ins.pc,"map resume extra actors require dialogue cleanup");actors.release_extra_party(*env_.dialogue);}
                env_.map->switch_map(target,ins.subop==1?1:0);
                actors.rebuild_extra_party();env_.palette->upload(globals::palette_target);set_actor_tile_state(memory_,player,state);
            }
        }
        next(ins);return Yield::Continue;
    }
    if (ins.opcode == opcode::load_map) {
        if (!env_.map) throw Fault(ins.pc, "map subsystem is not attached");
        const auto mode0 = ins.operand(memory_, 1).get(memory_, object_), mode1 = ins.operand(memory_, 2).get(memory_, object_);
        Actors actor_fallback(memory_);auto& actors=env_.actors?*env_.actors:actor_fallback; const auto player = actors.player_id();
        const auto osd = resolve_compact(memory_, memory_.read(globals::debug_osd_handle));
        if (osd) memory_.write(*osd + 0x20, 0xffffffff); memory_.write(globals::debug_osd_handle, 0);
        if((memory_.read(globals::party_selection_mask)>>16)&3){if(!env_.dialogue)throw Fault(ins.pc,"map transition extra actors require dialogue cleanup");actors.release_extra_party(*env_.dialogue);}
        env_.map->switch_map(ins.operand(memory_, 0).get(memory_, object_), unsigned(mode0 != 0) | (unsigned(mode1 != 0) << 1));
        actors.rebuild_extra_party();set_actor_tile_state(memory_, player, 0); next(ins); return Yield::Continue;
    }
    if (ins.opcode == opcode::camera) return camera_command(ins);
    if (ins.opcode == opcode::compact_sprite || ins.opcode == opcode::compact_sprite_offset) return sprite_command(ins);
    if (ins.opcode == 0xc0 || ins.opcode == opcode::bgm || ins.opcode == opcode::audio_control || ins.opcode == opcode::sound_cue) return audio_command(ins);
    if (ins.opcode == opcode::copy_vector || ins.opcode == opcode::advance_vector) return vector_command(ins);
    if (ins.opcode == opcode::actor_vector || ins.opcode == opcode::query_step || ins.opcode == opcode::apply_step_height || ins.opcode == opcode::place_actor || ins.opcode == opcode::turn_actor || ins.opcode == opcode::walk_actor || ins.opcode == opcode::follow_path || ins.opcode == opcode::tween_actor || ins.opcode == opcode::actor_visibility || ins.opcode == opcode::read_path_command || ins.opcode == opcode::next_path_command || ins.opcode == opcode::effect_lifecycle || ins.opcode == opcode::release_actor || ins.opcode == opcode::allocate_actor || ins.opcode == opcode::bind_actor || ins.opcode == opcode::current_actor_direction || ins.opcode == opcode::timed_displacement || ins.opcode == opcode::reset_actor_frame || ins.opcode == opcode::actor_on_screen || ins.opcode == opcode::party) return actor_command(ins);
    if (ins.opcode >= opcode::wait_transition && ins.opcode <= opcode::palette_fade) return palette_command(ins);
    if(ins.opcode==opcode::rectangle_effect){
        if(!env_.transition)throw Fault(ins.pc,"rectangle effect service is not attached");
        const auto mode=ins.operand(memory_,0).get(memory_,object_);auto duration=ins.operand(memory_,1).get(memory_,object_);
        if(env_.input_pause||env_.dialog_busy)duration=0;
        const auto actor=ins.subop==0||ins.subop==4?lookup_actor(memory_,ins.operand(memory_,2).get(memory_,object_)):0;
        env_.transition->start(ins.subop,mode,duration,actor);memory_.write(globals::transition_wait_phase,0);next(ins);return Yield::Continue;
    }
    if (ins.opcode == opcode::object_field || ins.opcode == opcode::actor_sequence || ins.opcode == opcode::timed_sprite_frame || ins.opcode == opcode::sprite_frame) return object_command(ins);
    if (ins.opcode == opcode::event_lifecycle) {
        if(ins.subop>=4){
            if(!env_.dialogue||!env_.dialogue->graphics)throw Fault(ins.pc,"event title requires dialog graphics");
            if(ins.subop==4)env_.dialogue->graphics->show_direct_text(ins.operand(memory_,0).get(memory_,object_));else env_.dialogue->graphics->clear_direct_text();
            next(ins);return Yield::Continue;
        }
        if (ins.subop == 0) {
            const auto party_index = memory_.read(globals::active_party_index);
            if (party_index > 15) throw Fault(ins.pc, "invalid active party index");
            set_actor_tile_state(memory_, memory_.read(globals::party_actor_ids + party_index * 4), 0);
            memory_.write(object_ + compact_offset::flags_high_byte, memory_.read(object_ + compact_offset::flags_high_byte, 1) | 0x10, 1);
            const auto osd = resolve_compact(memory_, memory_.read(globals::debug_osd_handle));
            if (osd && memory_.read(*osd + 0x20) != 4) {
                memory_.write(*osd + 0x2c, memory_.read(*osd + 0x20) == 1 ? 20 - memory_.read(*osd + 0x2c) : 0);
                memory_.write(*osd + 0x20, 4);
            }
            memory_.write(globals::pending_event_id, 0xffffffff); memory_.write(globals::resume_event_id, 0xffffffff);
            memory_.write(0x768a98, 0); memory_.write(globals::phase_interval_ms, 3);
        } else if(ins.subop==1){
            if(!env_.dialogue)throw Fault(ins.pc,"event finalization needs dialogue subsystem");
            const auto previous=memory_.read(globals::current_event_id);memory_.write(globals::current_event_id,0xffffffffu);memory_.write(globals::previous_event_id,previous);
            Actors actor_fallback(memory_);auto& actors=env_.actors?*env_.actors:actor_fallback;actors.cleanup_event(*env_.dialogue);
            const auto pending=memory_.read(globals::pending_event_id);const auto activated=pending==0xffffffffu?0:Arena(memory_).activate_event(pending);
            if(!activated){
                set_actor_tile_state(memory_,actors.player_id(),1);
                if(!memory_.read(0x768a98)){if(!env_.sprites)throw Fault(ins.pc,"event finalization needs sprite cache");env_.sprites->flush_deferred();}
                memory_.write(globals::dialogue_abort_requested,1);queue_.clear();
                if(!memory_.read(0x768a98))for(unsigned i=0;i<memory_.read(globals::party_count);++i)if(i!=memory_.read(globals::active_party_index)){
                    const auto actor=Actors::slot(i);memory_.write(actor+4,memory_.read(actor+4)&~0xc0u);
                }
            }
        } else {
            const auto event_id = memory_.read(globals::current_event_id);
            if (event_id >= 168) throw Fault(ins.pc, "queue/resume without active event");
            memory_.write(ins.subop == 2 ? globals::pending_event_id : globals::resume_event_id, event_id);
            memory_.write(globals::event_activation_counts + event_id * 4, memory_.read(globals::event_activation_counts + event_id * 4) + 1);
        }
        next(ins); return Yield::Continue;
    }
    if(ins.opcode==opcode::event_count){
        const auto value=ins.subop==1?ins.operand(memory_,1).get(memory_,object_):0xffffffffu;
        const auto id=ins.subop==2?memory_.read(globals::current_event_id):ins.operand(memory_,0).get(memory_,object_);
        if(id>=168)throw Fault(ins.pc,"event count index outside original table");
        if(ins.subop==0)memory_.write(object_+vm_offset::result,memory_.read(globals::event_activation_counts+id*4));
        else memory_.write(globals::event_activation_counts+id*4,value);
        next(ins);return Yield::Continue;
    }
    if (ins.opcode == opcode::map_patch_or_special) {
        if(ins.subop==0){
            if(!env_.map)throw Fault(ins.pc,"map patch needs map subsystem");
            const auto state=ins.operand(memory_,1).get(memory_,object_),id=ins.operand(memory_,0).get(memory_,object_);
            env_.map->apply_patch(id,state!=0);next(ins);return Yield::Continue;
        }
        const auto selector=ins.operand(memory_,0).get(memory_,object_);
        if(selector==1001){
            if(!env_.dialogue)throw Fault(ins.pc,"dialog timing sync requires dialogue subsystem");
            env_.dialogue->configure_timing(signed32(memory_.read(globals::dialogue_timing_percentages)),signed32(memory_.read(globals::dialogue_timing_percentages+4)),signed32(memory_.read(globals::dialogue_timing_percentages+8)));
        }else if(selector==1000){
            if(memory_.read(0x6db57c)==0xffffffff&&memory_.read(globals::event_dialogue_auto_delay)==0){memory_.write(0x6db57c,0);memory_.write(globals::event_dialogue_auto_delay,10);}
        }else if(selector==100){
            env_.diagnostics.emplace_back(ins.pc,"STAFF.AVI playback omitted by the current port scope");
        }else return original_command(ins);
        next(ins); return Yield::Continue;
    }
    if (ins.opcode == opcode::wait) return wait(ins);
    if (ins.opcode == opcode::end || (ins.opcode >= opcode::jump && ins.opcode <= opcode::if_end)) return control(ins);
    if (ins.opcode == opcode::random || ins.opcode == opcode::unary_alu || ins.opcode == opcode::binary_alu || ins.opcode == opcode::evaluate_alu) return arithmetic(ins);
    if (ins.opcode == opcode::progress_counter) {
        if (ins.subop == 0) memory_.write(object_ + vm_offset::progress_count, memory_.read(object_ + vm_offset::progress_count) + 1);
        else {
            const auto target = resolve_compact(memory_, ins.operand(memory_, 0).get(memory_, object_));
            if (target && signed32(memory_.read(*target + 0x100)) < signed32(ins.operand(memory_, 1).get(memory_, object_)))
                return Yield::Forced;
        }
        next(ins); return Yield::Continue;
    }
    if (ins.opcode == opcode::release_object) {
        const auto operand = ins.operand(memory_, 0);
        const auto target = resolve_compact(memory_, operand.get(memory_, object_));
        if (target) memory_.write(*target + 0x20, 0xffffffffu);
        operand.set(memory_, object_, 0); next(ins); return Yield::Continue;
    }
    if (ins.opcode == opcode::standby_object) {
        const auto target = resolve_compact(memory_, ins.operand(memory_, 0).get(memory_, object_));
        if (target) {
            const auto request = ins.operand(memory_, 1).get(memory_, object_), flags = memory_.read(*target + 0x18);
            if (request && !(flags & 2)) {
                memory_.write(*target + 0x18, flags | 2);
                if (flags & 0x100000) memory_.write(globals::exclusive_active_count, memory_.read(globals::exclusive_active_count) - 1);
            } else if (!request && (flags & 2)) {
                if (flags & 0x100000) throw Fault(ins.pc, "exclusive standby reactivation requires arbiter");
                memory_.write(*target + 0x18, flags & ~2u);
            }
        }
        next(ins); return Yield::Continue;
    }
    if (ins.opcode == opcode::assign) {
        const auto value = ins.operand(memory_, 1).get(memory_, object_);
        ins.operand(memory_, 0).set(memory_, object_, value);
        next(ins); return Yield::Continue;
    }
    Yield yield = Yield::Normal;
    if (ins.subop == 1) yield = Yield::Forced;
    else if (env_.input_pause || env_.dialog_busy) {
        const auto depth = memory_.read(object_ + vm_offset::block_depth);
        if (depth > 8) throw Fault(object_ + vm_offset::block_depth, "invalid VM block depth");
        bool active = false;
        for (unsigned i = 0; i < depth; ++i) active |= memory_.read(object_ + vm_offset::block_auxiliaries + i * 4) == 1;
        if (!active) {
            ++env_.idle_frame_counter;
            if (env_.idle_frame_counter != 0x46) yield = Yield::Continue;
            else env_.idle_frame_counter = 0;
        }
    }
    next(ins); return yield;
}

FrameResult Vm::run_frame(unsigned jobs, unsigned limit) {
    FrameResult result;
    unsigned spin_budget = 490;
    while (jobs) {
        Address marker = pc();
        for (;;) {
            if (!memory_.read(object_ + compact_offset::lifecycle)) { result.finished = true; return result; }
            if (result.instructions >= limit) throw Fault(pc(), "VM instruction watchdog; no forced PC advance");
            const auto before = pc();
            if (memory_.read(object_ + vm_offset::previous_pc) == before)
                memory_.write(object_ + vm_offset::same_pc_count, memory_.read(object_ + vm_offset::same_pc_count) + 1);
            else { memory_.write(object_ + vm_offset::previous_pc, before); memory_.write(object_ + vm_offset::same_pc_count, 0); }
            const auto instruction = Instruction::decode(memory_, before);
            if (env_.trace) env_.trace(object_, instruction, false);
            const auto yield = step(); ++result.instructions;
            if (env_.trace) env_.trace(object_, instruction, true);
            if (marker == pc()) {
                if (spin_budget == 0) throw Fault(pc(), "original same-PC watchdog; interactive override disabled");
                --spin_budget;
            } else { marker = pc(); spin_budget = 490; }
            if (yield == Yield::Continue) continue;
            result.yield = yield;
            if (yield == Yield::Normal) ++result.normal_yields;
            if (!(memory_.read(object_ + compact_offset::flags_high_byte, 1) & 0x10) || yield == Yield::Forced) return result;
            break;
        }
        --jobs;
    }
    return result;
}
} // namespace fsb::core
