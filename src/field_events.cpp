#include "fsb_core/field.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/arena.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
Handle Field::tick_overlays(Map& map,Arena&){
    Handle activated=0;
    map_logic::MapObjects objects(memory_,map,{
        [this](unsigned cue){audio_.play_cue(cue);},
        [this,&activated]{
            if(!position_trigger)throw Fault(0x412181,"field position router is not connected");
            const auto trigger=position_trigger();
            if(trigger.activated)activated=trigger.primary_root;
            return trigger.activated!=0;
        }});
    namespace overlay_offset=map_logic::overlay_offset;
    for(Address slot=globals::overlay_effects;slot<globals::overlay_effects_end;slot+=map_logic::overlay_stride){
        constexpr auto live=map_logic::overlay_registered|map_logic::overlay_running;
        if((memory_.read(slot)&live)!=live)continue;
        const auto callback=memory_.read(slot+overlay_offset::callback);
        if(!objects.tick(callback,slot)){
            if(!overlay_tick)throw Fault(callback,"overlay callback is not connected");
            overlay_tick(callback,slot);
        }
        // 457c65 also increments a just-cleared slot.
        memory_.write(slot+overlay_offset::ticks,memory_.read(slot+overlay_offset::ticks)+1);
    }
    return activated;
}
} // namespace fsb::core
