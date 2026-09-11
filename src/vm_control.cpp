#include "fsb_core/vm.hpp"
#include "fsb_core/script_control.hpp"
#include "fsb_core/debug_rewards.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
unsigned Vm::depth()const{return ScriptControl(memory_,object_).depth();}
Yield Vm::control(const Instruction& instruction){return ScriptControl(memory_,object_).execute(instruction);}
Yield Vm::arithmetic(const Instruction& ins) {
    const auto operand = ins.operand(memory_, 0);
    const auto gold_gain=[&](std::uint32_t amount){return env_.debug_rewards&&operand.location(object_)==0x803a18?env_.debug_rewards->scale_positive(amount):amount;};
    if (ins.opcode == opcode::evaluate_alu) {
        const unsigned first = ins.subop == 1 ? 1 : 0;
        const auto left = ins.operand(memory_, first).get(memory_, object_);
        const auto selector = ins.operand(memory_, first + 1).get(memory_, object_);
        auto right = ins.operand(memory_, first + 2).get(memory_, object_);
        if(ins.subop!=2&&selector==8&&operand.location(object_)==0x803a18&&left==memory_.read(0x803a18))right=gold_gain(right);
        const auto result = sequence_alu(selector, left, right);
        if (ins.subop != 2) operand.set(memory_, object_, result);
        else if (!result) env_.diagnostics.emplace_back(ins.pc, "confirm error"); // 0x420018 logs and advances.
        next(ins); return Yield::Continue;
    }
    auto value = operand.get(memory_, object_);
    if (ins.opcode == opcode::random) {
        if (ins.subop == 0) {
            if (!value) throw Fault(ins.pc, "sequence RNG modulus is zero");
            // This is NOT MSVC rand: the script VM has its own full-width LCG.
            const auto state = memory_.random_state().next_sequence();
            memory_.write(object_ + vm_offset::result, state % value);
        } else {
            if (!value) {
                if(env_.local_time_seed)value=*env_.local_time_seed;
                else if(env_.local_time){for(auto field:env_.local_time())value+=field;}
                else throw Fault(ins.pc, "local-time seed observation required");
            }
            memory_.random_state().sequence_seed=value;
        }
    } else {
        if (ins.opcode == opcode::unary_alu) {
            if (ins.subop == 0) value = !value;
            else if (ins.subop == 1) value = ~value;
            else if (ins.subop == 2) value+=gold_gain(1);
            else --value;
        } else {
            auto right = ins.operand(memory_, 1).get(memory_, object_);
            if(ins.subop==0)right=gold_gain(right);
            if (ins.subop <= 4) value = sequence_alu(ins.subop + 8, value, right);
            else if (ins.subop == 8) value <<= right & 31u;
            else value >>= right & 31u; // opcode29/09 is logical; ALU selector18 is arithmetic.
        }
        operand.set(memory_, object_, value);
    }
    next(ins); return Yield::Continue;
}
} // namespace fsb::core
