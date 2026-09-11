#include "fsb_core/combat/attached_effects.hpp"
#include "fsb_core/recovered_battle.hpp"

namespace fsb::core {
bool RecoveredBattle::dispatch_attached_effects(Address entry){
    switch(entry){case 0x4622c2:case 0x462370:case 0x4623dd:case 0x462452:break;default:return false;}
    combat::AttachedEffectServices services;
    services.spawn=[this](Address cb){return callback(0x45d89c,{cb});};
    services.release=[this](Address object){callback(0x45d91d,{object});};
    services.report=[this](Address message){callback(0x401ad8,{message});};
    combat::AttachedEffects effects(memory_,services);
    switch(entry){
    case 0x4622c2:result(effects.attach(argument(0),signed32(argument(1))),8);break;
    case 0x462370:result(effects.detach(argument(0),signed32(argument(1))),8);break;
    case 0x4623dd:result(effects.clear_owner(argument(0)),4);break;
    case 0x462452:effects.clear_all();result(1);break;
    }
    return true;
}
} // namespace fsb::core
