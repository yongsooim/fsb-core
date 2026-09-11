#pragma once
#include "actors.hpp"
#include "audio.hpp"
#include <functional>

namespace fsb::core {
class Map;
class Arena;
struct FieldEventTrigger {bool activated=false;Handle primary_root=0;};
bool mark_field_triggers(Memory& memory,int x,int y,unsigned kind,unsigned direction);
class Field {
public:
    Field(Memory& memory,Actors& actors,Audio& audio):memory_(memory),actors_(actors),audio_(audio){}
    void tick_actor(Address actor);
    bool mark_triggers(int x,int y,unsigned kind,unsigned direction);
    Handle tick_overlays(Map& map,Arena& arena);
    std::function<void(Address)> interact;
    std::function<FieldEventTrigger()> position_trigger;
    std::function<void(Address,Address)> overlay_tick;
private:
    Memory& memory_;Actors& actors_;Audio& audio_;
    void probe_action(Address actor);
};
} // namespace fsb::core
