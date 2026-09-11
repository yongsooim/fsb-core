#include "fsb_core/dialogue.hpp"
#include "fsb_core/audio.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
void Dialogue::notify_selection(Address controller,unsigned index){
    if(memory_.read(controller+0x1a0))queue_.enqueue({(index+0x30)|0x435000,0xffffffffu,0,0});
}
void Dialogue::start_selection(Address controller,Address state){
    if(!memory_.read(state+0x16c))return;if(!audio)throw Fault(state,"choice menu requires audio service");audio->play_cue(0);
    memory_.write(state+0x15c,memory_.read(state+0x15c,2)|0x840,2);memory_.write(0x7683d4,1);memory_.write(state+0x170,0);notify_selection(controller,0);
}
bool Dialogue::update_selection(Address controller,Address state){
    auto& m=memory_;m.write(state+0x15c,m.read(state+0x15c)&~0x800u);
    const auto previous=m.read(state+0x170),count=m.read(state+0x16c),flags=m.read(globals::input_flags),key=m.read(globals::input_key);
    if(!count)throw Fault(state,"choice input without choice rows");bool confirmed=false;auto selected=previous;
    const auto wrap=[&](std::uint32_t value){return std::uint32_t(signed32(value)%signed32(count));};
    if(m.read(globals::input_message)==0x100&&(!(flags&0x40000000)||m.read(globals::shift_key_state)||m.read(0x6da698))){
        bool changed=false;const auto signature=((flags>>1)&0x800000)|(flags&0xff0000);
        if(key==13)confirmed=!(flags&0x40000000);
        else if(key==32||key==40){selected=wrap(previous+1);changed=true;}
        else if(key==38){selected=wrap(count+previous-1);changed=true;}
        else if(key==88||signature==0x4f0000)confirmed=m.read(state+12)==5||(signed32(m.read(globals::current_event_id))<0&&!(flags&0x40000000));
        if(!changed){if((signature>>16)==0x48)selected=wrap(selected+count-1);else if((signature>>16)==0x50)selected=wrap(selected+1);}
    }
    m.write(state+0x170,selected);
    if(!audio)throw Fault(state,"choice input requires audio service");
    if(selected!=previous){notify_selection(controller,selected);audio->play_cue(1);} //40e4ac push1.
    if(!confirmed)return false;
    if(const auto mailbox=m.read(controller+0xe8))m.write(mailbox,selected);
    m.write(state+0x15c,m.read(state+0x15c)&~0x40u);m.write(0x7683d4,0);m.write(state+0x170,0);audio->play_cue(2);return true; //40e4e0 push2.
}
} // namespace fsb::core
