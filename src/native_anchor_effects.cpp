#include "fsb_core/combat/anchor_effects.hpp"
#include "fsb_core/combat/effects.hpp"
#include "fsb_core/combat/object_motion.hpp"
#include "fsb_core/actor_core/motion_callbacks.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actor_fields.hpp"
namespace fsb::core {
bool RecoveredBattle::dispatch_anchor_effects(Address entry) {
    switch(entry){case 0x4624ad:case 0x46256a:case 0x4626ab:case 0x46274d:case 0x4629cb:case 0x462a49:case 0x462b50:case 0x4627eb:case 0x462c06:case 0x462cf4:case 0x4631a2:break;default:return false;}
    if(entry==0x4631a2){combat::ObjectMotion(memory_).polar_step(argument(0),argument(1),signed32(argument(2)));result(0,12);return true;}
    combat::AnchorEffectServices s;
    s.spawn=[this](Address cb){return callback(0x45d89c,{cb});};
    s.release=[this](Address object){callback(0x45d91d,{object});};
    s.start_script=[this](Address object,Address script){callback(0x447841,{object,script});};
    s.random=[this]{return crt_rand(memory_);};
    const auto words=actor_core::memory_words(memory_);
    s.oscillate=[&](Address object){actor_core::tick_oscillation(words,object,
        [&](std::uint32_t angle){return fixed_sin(memory_,angle);},
        [&](std::uint32_t angle){return fixed_cos(memory_,angle);});};
    combat::Effects effects(memory_);effects.take_object=s.spawn;
    s.spark_cluster=[&](Address object){effects.spawn_spark_cluster(object);};
    combat::AnchorEffects anchors(memory_,s);
    s.spawn_rising=[&](Address source){anchors.spawn_rising(source);};
    const auto object=argument(0);
    switch(entry){
    case 0x4627eb:anchors.scatter_cluster(object);break;
    case 0x462c06:anchors.spiral(object,false);break;
    case 0x462cf4:anchors.spiral(object,true);break;
    case 0x4624ad:anchors.tick_exploding_mark(object);break;
    case 0x46256a:anchors.emit_floating_marks(object);break;
    case 0x4626ab:anchors.spawn_rising(object);break;
    case 0x46274d:anchors.emit_rising(object);break;
    case 0x4629cb:anchors.tick_orbit(object);break;
    case 0x462a49:anchors.orbit_ring(object,false);break;
    case 0x462b50:anchors.orbit_ring(object,true);break;
    }
    result(0,4);return true;
}
}
