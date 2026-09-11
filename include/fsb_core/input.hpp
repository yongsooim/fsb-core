#pragma once
#include "primitives.hpp"

namespace fsb::core {
// Plain host observations. Legacy message numbers/scancodes are a data
// protocol, never native OS types or callbacks inside the core.
struct InputMessage {
    std::uint32_t kind=0,key=0,flags=0;
    bool shift=false,control=false;
    int x=-1,y=-1; // Optional pointer position in core logical pixels.
    bool positioned=false;
};
InputMessage keyboard_message(unsigned virtual_key,unsigned pc_scancode,bool down,
                              bool shift=false,bool control=false,bool alt=false,bool repeat=false);
InputMessage mouse_button_message(unsigned button,bool down);
InputMessage pointer_button_message(unsigned button,bool down,int x,int y);
void apply_input_state(Memory& memory,const InputMessage& message);
} // namespace fsb::core
