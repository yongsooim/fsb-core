#include "fsb_core/effect_script.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
using effect_script::Command;
void EffectScript::stop(Address actor) {
    memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)&~effect_script::running_flag);
    memory_.write(actor+effect_script::script,0);
    memory_.write(actor+effect_script::wait,0);
    memory_.write(actor+effect_script::cursor,0);
}
void EffectScript::start(Address actor,Address script) {
    memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)|effect_script::running_flag|effect_script::visible_flag);
    memory_.write(actor+effect_script::wait,0);
    memory_.write(actor+effect_script::cursor,0);
    memory_.write(actor+effect_script::script,script);
    run(actor,false); //447841 executes immediately, without consuming a wait tick.
}
void EffectScript::run(Address actor,bool honor_wait) {
    memory_.write(effect_script::yielded,0);
    while(!memory_.read(effect_script::yielded)) {
        const auto remaining=memory_.read(actor+effect_script::wait);
        if(honor_wait&&remaining) {
            memory_.write(effect_script::yielded,1);
            memory_.write(actor+effect_script::wait,remaining-1);
        } else {
            const auto pc=memory_.read(actor+effect_script::script)+memory_.read(actor+effect_script::cursor);
            execute(actor,static_cast<Command>(memory_.read(pc,1)));
        }
    }
}
void EffectScript::tick() {
    //4478ba owns the global scan. Each of the768 slots is visited once.
    for(unsigned index=0;index<capacity::actor_slots;++index) {
        const auto actor=Actors::slot(index);
        if((memory_.read(actor+actor_offset::flags)&effect_script::running_flag)&&memory_.read(actor+effect_script::script))run(actor,true);
    }
}
void EffectScript::next(Address actor) {
    const auto offset=memory_.read(actor+effect_script::cursor);
    const auto pc=memory_.read(actor+effect_script::script)+offset;
    memory_.write(actor+effect_script::cursor,offset+memory_.read(pc+1,1));
}
void EffectScript::wait(Address actor,unsigned frames) {
    memory_.write(actor+effect_script::wait,frames);
    if(frames)memory_.write(effect_script::yielded,1);
}
Address EffectScript::notify_controller(Address actor,std::int32_t state) {
    auto controller=memory_.read(effect_script::controller);
    if(!controller||!memory_.read(controller+actor_offset::callback))return controller;
    if(!invoke_actor)throw Fault(controller,"effect controller callback service is not attached");
    memory_.write(controller+actor_offset::callback_argument,actor);
    controller=memory_.read(effect_script::controller);
    const auto previous=memory_.read(controller+actor_offset::callback_state);
    memory_.write(controller+actor_offset::callback_state,std::uint32_t(state));
    controller=memory_.read(effect_script::controller);
    invoke_actor(memory_.read(controller+actor_offset::callback),controller);
    // A callback may switch the active controller or publish a different state.
    controller=memory_.read(effect_script::controller);
    if(memory_.read(controller+actor_offset::callback_state)==std::uint32_t(state))memory_.write(controller+actor_offset::callback_state,previous);
    controller=memory_.read(effect_script::controller);
    memory_.write(controller+actor_offset::callback_argument,0);
    return controller;
}
void EffectScript::execute(Address actor,Command command) {
    const auto pc=memory_.read(actor+effect_script::script)+memory_.read(actor+effect_script::cursor);
    const auto word=[&](unsigned offset){return memory_.read(pc+offset,2);};
    switch(command) {
    case Command::Stop:
        stop(actor);memory_.write(effect_script::yielded,1);return;
    case Command::Restart:
        memory_.write(actor+effect_script::cursor,0);return;
    case Command::SaveAppearance:case Command::RestoreAppearance: {
        const auto from=command==Command::SaveAppearance?actor_offset::sprite_base:effect_script::saved_appearance;
        const auto to=command==Command::SaveAppearance?effect_script::saved_appearance:actor_offset::sprite_base;
        for(unsigned lane=0;lane<3;++lane)memory_.write(actor+to+lane*4,memory_.read(actor+from+lane*4));
        break;
    }
    case Command::SetCharacterAnimation:case Command::SetIndexedAnimation:case Command::SetEffectAnimation: {
        const auto bank=command==Command::SetCharacterAnimation?sprite_bank_kind::character:
            command==Command::SetIndexedAnimation?sprite_bank_kind::indexed:sprite_bank_kind::active_effect;
        memory_.write(actor+actor_offset::sprite_selector,word(2)|(bank<<16));
        memory_.write(actor+actor_offset::sprite_frame,memory_.read(pc+4));
        wait(actor,word(8));break;
    }
    case Command::SetFrame:
        memory_.write(actor+actor_offset::sprite_frame,memory_.read(pc+2));wait(actor,word(6));break;
    case Command::Wait:wait(actor,word(2));break;
    case Command::SaveTransform:case Command::RestoreTransform: {
        const auto from=command==Command::SaveTransform?actor_offset::world_x:effect_script::saved_transform;
        const auto to=command==Command::SaveTransform?effect_script::saved_transform:actor_offset::world_x;
        const auto bytes=memory_.bytes(actor+from,effect_script::transform_bytes);
        std::copy(bytes.begin(),bytes.end(),memory_.span(actor+to,bytes.size()).begin());break;
    }
    case Command::TranslatePosition:case Command::TranslateDrawOffset: {
        const auto base=command==Command::TranslatePosition?actor_offset::world_x:actor_offset::draw_offset_x;
        for(unsigned lane=0;lane<3;++lane) {
            const auto field=actor+base+lane*4;
            memory_.write(field,memory_.read(field)+(word(2+lane*2)<<16));
        }
        wait(actor,word(8));break;
    }
    case Command::AdvanceController: {
        const auto field=memory_.read(effect_script::controller)+actor_offset::callback_state;
        memory_.write(field,memory_.read(field)+1);break;
    }
    case Command::NotifyController:notify_controller(actor,std::int16_t(word(2)));break;
    case Command::SetFlags:
        memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)|memory_.read(pc+2));break;
    case Command::ClearFlags:
        memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)&~memory_.read(pc+2));break;
    case Command::PlayCue:case Command::StopCue: {
        const auto& invoke=command==Command::PlayCue?play_cue:stop_cue;
        if(!invoke)throw Fault(pc,"effect sound cue service is not attached");
        invoke(word(2));break;
    }
    case Command::SetTimingMode:memory_.write(effect_script::timing_mode,word(2));break;
    default:throw Fault(pc,"unknown effect script command");
    }
    next(actor);
}
}
