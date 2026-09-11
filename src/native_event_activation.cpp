#include "fsb_core/event_activation.hpp"
#include "fsb_core/recovered_battle.hpp"

namespace fsb::core {
bool RecoveredBattle::dispatch_event_activation(Address entry) {
    if(entry!=0x4120c7 && entry!=0x412147 && entry!=0x4138ee)return false;
    EventActivationServices services;
    services.clone_definition=[this](Address definition,unsigned pool){return callback(0x41a00b,{definition,pool});};
    services.player_actor=[this]{return callback(0x457e56);};
    services.set_actor_state=[this](Handle actor,unsigned state){callback(0x4300c7,{actor,state});};
    services.actor_object=[this](Handle actor){return callback(0x42feb9,{actor});};
    services.player_actor_object=[this](Handle actor){return callback(0x457ec9,{actor});};
    services.set_object_state=[this](Address actor,unsigned state){callback(0x430059,{actor,state});};
    services.runtime_object=[this](Handle handle){return callback(0x4026c1,{handle});};
    services.inline_dialog=[this](Handle actor,Address markup,std::int32_t facing){return callback(0x4139b6,{actor,markup,std::uint32_t(facing)});};
    services.missing_definition=[this](unsigned id){callback(0x401a02,{0x57fdb8,id});};
    services.request_event=[this](unsigned id,std::uint32_t trigger){return callback(0x412017,{id,trigger});};
    services.alternate_visuals=[this]{callback(0x44e085);};
    EventActivation events(memory_,services);
    switch(entry) {
    case 0x4120c7:result(events.spawn_object(argument(0),argument(1),argument(2)),12);break;
    case 0x412147:result(events.spawn_player_dialog(argument(0)),4);break;
    case 0x4138ee:result(events.resume_pending());break;
    }
    return true;
}
} // namespace fsb::core
