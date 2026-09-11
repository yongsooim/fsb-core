#include "fsb_core/original_runtime.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core::original_runtime {
bool dispatch(Address entry, RecoveredBattle& call, Runtime& runtime) {
    auto& memory = runtime.memory;
    const auto arg = [&](unsigned index) { return call.argument(index); };
    switch (entry) {
    case 0x859484:{
        if(!runtime.environment.local_time)throw Fault(entry,"local-time observation was not supplied by the host");
        const auto fields=runtime.environment.local_time();for(unsigned i=0;i<fields.size();++i)call.write(arg(0)+i*2,fields[i],2);
        call.result(0,4);return true;
    }
    case 0x859554:runtime.post_input({arg(1),arg(2),arg(3)});call.result(1,16);return true;
    case 0x8594b4:
        if(runtime.environment.system_beep)call.result(runtime.environment.system_beep(arg(0)),4);
        else{runtime.environment.diagnostics.emplace_back(entry,"system notification sound requested");call.result(0,4);}
        return true;
    case 0x40112a:call.result(memory.allocate_zeroed(std::max(1u,arg(0))),4);return true;
    case 0x401151:{
        const auto size=std::uint64_t(arg(0))*arg(1);
        if(size>0xffffffffu)throw Fault(entry,"original calloc allocation overflow");
        call.result(memory.allocate_zeroed(std::max(1u,unsigned(size))),8);return true;
    }
    case 0x401188:{
        const auto old=arg(0),size=arg(1);
        if(!size){memory.release_allocation(old);call.result(0,8);return true;}
        std::vector<std::uint8_t> retained;
        if(old){
            const auto allocated=memory.allocation_size(old);
            if(!allocated)throw Fault(old,"invalid guest realloc source");
            retained=memory.bytes(old,std::min<std::size_t>(size,*allocated));
        }
        const auto next=memory.allocate_zeroed(size);
        for(unsigned i=0;i<retained.size();++i)memory.write(next+i,retained[i],1);
        memory.release_allocation(old);call.result(next,8);return true;
    }
    case 0x4972b0:memory.release_allocation(arg(0));call.result(0);return true;
    case 0x497470:{
        const auto source=arg(0),character=arg(1)&255;Address found=0;
        for(unsigned i=0;;++i){const auto value=call.read(source+i,1);if(value==character)found=source+i;if(!value)break;}
        call.result(found);return true;
    }
    case 0x40192a:throw Fault(entry,call.format_text(arg(0),call.r[4]+8));
    case 0x401a02:runtime.environment.diagnostics.emplace_back(entry,call.format_text(arg(0),call.r[4]+8));call.result(0);return true;
    case 0x40110f:runtime.post_input({arg(0),arg(1),arg(2)});call.result(1,12);return true;
    case 0x402693:call.result(resolve_compact(memory,arg(0)).value_or(0),4);return true;
    case 0x4026c1:call.result(compact_address(std::uint16_t(arg(0))),4);return true;
    case 0x4026d4:call.result(compact_handle(memory,arg(0)),4);return true;
    case 0x40277a:call.result(runtime.arena.allocate_after(arg(0),arg(1),arg(2),arg(3)),16);return true;
    case 0x402a02:runtime.arena.release(compact_handle(memory,arg(0)));call.result(0,4);return true;
    case 0x402a6f:runtime.arena.standby(arg(0));call.result(0,4);return true;
    case 0x402a8f:runtime.arena.wake(arg(0));call.result(0,4);return true;
    case 0x402abb:call.result(runtime.arena.activate_exclusive(arg(0)),4);return true;
    case 0x402b67:call.result(runtime.arena.active_exclusive(arg(0)),4);return true;
    case 0x40275a:
        if(const auto handle=call.read(arg(0))){if(const auto object=resolve_compact(memory,handle))memory.write(*object+compact_offset::lifecycle,0xffffffffu);call.write(arg(0),0);}
        call.result(0,4);return true;
    case 0x403cce:runtime.messages.clear();call.result(0);return true;
    case 0x403cd9:runtime.messages.enqueue({arg(0),arg(1),arg(2),arg(3)});call.result(0,16);return true;
    case 0x403d49:call.result(runtime.messages.find(arg(0),arg(1),arg(2)==0xffffffffu?std::nullopt:std::optional<unsigned>(arg(2))).value_or(0xffffffffu),12);return true;
    case 0x403dbd:call.result(runtime.messages.find_flagged(arg(0)).value_or(0xffffffffu),4);return true;
    case 0x403e0b:{
        const auto message=runtime.messages.at(arg(1));const unsigned values[]={message.target,message.channel,message.code,message.sender};
        for(unsigned i=0;i<4;++i)call.write(arg(0)+i*4,values[i]);call.result(0,8);return true;
    }
    case 0x403e55:runtime.messages.remove(arg(0));call.result(0,4);return true;
    case 0x403ed7:{
        std::optional<unsigned> start;
        while(const auto slot=runtime.messages.find(arg(0),arg(1),start)){
            if(runtime.messages.at(*slot).code==arg(2)){runtime.messages.remove(*slot);call.result(1,12);return true;}
            start=*slot+1;
        }
        call.result(0,12);return true;
    }
    default:return false;
    }
}
}
