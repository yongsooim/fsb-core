#include "fsb_core/combat/markers.hpp"
#include "fsb_core/symbols.hpp"
namespace fsb::core::combat {
Address Markers::spawn(std::uint32_t x,std::uint32_t y,std::uint32_t grid) {
    for(unsigned i=0;i<markers::capacity;++i) {
        const auto slot=markers::slots+i*4;
        if(memory_.read(slot))continue;
        const auto object=services_.spawn(markers::callback);
        // Original allocation failure faults at this first write; never publish a null marker.
        memory_.write(object+actor_offset::sprite_frame,0);
        memory_.write(object+actor_offset::facing,0);
        memory_.write(object+actor_offset::motion_state,0);
        memory_.write(object+actor_offset::sprite_base,markers::sprite);
        memory_.write(object+markers::slot_index,i);
        services_.place_actor(object,x,y,grid);
        memory_.write(object+actor_offset::tile_y,y);memory_.write(object+actor_offset::tile_x,x);
        memory_.write(slot,object);return object;
    }
    return 0;
}
void Markers::release_slot(unsigned index) {
    const auto slot=markers::slots+index*4;
    services_.release(memory_.read(slot));memory_.write(slot,0);
}
void Markers::tick(Address object) {
    if(memory_.scene_state().game_mode==markers::battle_mode)return;
    memory_.write(markers::slots+memory_.read(object+markers::slot_index)*4,0);
    services_.release(object);
}
}
