#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/script_control.hpp"
#include "fsb_core/vm.hpp"

namespace fsb::core {
std::span<const std::uint8_t> RecoveredBattle::read_buffer(Address address,std::size_t size)const{
    constexpr Address base=0x01000000;
    if(address>=base&&std::uint64_t(address)-base+size<=stack_.size())return {stack_.data()+address-base,size};
    return memory_.view(address,size);
}
std::span<std::uint8_t> RecoveredBattle::write_buffer(Address address,std::size_t size){
    constexpr Address base=0x01000000;
    if(address>=base&&std::uint64_t(address)-base+size<=stack_.size())return {stack_.data()+address-base,size};
    return memory_.span(address,size);
}
bool RecoveredBattle::dispatch_native(Address entry){
    if(dispatch_effect_script(entry))return true;
    if(dispatch_event_activation(entry))return true;
    if(dispatch_handler_lifecycle(entry))return true;
    if(dispatch_attached_effects(entry))return true;
    if(dispatch_followup(entry))return true;
    if(dispatch_turn_control(entry))return true;
    if(dispatch_battle_engine(entry))return true;
    if(dispatch_entrance(entry))return true;
    if(dispatch_anchor_effects(entry))return true;
    if(dispatch_target_geometry(entry))return true;
    if(dispatch_a_actors(entry))return true;
    if(dispatch_c_maps(entry))return true;
    if(dispatch_c_maps_installers(entry))return true;
    if(dispatch_b_combat(entry))return true;
    if(dispatch_d_effects(entry))return true;
    if(dispatch_d_effect_objects(entry))return true;
    // Temporary ABI boundary for callers that still use the old execution
    //model. No reconstructed operation accesses this model itself.
    const auto control=[&](std::uint8_t opcode){
        const auto object=argument(0);
        auto instruction=Instruction::decode(memory_,memory_.read(object+vm_offset::pc));
        instruction.opcode=opcode; // The original entry, not a forged header, selects the handler.
        const auto yielded=ScriptControl(memory_,object).execute(instruction);
        if(yielded!=Yield::Continue)memory_.write(0x768a8c,unsigned(yielded));
        result(0,4);return true; // These handlers publish control state; the dispatcher does not use EAX.
    };
    switch(entry){
    case 0x41a0fb:result(ScriptControl::find_marker(memory_,argument(0),std::uint16_t(argument(1)),std::uint16_t(argument(2))),12);return true;
    case 0x41b38c:{
        const auto object=argument(0);
        ScriptControl(memory_,object).push_call(argument(1));result(object+vm_offset::block_depth,8);return true;
    }
    case 0x41b97f:{
        const auto object=argument(0);
        ScriptControl(memory_,object).pop_loop();result(memory_.read(object+vm_offset::pc),4);return true;
    }
    case 0x41c74e:{
        const auto object=argument(0),before=memory_.read(object+vm_offset::pc);const bool enter=argument(1)!=0;
        ScriptControl(memory_,object).select_branch(enter);result(enter?before:memory_.read(object+vm_offset::pc),8);return true;
    }
    case 0x498090:memory_.require_writable(0x6d1bf0,4);result(crt_rand(memory_));return true;
    case 0x499150:result(signed_magnitude(argument(0)));return true; // cdecl: caller removes its argument.
    case 0x408de1:result(fixed_sin(memory_,argument(0)),4);return true;
    case 0x408e69:result(fixed_cos(memory_,argument(0)),4);return true;
    case 0x41a126:result(sequence_alu(argument(0),argument(1),argument(2)),12);return true;
    case 0x41a2a8:return control(opcode::end);
    case 0x41aa9f:return control(opcode::jump);
    case 0x41ab8a:return control(opcode::conditional_jump);
    case 0x41afe7:return control(opcode::branch_table);
    case 0x41b1f3:return control(opcode::call_table);
    case 0x41b3e8:return control(opcode::call);
    case 0x41b49f:return control(opcode::repeat_call);
    case 0x41b9fd:return control(opcode::repeat_begin);
    case 0x41bb5c:return control(opcode::repeat_end);
    case 0x41bc31:return control(opcode::while_begin);
    case 0x41bf7f:return control(opcode::while_end);
    case 0x41bfd7:return control(opcode::break_block);
    case 0x41c275:return control(opcode::if_begin);
    case 0x41c7ed:return control(opcode::else_branch);
    case 0x41cced:return control(opcode::if_end);
    case 0x411d15:result(Dialogue::style_flags(argument(0)),4);return true;
    case 0x411d4b:result(Dialogue::style_variant(argument(0)),4);return true;
    case 0x404cf0:{
        const auto destination=argument(0),source=argument(1),count=argument(2);
        if(!destination||!source)throw Fault(0x40192a,"grayscale requires source and destination");
        if(signed32(count)<=0){result(count,12);return true;}
        const auto size=std::uint64_t(count)*4;
        if(size>0xffffffffu)throw Fault(entry,"grayscale span exceeds guest address space");
        const auto gray=Palette::grayscale(write_buffer(destination,std::size_t(size)),read_buffer(source,std::size_t(size)));
        result(unsigned(gray)*0x101u,12);return true;
    }
    case 0x404ed0:{
        const auto destination=argument(0),source=argument(1),first=argument(2),count=argument(3);
        if(!count){result(0,16);return true;}
        const auto bytes=std::uint64_t(count)*4;
        if(bytes>0xffffffffu)throw Fault(entry,"copy span exceeds guest address space");
        const auto begin=first*4;
        Palette::copy_words(write_buffer(destination+begin,std::size_t(bytes)),read_buffer(source+begin,std::size_t(bytes)));
        result(destination+begin+count*4,16);return true;
    }
    case 0x404e4c:{
        const auto destination=argument(0),source=argument(1),delta=argument(2),count=argument(3);
        if(!destination||!source)throw Fault(0x40192a,"palette RGB adjustment requires valid source and destination");
        if(signed32(count)<=0){result(count,16);return true;}
        const auto size=std::uint64_t(count)*4;
        if(size>0xffffffffu)throw Fault(entry,"palette span exceeds guest address space");
        const auto value=Palette::adjust_rgb(write_buffer(destination,std::size_t(size)),read_buffer(source,std::size_t(size)),delta);
        result(value,16);return true;
    }
    default:return false;
    }
}
}
