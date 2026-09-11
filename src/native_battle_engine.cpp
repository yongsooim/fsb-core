#include "fsb_core/combat/engine.hpp"
#include "fsb_core/combat/flow.hpp"
#include "fsb_core/combat/grid.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/recovered_battle.hpp"

namespace fsb::core {
bool RecoveredBattle::dispatch_battle_engine(Address entry) {
    switch(entry){case 0x44d45a:break;default:return false;}
    BattleRules rules(memory_);
    combat::EngineServices s;
    s.decay_status=[&]{rules.decay_status();};
    s.advance_gauges=[&]{rules.advance_gauges();};
    s.prepare_vitality=[&]{rules.prepare_vitality();};
    s.commit_vitality=[&]{rules.commit_vitality();};
    s.snapshot_party=[&](bool active){rules.snapshot_party_panel(active);};
    s.snapshot_enemy=[&](bool active){rules.snapshot_enemy_panel(active);};
    s.precheck=[&]{return rules.precheck();};
    s.select_next=[&]{const auto next=rules.next_context();memory_.write(combat::turn::active_slot,next.value_or(0xffffffffu));return next.has_value();};
    s.move_focus=[this](unsigned from,unsigned to,unsigned ticks,bool enabled){callback(0x4544cf,{from,to,ticks,unsigned(enabled)});};
    s.has_status=[&](unsigned slot,unsigned mask){return (rules.status(slot)&mask)!=0;};
    s.poison_delta=[&](unsigned slot){return rules.take_poison_delta(slot);};
    s.apply_delta=[&](unsigned slot,std::uint32_t delta){rules.apply_delta(slot,delta);};
    s.queue_status=[this]{callback(0x461a4f,{});};
    s.activate_context=[this]{callback(0x44d17b,{});};
    s.poll_handler=[this]{return callback(0x461a91,{})!=0;};
    s.finish_followup=[this]{return combat::Flow(memory_).finish_pending_steps();};
    s.prepare_followup=[this](combat::followup::Stage stage){callback(0x461d82,{unsigned(stage)});};
    s.enqueue_wave=[this](unsigned wave){callback(0x461b11,{wave});};
    s.start_player=[this](unsigned mode,unsigned action){callback(0x461854,{mode,action});};
    s.start_enemy=[this](unsigned action){callback(0x461903,{action});};
    s.invalidate_panel=[this]{callback(0x40bb53,{});};
    s.clear_command=[this]{callback(0x4394ea,{});};
    s.select_ai_followup=[this]{callback(0x44e7e5,{});};
    s.apply_status_icons=[this]{callback(0x44c954,{});};
    s.menu_result=[this]{return callback(0x43955f,{});};
    s.clear_cursor=[&](std::uint32_t x,std::uint32_t y,unsigned radius,unsigned keep){rules.clear_cursor(signed32(x),signed32(y),int(radius),keep);};
    s.default_handler=[&](unsigned slot,bool secondary){return rules.default_handler(slot,secondary);};
    s.prepare_handler=[&](unsigned handler,unsigned facing){rules.prepare_handler(handler,facing);};
    s.track_action=[this](std::uint32_t x,std::uint32_t y,bool direct){callback(0x451746,{x,y,unsigned(direct)});};
    s.mark_entrance=[this]{combat::Grid(memory_).mark_entrance_actor();};
    s.retire_entrance=[this]{callback(0x44ab2e,{});};
    combat::Engine(memory_,s).tick();result(0);return true;
}
}
