#pragma once
#include "primitives.hpp"
#include "symbols.hpp"

namespace fsb::core {
enum class Yield : unsigned;
// Game-language control flow. This stack belongs to the event language;
// it is independent of x86 registers, the CPU stack and platform services.
class ScriptControl {
public:
    ScriptControl(Memory& memory,Address object):memory_(memory),object_(object){}
    unsigned depth()const;
    Yield execute(const Instruction& instruction);
    void push_call(Address return_pc);
    void pop_loop();
    void select_branch(bool enter);
    static Address find_marker(const Memory& memory,Address start,std::uint16_t opener,std::uint16_t closer,bool accept_else=false);
private:
    Memory& memory_;
    Address object_;
    Address pc()const{return memory_.read(object_+vm_offset::pc);}
    void next(const Instruction& instruction){memory_.write(object_+vm_offset::pc,instruction.next());}
    void push(Address head,Address exit,std::optional<std::uint32_t> count,std::uint32_t auxiliary);
    void pop();
    Yield finish_or_return();
    Address scan_to(Address start,unsigned opener,unsigned closer,bool accept_else=false)const {
        return find_marker(memory_,start,std::uint16_t(opener),std::uint16_t(closer),accept_else);
    }
};
}
