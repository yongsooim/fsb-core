#include "fsb_core/input.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
InputMessage keyboard_message(unsigned key,unsigned scan,bool down,bool shift,bool control,bool alt,bool repeat){
    if(key>255||scan>255)throw Fault(key,"input key/scancode exceeds original tables");
    const auto flags=1u|((scan&127u)<<16)|((scan&128u)?unsigned(keyboard_flag::extended_key):0u)|(alt?unsigned(keyboard_flag::alt_context):0u)|((repeat||!down)?unsigned(keyboard_flag::previously_down):0u)|(!down?0x80000000u:0u);
    return {down?(alt?unsigned(input_message::system_key_down):unsigned(input_message::key_down)):(alt?unsigned(input_message::system_key_up):unsigned(input_message::key_up)),key,flags,shift,control};
}
InputMessage mouse_button_message(unsigned button,bool down){
    if(button>1)throw Fault(button,"only original left/right mouse buttons are connected");
    return {input_message::mouse_button,(button?0x40u:0x80u)|(down?3u:0x18u),0};
}
InputMessage pointer_button_message(unsigned button,bool down,int x,int y){
    auto message=mouse_button_message(button,down);message.x=x;message.y=y;message.positioned=true;return message;
}
void apply_input_state(Memory& memory,const InputMessage& input){
    if(input.kind==input_message::key_down||input.kind==input_message::key_up||input.kind==input_message::system_key_down||input.kind==input_message::system_key_up){
        //0x40758d WH_KEYBOARD body. Shift/Control are GetKeyState observations;
        // the two remaining tables store scancode and virtual-key held state.
        memory.write(globals::shift_key_state,input.shift?0x8000:0);memory.write(globals::control_key_state,input.control?0x8000:0);
        memory.write(globals::alt_context_state,input.flags&keyboard_flag::alt_context);memory.write(globals::no_modifiers_held,!input.shift&&!input.control&&!(input.flags&keyboard_flag::alt_context));
        const auto scan=((input.flags>>16)&255)|((input.flags>>17)&128),key=input.key&255;
        const auto held=(input.flags>>31)^1u;
        memory.write(globals::last_key_held,held);memory.write(globals::scan_key_states+scan*4,held);memory.write(globals::virtual_key_states+key*4,held);
        memory.write(globals::last_scan_code,scan);memory.write(globals::last_virtual_key,key);
        if(input.kind==input_message::key_down&&input.key==19)memory.write(globals::runtime_mode_flags,memory.read(globals::runtime_mode_flags)^2u); // Original Pause-key window handler.
    }else if(input.kind==input_message::mouse_button){
        const auto button=input.key&0xc0,action=input.key&0x3f;
        if((button==0x80||button==0x40)&&(action==3||action==5||action==0x18||action==0x28))
            memory.write(globals::left_mouse_held+(button==0x40),action==3||action==5,1);
    }
}
} // namespace fsb::core
