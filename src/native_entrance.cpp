#include "fsb_core/combat/entrance.hpp"
#include "fsb_core/combat/markers.hpp"
#include "fsb_core/combat/status_visuals.hpp"
#include "fsb_core/combat/grid.hpp"
#include "fsb_core/combat/status.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/recovered_battle.hpp"
namespace fsb::core {
bool RecoveredBattle::dispatch_entrance(Address entry) {
    switch(entry){case 0x44c1fc:case 0x44c2f4:case 0x44c313:case 0x44c337:case 0x44c3b6:case 0x44c954:break;default:return false;}
    const auto place=[this](Address actor,std::uint32_t x,std::uint32_t y,std::uint32_t grid){set_actor_tile_position(memory_,actor,signed32(x),signed32(y),signed32(grid));};
    if(entry==0x44c954) {
        combat::AttachedEffectServices effects;
        effects.release=[this](Address object){callback(0x45d91d,{object});};
        combat::AttachedEffects registry(memory_,effects);
        const combat::StatusVisuals::Detach detach=[&](Address actor,combat::attached_effects::Kind kind){registry.detach(actor,std::int32_t(kind));};
        combat::StatusVisuals(memory_,detach).remove_expired();result(0);return true;
    }
    if(entry==0x44c2f4||entry==0x44c313||entry==0x44c337) {
        combat::MarkerServices s;s.place_actor=place;
        s.spawn=[this](Address cb){return callback(0x45d89c,{cb});};
        s.release=[this](Address object){callback(0x45d91d,{object});};
        combat::Markers markers(memory_,s);
        if(entry==0x44c337)result(markers.spawn(argument(0),argument(1),argument(2)),12);
        else {if(entry==0x44c2f4)markers.release_slot(argument(0));else markers.tick(argument(0));result(0,4);}
        return true;
    }
    combat::EntranceServices s;s.place_actor=place;
    s.select_music=[this]{return combat::Status(memory_).scene_track();};
    s.transition_music=[this](unsigned out,unsigned from,unsigned track,unsigned start,unsigned in,unsigned volume){callback(0x4337f4,{out,from,track,start,in,volume});};
    s.clear_timer=[this](Address at){memory_.write(at,0);};s.random=[this]{return crt_rand(memory_);};
    s.move_focus=[this](unsigned from,unsigned to,unsigned ticks,bool enabled){callback(0x4544cf,{from,to,ticks,unsigned(enabled)});};
    s.focus=[this](unsigned to,unsigned ticks){callback(0x45451c,{to,ticks});};
    s.mark_entrance=[this]{combat::Grid(memory_).mark_entrance_actor();};
    combat::Entrance entrance(memory_,s);
    s.activate_enemy=[&](unsigned enemy,bool refresh){entrance.activate_enemy(enemy,refresh);};
    if(entry==0x44c1fc)result(entrance.activate_enemy(argument(0),(argument(1)&255)!=0),8);
    else {entrance.tick((argument(0)&255)!=0,(argument(1)&255)!=0);result(0,8);}
    return true;
}
}
