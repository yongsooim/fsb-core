#include "fsb_core/combat/followup.hpp"
#include "fsb_core/recovered_battle.hpp"
namespace fsb::core {
bool RecoveredBattle::dispatch_followup(Address entry){
    switch(entry){case 0x461b11:case 0x461d82:break;default:return false;}
    combat::FollowupServices services;
    services.snapshot_motion=[this](Address actor){callback(0x45db53,{actor});};
    services.restore_motion=[this](Address actor){callback(0x45db6c,{actor});};
    services.attach_effect=[this](Address actor,std::int32_t kind){callback(0x4622c2,{actor,std::uint32_t(kind)});};
    services.clear_effects=[this](Address actor){callback(0x4623dd,{actor});};
    services.start_script=[this](Address actor,Address script){callback(0x447841,{actor,script});};
    combat::Followup flow(memory_,services);
    if(entry==0x461b11)flow.enqueue_wave(argument(0));else flow.prepare(argument(0));
    result(0,4);return true;
}
}
