#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
namespace {
// 460872 title master states; 4385e9 separately owns visual/menu state771b44.
constexpr Address title_flow=0x804a64,selected_save=0x5d22a4;
constexpr unsigned menu=2,load_picker=3,load_selected=4,playing=7;
//460872 keeps a second copy of the menu/slot/load states for the defeat path.
constexpr unsigned game_over_splash=8,post_menu=9,post_load_picker=10,post_load_selected=11;
}
void Runtime::start_intro(unsigned through_event){
    initialize_session(through_event);intro_active_=true;current_event_=0xffffffffu;
    memory.write(globals::current_event_id,0xffffffffu);memory.write(title_flow,menu);
}
InputMessage Runtime::title_pointer(InputMessage input){
    if(memory.read(title_flow)!=menu||memory.read(0x771b44)!=2)return input;
    // Pointer convenience uses the original art rectangles and feeds confirm
    // into4385e9. No platform-specific hit testing or title state in the host.
    const int x=input.x-signed32(memory.read(0x771630)),y=input.y-signed32(memory.read(0x77162c));
    const Rect regions[]={{440,317,570,364},{427,364,576,407},{451,407,563,458}};
    int hit=-1;for(unsigned i=0;i<3;++i)if(x>=regions[i].left&&x<regions[i].right&&y>=regions[i].top&&y<regions[i].bottom&&(i||memory.read(0x771b48)))hit=int(i);
    if(input.key==0x83){title_pointer_down_=hit;if(hit>=0&&memory.read(0x5b14ec)!=unsigned(hit)){memory.write(0x5b14ec,unsigned(hit));audio.play_cue(0x145);}}
    else if(input.key==0x98){const int down=title_pointer_down_;title_pointer_down_=-1;if(hit>=0&&hit==down){post_input(keyboard_message(13,0x1c,false));return keyboard_message(13,0x1c,true);}}
    return {};
}
void Runtime::tick_title(){
    auto& original=battle.recovered;
    switch(memory.read(title_flow)){
    case menu:{
        const auto choice=original.invoke(0x4385e9);
        if(choice==0){
            memory.write(selected_save,0xffffffffu);original.invoke(0x431869);
            field_menu.open(8);memory.write(title_flow,load_picker);
        }else if(choice==1){memory.random_state().crt_seed=memory.read(globals::frame_time_ms);intro_active_=false;begin_new_game();} //460872 ->498080(GetTickCount()).
        else if(choice==2){intro_active_=false;memory.write(globals::game_mode,0xffffffffu);}
        return;
    }
    case load_picker:{
        // 460872: slot8 has no ESC parent. It owns selection/yes-no and returns
        // the actual slot in the command's high word only after teardown.
        const auto packed=original.invoke(0x43955f),command=packed&65535;
        if(command==1){original.invoke(0x4394ea);memory.write(title_flow,menu);}
        else if(command==3){original.invoke(0x4394ea);original.invoke(0x40bb53);memory.write(selected_save,packed>>16);memory.write(title_flow,load_selected);}
        return;
    }
    case load_selected:
        memory.random_state().crt_seed=memory.read(globals::frame_time_ms);
        if(!original.invoke(0x46118e,{memory.read(selected_save)})){memory.write(title_flow,menu);return;}
        memory.write(title_flow,playing);memory.write(0x803a4c,3);memory.write(globals::game_mode,0);intro_active_=false;return;
    case game_over_splash:
        // 460872 owns the SGO splash; it releases the screen to state9 on input.
        battle.tick_game_over();if(memory.read(title_flow)==post_menu)intro_active_=true;return;
    case post_menu:{
        memory.write(globals::current_map_id,0xffffffffu);memory.write(0x5d2298,0xffffffffu);
        const auto choice=original.invoke(0x4385e9);
        if(choice==0){
            memory.write(title_flow,post_load_picker);actors.reset_range(0,0x300);memory.write(0x804abc,memory.read(globals::frame_time_ms));
            original.invoke(0x431869);field_menu.open(8);
        }else if(choice==1){
            //460bdb: the second entry resumes slot0 after a defeat instead of
            // starting a new game, and rewinds the replay cursor to the start.
            if(!original.invoke(0x46118e,{0}))return; // Same guard as load_selected; the original ignores the result.
            memory.write(0x803a4c,1);memory.write(0x5d21e4,0);actors.reset_range(0,0x300);
            memory.write(0x804abc,memory.read(globals::frame_time_ms));
            memory.write(title_flow,playing);memory.write(globals::game_mode,3);intro_active_=false;
        }else if(choice==2){intro_active_=false;memory.write(globals::game_mode,0xffffffffu);}
        return;
    }
    case post_load_picker:{
        const auto packed=original.invoke(0x43955f),command=packed&65535;
        if(command==1){original.invoke(0x4394ea);memory.write(title_flow,post_menu);}
        else if(command==3){original.invoke(0x4394ea);original.invoke(0x40bb53);memory.write(selected_save,packed>>16);memory.write(title_flow,post_load_selected);}
        return;
    }
    case post_load_selected:
        if(!original.invoke(0x46118e,{memory.read(selected_save)})){memory.write(title_flow,post_menu);return;}
        //460c9e resumes the restored replay cursor at base+1 and lets the map
        // loader own the fade, unlike the title path's direct resource phase.
        memory.write(0x804658,0xffffffffu);memory.write(0x5d21e4,memory.read(0x5d21dc)+1);
        memory.write(0x803a4c,1);memory.write(0x804abc,memory.read(globals::frame_time_ms));
        if(memory.read(0x607cbc)==0xe6||memory.read(0x607cc0)==0xe6)memory.write(0x607cf4,0x13);
        memory.write(title_flow,playing);memory.write(globals::game_mode,3);intro_active_=false;return;
    default:throw Fault(title_flow,"unknown title flow state");
    }
}
} // namespace fsb::core
