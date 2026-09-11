#include "fsb_core/combat/turn_control.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/combat/status.hpp"
#include "fsb_core/recovered_battle.hpp"

namespace fsb::core {
bool RecoveredBattle::dispatch_turn_control(Address entry) {
    switch(entry){case 0x44ceb2:case 0x44cfe2:case 0x44d17b:case 0x461c66:break;default:return false;}
    BattleRules rules(memory_);
    combat::TurnControlServices s;
    s.occupant=[this](auto x,auto y){return combat::Status(memory_).actor_at_tile(signed32(x),signed32(y)).value_or(0xffffffffu);};
    s.passable=[this](auto x,auto y,combat::turn::Edge edge,unsigned mode){return std::uint8_t(callback(0x44956a,{x,y,unsigned(edge),mode}));};
    s.default_action=[&](unsigned slot,bool secondary){return rules.default_action(slot,secondary);};
    s.default_handler=[&](unsigned slot,bool secondary){return rules.default_handler(slot,secondary);};
    s.prepare_handler=[&](unsigned handler,unsigned facing){rules.prepare_handler(handler,facing);};
    s.load_menu_assets=[this](unsigned set){callback(0x43934c,{set});};
    s.open_menu=[this](unsigned slot,unsigned mode){callback(0x439701,{slot,mode});};
    s.flood_costs=[this](const combat::turn::MovementArea& a){callback(0x4490c7,{a.grid,a.bounds[0],a.bounds[1],a.bounds[2],a.bounds[3],a.x,a.y,a.marker});};
    s.mark_reachable=[&](std::int32_t budget){rules.mark_reachable(budget);};
    s.enemy_turn=[this](Address actor){callback(0x453b8d,{actor});};
    s.release=[this](Address actor){callback(0x45d91d,{actor});};
    combat::TurnControl control(memory_,s);
    switch(entry){
    case 0x44ceb2:result(control.has_adjacent_enemy(argument(0),argument(1)),8);break;
    case 0x44cfe2:control.begin_action(argument(0));result(0,4);break;
    case 0x44d17b:control.activate_context();result(0);break;
    case 0x461c66:control.tick_defeated_enemy(argument(0));result(0,4);break;
    }
    return true;
}
}
