#include "fsb_core/combat/handler_lifecycle.hpp"
#include "fsb_core/recovered_battle.hpp"

namespace fsb::core {
bool RecoveredBattle::dispatch_handler_lifecycle(Address entry) {
    switch(entry){case 0x461854:case 0x461903:case 0x46196c:case 0x461a4f:case 0x461a91:break;default:return false;}
    combat::HandlerLifecycleServices s;
    s.spawn=[this](Address cb){return callback(0x45d89c,{cb});};
    s.report_failure=[this](Address message){callback(0x401ad8,{message});};
    s.show_banner=[this](unsigned action){callback(0x464494,{action});};
    s.start_script=[this](Address actor,Address script){callback(0x447841,{actor,script});};
    s.show_number=[this](Address actor,std::int32_t value){callback(0x4646be,{actor,std::uint32_t(value)});};
    s.release=[this](Address object){callback(0x45d91d,{object});};
    combat::HandlerLifecycle flow(memory_,s);
    switch(entry){
    case 0x461854:result(flow.start_player(argument(0),argument(1)),8);break;
    case 0x461903:result(flow.start_enemy(argument(0)),4);break;
    case 0x46196c:flow.tick_status_delta(argument(0));result(0,4);break;
    case 0x461a4f:result(flow.queue_status_delta());break;
    case 0x461a91:result(flow.poll_completion());break;
    }
    return true;
}
} // namespace fsb::core
