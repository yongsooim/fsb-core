#include "fsb_core/script_control.hpp"
#include "fsb_core/vm.hpp"

namespace fsb::core {
unsigned ScriptControl::depth() const {
    const auto value = memory_.read(object_ + vm_offset::block_depth);
    if (value > 8) throw Fault(object_ + vm_offset::block_depth, "invalid block depth");
    return value;
}
void ScriptControl::push(Address head, Address exit, std::optional<std::uint32_t> count, std::uint32_t auxiliary) {
    const auto d = depth();
    if (d == 8) throw Fault(pc(), "block stack overflow");
    memory_.write(object_ + vm_offset::block_heads + d * 4, head); memory_.write(object_ + vm_offset::block_exits + d * 4, exit);
    if (count) memory_.write(object_ + vm_offset::block_counts + d * 4, *count);
    memory_.write(object_ + vm_offset::block_auxiliaries + d * 4, auxiliary); memory_.write(object_ + vm_offset::block_depth, d + 1);
    //41b3b8 and the inline loop pushes assert after publishing the frame.
    if(d+1>=8)throw Fault(pc(),"block stack overflow");
}
void ScriptControl::pop() {
    const auto d = depth();
    memory_.write(object_ + vm_offset::block_depth, d - 1);
    if (!d) throw Fault(pc(), "block stack underflow");
    const auto exit=memory_.read(object_ + vm_offset::block_exits + (d - 1) * 4);
    if(!memory_.read(object_+vm_offset::block_heads+(d-1)*4)||!exit)throw Fault(pc(),"loop frame has no head or exit");
    memory_.write(object_ + vm_offset::pc,exit);
}
Yield ScriptControl::finish_or_return() {
    auto d = depth();
    if (!d) { memory_.write(object_ + compact_offset::lifecycle, 0); return Yield::Forced; }
    while (d) {
        --d; memory_.write(object_ + vm_offset::block_depth, d);
        const auto exit = memory_.read(object_ + vm_offset::block_exits + d * 4);
        if (memory_.read(object_ + vm_offset::block_heads + d * 4) == 0 && exit) {
            memory_.write(object_ + vm_offset::pc, exit); return Yield::Continue;
        }
    }
    throw Fault(pc(), "block return found no inactive return frame");
}
void ScriptControl::push_call(Address return_pc) { push(0,return_pc,std::nullopt,0); }
void ScriptControl::pop_loop() { pop(); }
void ScriptControl::select_branch(bool enter) {
    const auto d=depth();
    if(!d)throw Fault(pc(),"branch selection requires a control frame");
    const auto count_field=object_+vm_offset::block_counts+(d-1)*4;
    if(enter) {
        memory_.write(count_field,1);
        const auto current=pc();
        memory_.write(object_+vm_offset::pc,current+memory_.read(current+2,2));
    } else {
        const auto current=pc();
        const auto target=scan_to(current+memory_.read(current+2,2),opcode::if_begin,opcode::if_end,true);
        //41c7d8: finish scanning before publishing the false branch state.
        memory_.write(count_field,0);
        memory_.write(object_+vm_offset::pc,target);
    }
}
Address ScriptControl::find_marker(const Memory& memory,Address start,std::uint16_t opener,std::uint16_t closer,bool accept_else) {
    unsigned nested = 0;
    for (unsigned guard = 0; guard < 100000; ++guard) {
        const auto command=memory.read(start,1);
        if (command == opener) ++nested;
        else if (command == closer) { if (!nested) return start; --nested; }
        else if (accept_else && !nested && command == opcode::else_branch) return start;
        // The original helper reads the marker before its length; the matching
        // marker need not have a decodable payload to identify this boundary.
        const auto length=memory.read(start+2,2);
        if(!length)throw Fault(start,"control scan cannot advance");
        start+=length;
    }
    throw Fault(start, "unterminated control block");
}
Yield ScriptControl::execute(const Instruction& ins) {
    // Source handlers assert on these invalid variants. Other control handlers
    // ignore the subop byte, including repeated calls.
    const auto maximum_subop=[&]() -> unsigned {
        switch(ins.opcode){
        case opcode::end:case opcode::branch_table:case opcode::if_begin:return 2;
        case opcode::conditional_jump:case opcode::while_begin:case opcode::break_block:return 1;
        case opcode::else_branch:return 3;
        default:return 255;
        }
    }();
    if(ins.subop>maximum_subop)throw Fault(ins.pc,"invalid control-flow variant");
    const auto get = [&](unsigned i) { return ins.operand(memory_, i).get(memory_, object_); };
    const auto condition = [&](unsigned sub) {
        if (sub == 0) return get(0) != 0;
        const auto right = get(2), left = get(0), selector = get(1);
        const bool value = sequence_alu(selector, left, right) != 0;
        return sub == 2 ? !value : value;
    };
    if(ins.opcode==opcode::break_block){
        if(ins.subop){const auto rhs=get(2),lhs=get(0),selector=get(1);if(!sequence_alu(selector,lhs,rhs)){next(ins);return Yield::Continue;}}
        auto d=depth();Address exit=0;
        do{if(!d)throw Fault(ins.pc,"break has no enclosing exit");memory_.write(object_+vm_offset::block_depth,--d);exit=memory_.read(object_+vm_offset::block_exits+d*4);}while(!exit);
        if(!memory_.read(object_+vm_offset::block_heads+d*4))throw Fault(ins.pc,"break reached a call frame instead of a loop");
        memory_.write(object_+vm_offset::block_counts+d*4,0);memory_.write(object_+vm_offset::pc,exit);return Yield::Continue;
    }
    if (ins.opcode == opcode::end) {
        if (ins.subop == 0) { memory_.write(object_ + compact_offset::lifecycle, 0); return Yield::Forced; }
        if (ins.subop == 2) {
            const auto right = get(2), left = get(0), selector = get(1);
            if (!sequence_alu(selector, left, right)) { next(ins); return Yield::Continue; }
        }
        return finish_or_return();
    }
    if(ins.opcode==opcode::conditional_jump){
        bool taken;
        if(ins.subop==1)taken=get(1)==0;
        else{const auto rhs=get(3),lhs=get(1),selector=get(2);taken=sequence_alu(selector,lhs,rhs)!=0;}
        if(!taken)next(ins);
        else{if(depth()&&!memory_.read(object_+vm_offset::block_exits+(depth()-1)*4))memory_.write(object_+vm_offset::block_depth,depth()-1);memory_.write(object_+vm_offset::pc,get(0));}
        return Yield::Continue;
    }
    if(ins.opcode==opcode::repeat_call){
        auto d=depth();const bool reentry=d&&memory_.read(object_+vm_offset::block_exits+(d-1)*4)==ins.next();
        if(!reentry){const auto count=get(1);if(!count){next(ins);return Yield::Continue;}push(ins.pc,ins.next(),count,0);d=depth();}
        const auto remaining=memory_.read(object_+vm_offset::block_counts+(d-1)*4);
        if(!remaining){memory_.write(object_+vm_offset::block_depth,d-1);next(ins);return Yield::Continue;}
        memory_.write(object_+vm_offset::block_counts+(d-1)*4,remaining-1);push(0,ins.pc,std::nullopt,0);memory_.write(object_+vm_offset::pc,get(0));return Yield::Continue;
    }
    if (ins.opcode == opcode::jump || ins.opcode == opcode::branch_table || ins.opcode == opcode::call_table || ins.opcode == opcode::call) {
        if (ins.opcode == opcode::jump) {
            if (depth() && memory_.read(object_ + vm_offset::block_exits + (depth() - 1) * 4) == 0)
                memory_.write(object_ + vm_offset::block_depth, depth() - 1);
            memory_.write(object_ + vm_offset::pc, get(0)); return Yield::Continue;
        }
        if (ins.opcode == opcode::call) {
            push_call(ins.next());
            memory_.write(object_ + vm_offset::pc, get(0)); return Yield::Continue;
        }
        Address target;
        if (ins.opcode == opcode::branch_table || ins.opcode == opcode::call_table) {
            const auto count = get(0), index = get(1);
            if (signed32(index) < 0 || index >= count) throw Fault(ins.pc, "branch table index outside count");
            const auto offset = 14ull + std::uint64_t(index) * 4;
            if (offset + (ins.opcode == opcode::branch_table && ins.subop == 2 ? 8 : 4) > ins.length)
                throw Fault(ins.pc, "branch table exceeds command payload");
            target = memory_.read(ins.pc + static_cast<Address>(offset));
            if (ins.opcode == opcode::branch_table && ins.subop) {
                memory_.write(object_ + 0xe8, target);
                if (ins.subop == 2) memory_.write(object_ + 0xec, memory_.read(ins.pc + static_cast<Address>(offset) + 4));
                next(ins); return Yield::Continue;
            }
        } else target = get(0);
        if (ins.opcode == opcode::call_table) push_call(ins.next());
        else if (depth() && memory_.read(object_ + vm_offset::block_exits + (depth() - 1) * 4) == 0)
            memory_.write(object_ + vm_offset::block_depth, depth() - 1);
        memory_.write(object_ + vm_offset::pc, target); return Yield::Continue;
    }
    if (ins.opcode == opcode::repeat_begin) {
        const auto count = get(0);
        const auto end = Instruction::decode(memory_, scan_to(ins.next(), opcode::repeat_begin, 0x0c)).next();
        if (!count) memory_.write(object_ + vm_offset::pc, end);
        else { push(ins.next(), end, count, ins.subop != 0); next(ins); }
    } else if (ins.opcode == opcode::repeat_end) {
        const auto d = depth();
        if (!d || memory_.read(object_ + vm_offset::block_exits + (d - 1) * 4) != ins.next()) throw Fault(ins.pc, "repeat tail does not match frame");
        const auto count = memory_.read(object_ + vm_offset::block_counts + (d - 1) * 4);
        if (signed32(count) < 1) throw Fault(ins.pc, "invalid repeat count");
        memory_.write(object_ + vm_offset::block_counts + (d - 1) * 4, count - 1);
        if (count == 1) pop(); else memory_.write(object_ + vm_offset::pc, memory_.read(object_ + vm_offset::block_heads + (d - 1) * 4));
    } else if (ins.opcode == opcode::while_begin) {
        const auto d = depth();
        const bool reentry = d && memory_.read(object_ + vm_offset::block_heads + (d - 1) * 4) == ins.pc;
        bool enter = true;
        if (ins.subop == 0) {
            const auto right = get(2), left = get(0), selector = get(1);
            enter = sequence_alu(selector, left, right) != 0;
        }
        if (reentry) { if (enter) next(ins); else pop(); }
        else {
            const auto end = Instruction::decode(memory_, scan_to(ins.next(), opcode::while_begin, 0x0e)).next();
            if (enter) { push(ins.pc, end, std::nullopt, 1); next(ins); }
            else memory_.write(object_ + vm_offset::pc, end);
        }
    } else if (ins.opcode == opcode::while_end) {
        const auto d = depth();
        if (!d || memory_.read(object_ + vm_offset::block_exits + (d - 1) * 4) != ins.next()) {
            memory_.write(0x768a8c,unsigned(Yield::Normal)); //41bfa5 precedes the original assertion.
            throw Fault(ins.pc, "loop tail does not match frame");
        }
        memory_.write(object_ + vm_offset::pc, memory_.read(object_ + vm_offset::block_heads + (d - 1) * 4));
    } else if (ins.opcode == opcode::if_begin || ins.opcode == opcode::else_branch) {
        bool enter = false;
        if (ins.opcode == opcode::if_begin) {
            enter = condition(ins.subop); // Evaluate against the pre-push context.
            const auto d = depth(); if (d == 8) throw Fault(ins.pc, "conditional stack overflow");
            memory_.write(object_ + vm_offset::block_exits + d * 4, 0); memory_.write(object_ + vm_offset::block_auxiliaries + d * 4, 0);
            memory_.write(object_ + vm_offset::block_depth, d + 1);
            if(d+1>=8)throw Fault(ins.pc,"conditional stack overflow");
        } else {
            const auto d = depth();
            if (!d || memory_.read(object_ + vm_offset::block_exits + (d - 1) * 4)) throw Fault(ins.pc, "else outside conditional");
            if (memory_.read(object_ + vm_offset::block_counts + (d - 1) * 4) == 1) {
                memory_.write(object_ + vm_offset::pc, scan_to(ins.next(), opcode::if_begin, 0x12)); return Yield::Continue;
            }
            enter = ins.subop == 3 || condition(ins.subop);
        }
        select_branch(enter);
    } else if (ins.opcode == opcode::if_end) {
        const auto d = depth();
        memory_.write(object_ + vm_offset::block_depth, d - 1);
        if (!d || memory_.read(object_ + vm_offset::block_exits + (d - 1) * 4)) throw Fault(ins.pc, "conditional end outside conditional");
        memory_.write(object_ + vm_offset::block_counts + (d - 1) * 4, 0); next(ins);
    } else throw Fault(ins.pc, "unimplemented control flow");
    return Yield::Continue;
}

} // namespace fsb::core
