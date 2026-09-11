#pragma once
#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::lab {
inline unsigned player_step_for_planning(const core::Memory& memory,const core::Actors& actors,
                                        core::Address actor,unsigned direction){
    using namespace core;
    const auto result=actors.step_attribute(actor,direction);
    if(direction<2||direction>3||(result&3))return result;
    //45595d's script probe omits horizontal height connectors. The actual
    //458ed7 player branches45a55a/45aa29 allow them. Keep the original probe
    //unchanged and include the player's extra edge only in input planning.
    const int x=signed32(memory.read(actor+actor_offset::world_x))/units::tile_width_q16;
    const int y=signed32(memory.read(actor+actor_offset::world_y))/units::tile_height_q16;
    const int z=signed32(memory.read(actor+actor_offset::layer_q16))/units::q16_one;
    const int width=signed32(memory.read(globals::grid_row_stride)),nx=x+(direction==2?-1:1);
    if(nx<0||nx>=width||y<0||y>=signed32(memory.read(globals::grid_height))||z<0||z>1)return result;
    const auto row=unsigned(z)*capacity::map_cells+unsigned(y*width);
    const auto center=memory.read(globals::tile_attributes+(row+unsigned(x))*4);
    const auto neighbor=memory.read(globals::tile_attributes+(row+unsigned(nx))*4);
    if(!(center&0x1000)||(center&(0x20u<<direction))||!(neighbor&0x200))return result;
    const auto link=(center>>26)&7;
    const auto height=link==direction?4u:memory.read(tables::opposite_directions+link*4)==direction?8u:0u;
    return height?(direction<<8)|(result&16u)|height|1u:result;
}
} // namespace fsb::lab
