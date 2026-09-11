#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/recovered_battle.hpp"

namespace fsb::core {
Yield Vm::callback_command(const Instruction& ins){
    if((ins.length-4)%5||ins.length<9)throw Fault(ins.pc,"invalid callback operand list");
    const auto callback=ins.operand(memory_,0).get(memory_,object_);
    const unsigned count=(ins.length-4)/5-1;std::vector<std::uint32_t> args(count);
    for(unsigned i=count;i>0;--i)args[i-1]=ins.operand(memory_,i).get(memory_,object_);
    if(!env_.palette)throw Fault(ins.pc,"palette callback subsystem is not attached");
    std::uint32_t result;
    switch(callback){
    case 0x404cf0:{
        if(count!=3)throw Fault(ins.pc,"grayscale callback arity mismatch");
        if(!args[0]||!args[1])throw Fault(0x40192a,"grayscale requires source and destination");
        if(signed32(args[2])<=0){result=args[2];break;}
        const auto bytes=std::uint64_t(args[2])*4;
        if(bytes>0xffffffffu)throw Fault(ins.pc,"grayscale callback size overflow");
        result=unsigned(Palette::grayscale(memory_.span(args[0],std::size_t(bytes)),memory_.view(args[1],std::size_t(bytes))))*0x101u;break;
    }
    case 0x404ed0:{
        if(count!=4)throw Fault(ins.pc,"word-copy callback arity mismatch");
        if(!args[3]){result=0;break;}
        const auto bytes=std::uint64_t(args[3])*4;
        if(bytes>0xffffffffu)throw Fault(ins.pc,"word-copy callback size overflow");
        const auto begin=args[2]*4;
        Palette::copy_words(memory_.span(args[0]+begin,std::size_t(bytes)),memory_.view(args[1]+begin,std::size_t(bytes)));
        result=args[0]+begin+args[3]*4;break;
    }
    case routines::palette_gradient:
        if(count!=5)throw Fault(ins.pc,"palette gradient callback arity mismatch");
        result=env_.palette->gradient(args[0],args[1],args[2],args[3],args[4]);break;
    case routines::begin_palette_fade:
        if(count!=4)throw Fault(ins.pc,"palette fade callback arity mismatch");
        env_.palette->begin(args[0],args[1],args[2],args[3]);result=args[2]?unsigned(globals::palette_work):0u;break;
    case routines::upload_palette:
    case routines::upload_palette_copy:
        if(count!=3)throw Fault(ins.pc,"palette upload callback arity mismatch");
        env_.palette->upload(args[0],args[1],args[2],callback==routines::upload_palette_copy);result=0;break; // Translated SetEntries succeeds synchronously.
    default:
        if(!env_.recovered||!RecoveredBattle::has_entry(callback))throw Fault(callback,"original callback has no translated core implementation");
        result=env_.recovered->callback(callback,args);break;
    }
    memory_.write(object_+vm_offset::result,result);next(ins);return Yield::Continue;
}
} // namespace fsb::core
