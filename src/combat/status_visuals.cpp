#include "fsb_core/combat/status_visuals.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/symbols.hpp"
namespace fsb::core::combat {
namespace { constexpr Address status_offset=8, enemy_count=0x776484; }
void StatusVisuals::remove_row(Address actor,Address status,bool enemy) {
    if(memory_.read(status)&battle_status::unavailable)return;
    constexpr unsigned masks[]={battle_status::poison,battle_status::sleep,battle_status::silence,
        battle_status::paralysis,battle_status::curse,battle_status::lucky_march,battle_status::damage_guard,
        battle_status::reflect,battle_status::power_samba,battle_status::hiphop};
    // Enemy bit0x40000 keeps the existing paralysis visual alive; its own
    // gameplay meaning/producer has not been established.
    constexpr unsigned shared_visual_keepalive=0x40000;
    for(unsigned i=0;i<(enemy?5u:10u);++i) {
        const auto mask=masks[i]|((enemy&&i==3)?shared_visual_keepalive:0);
        if(!(memory_.read(status)&mask))detach_(actor,attached_effects::Kind(i+1));
    }
}
void StatusVisuals::remove_expired() {
    for(std::uint32_t i=0;signed32(i)<signed32(memory_.read(globals::party_count));++i) {
        const auto record=BattleRules::party_record(memory_.read(globals::party_actor_ids+i*4));
        remove_row(globals::actor_objects+i*layout::actor_size,record+status_offset,false);
    }
    for(std::uint32_t i=0;signed32(i)<signed32(memory_.read(enemy_count));++i) {
        const auto record=BattleRules::enemy_record(i);
        remove_row(globals::actor_objects+memory_.read(record)*layout::actor_size,record+status_offset,true);
    }
}
}
