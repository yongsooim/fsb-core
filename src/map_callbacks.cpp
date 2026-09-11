#include "fsb_core/map.hpp"
#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/map_logic/map_setup.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
Address Map::register_overlay(Rect rectangle,Address callback,unsigned flags){
    for(Address slot=globals::overlay_effects;slot<globals::overlay_effects_end;slot+=map_logic::overlay_stride){
        if(memory_.read(slot)&map_logic::overlay_registered)continue;
        const std::uint32_t values[]={flags|map_logic::overlay_registered,std::uint32_t(rectangle.left),std::uint32_t(rectangle.top),
                                      std::uint32_t(rectangle.right),std::uint32_t(rectangle.bottom),callback,0,0};
        for(unsigned i=0;i<8;++i)memory_.write(slot+i*4,values[i]);return slot;
    }
    throw Fault(callback,"overlay table exhausted");
}
bool Map::setup_field_overlays(Address callback){
    // The straight-line installers are a recovered table. The ones that also
    // spawn objects need the original allocator, so they go out through
    // setup_callback and land on their reconstruction from there.
    return map_logic::install_map_overlays(memory_,*this,callback);
}
} // namespace fsb::core
