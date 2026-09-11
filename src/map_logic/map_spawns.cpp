#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/map_logic/map_setup.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::map_logic {
namespace {
constexpr unsigned object_map_id=0x160;
constexpr std::uint32_t drawn_flag=0x40,pushable_flags=0x4f;
constexpr std::uint32_t one_tile_q16=0x10000;
constexpr std::uint8_t occupied_bit=2;
}
Address MapObjects::take_object(Address callback,std::uint32_t extra_flags,std::uint32_t selector,std::uint32_t frame,
                                std::int32_t x,std::int32_t y,std::int32_t z){
    if(!services_.allocate)throw Fault(0x45d89c,"map object allocator is not connected");
    const auto object=services_.allocate(callback);
    memory_.write(object+actor_offset::flags,memory_.read(object+actor_offset::flags)|extra_flags);
    memory_.write(object+actor_offset::sprite_base,map_object_sheet);
    memory_.write(object+actor_offset::sprite_selector,selector);
    memory_.write(object+actor_offset::sprite_frame,frame);
    memory_.write(object+object_map_id,memory_.read(globals::current_map_id));
    set_actor_tile_position(memory_,object,x,y,z);
    return object;
}
Address MapObjects::spawn_pushable(std::int32_t x,std::int32_t y,std::int32_t z){
    // Flags 0x4f also marks it solid and pushable, not just drawn.
    const auto object=take_object(0x4897a3,pushable_flags,pushable_selector,0,x,y,z);
    memory_.write(object+actor_offset::motion_state,0);
    return object;
}
void MapObjects::spawn_conveyor_pair(){
    // Both halves park two tiles off the top of the map until the party steps on
    // a belt cell. The rider draws one tile above it, the shadow one below and a
    // layer up so it sorts behind.
    constexpr std::int32_t x=0,y=-2,z=0;
    const auto rider=take_object(0x48897a,drawn_flag,conveyor_selector,2,x,y,z);
    for(auto field:{actor_offset::motion_frame,actor_offset::frame_group,actor_offset::facing,actor_offset::motion_state})
        memory_.write(rider+field,0);
    memory_.write(rider+actor_offset::world_y,memory_.read(rider+actor_offset::world_y)-one_tile_q16);
    const auto shadow=take_object(0x488aa7,drawn_flag,conveyor_selector,0,x,y,z);
    for(auto field:{actor_offset::motion_frame,actor_offset::frame_group,actor_offset::facing,actor_offset::motion_state})
        memory_.write(shadow+field,0);
    memory_.write(shadow+actor_offset::world_y,memory_.read(shadow+actor_offset::world_y)+one_tile_q16);
    memory_.write(shadow+actor_offset::layer_q16,memory_.read(shadow+actor_offset::layer_q16)+1);
}
void MapObjects::spawn_gate_pair(std::int32_t x,std::int32_t y,std::int32_t z){
    const auto front=take_object(0x489a60,drawn_flag,gate_piece_selector,3,x,y,z);
    for(auto field:{actor_offset::motion_frame,actor_offset::facing,actor_offset::motion_state})
        memory_.write(front+field,0);
    memory_.write(front+actor_offset::world_y,memory_.read(front+actor_offset::world_y)-one_tile_q16);
    const auto back=take_object(0x489b53,drawn_flag,gate_piece_selector,0xb,x,y,z);
    for(auto field:{actor_offset::motion_frame,actor_offset::facing,actor_offset::motion_state})
        memory_.write(back+field,0);
    memory_.write(back+actor_offset::world_y,memory_.read(back+actor_offset::world_y)+one_tile_q16);
    memory_.write(back+actor_offset::layer_q16,memory_.read(back+actor_offset::layer_q16)+1);
    // The gate's own cell blocks movement until it is pushed away.
    const auto cell=std::uint32_t(z)*capacity::map_cells+std::uint32_t(y)*memory_.read(globals::grid_row_stride)+std::uint32_t(x);
    const auto attribute=globals::tile_attributes+cell*4+1;
    memory_.write(attribute,memory_.read(attribute,1)|occupied_bit,1);
}
void MapObjects::spawn_platform_pair(){
    const auto platform=take_object(0x488737,drawn_flag,platform_selector,2,0,-2,0);
    memory_.write(platform+actor_offset::motion_frame,0xffffffffu);
    memory_.write(platform+actor_offset::frame_group,1);
    memory_.write(platform+actor_offset::facing,0);
    memory_.write(platform+actor_offset::motion_state,0);
    memory_.write(platform+actor_offset::world_y,memory_.read(platform+actor_offset::world_y)-one_tile_q16);
    // Two tracks three tiles wide: the upper one lifts, the lower one lowers.
    const auto id=memory_.read(platform);
    const auto track=[&](int row,Address handler,std::uint32_t flags){
        const auto slot=map_.register_overlay({8,row,10,row},handler,flags);
        memory_.write(globals::current_overlay_effect,slot);
        memory_.write(slot+overlay_offset::patch_id,id);
    };
    track(6,0x4886b4,0x402);
    track(7,0x488631,0x401);
}
void MapObjects::spawn_switch(unsigned id){
    const auto record=switch_records+id*switch_record_stride;
    const auto trigger_x=memory_.read(record+switch_record_offset::trigger_x);
    const auto trigger_y=memory_.read(record+switch_record_offset::trigger_y);
    const auto raised=(memory_.read(switch_raised_bits)&(1u<<(id&31)))!=0;
    if(raised){
        // Already lifted: the marker tile above the trigger shows it, and the
        // block starts at the open target instead of the closed one.
        const auto marker=globals::map_tile_codes+((trigger_y-1)*memory_.read(globals::map_layer_width)+trigger_x)*4;
        memory_.write(marker,memory_.read(marker)+1);
    }
    const auto corner=raised?switch_record_offset::open_x:switch_record_offset::closed_x;
    const auto block=spawn_pushable(signed32(memory_.read(record+corner)),signed32(memory_.read(record+corner+4)),0);
    memory_.write(record+switch_record_offset::linked_object,block);
    memory_.write(block+actor_offset::path_commands,id,2);
    const auto slot=map_.register_overlay({signed32(trigger_x),signed32(trigger_y),signed32(trigger_x),signed32(trigger_y)},0x487122,0x10f);
    memory_.write(globals::current_overlay_effect,slot);
    memory_.write(slot+overlay_offset::patch_id,memory_.read(record+switch_record_offset::id));
    memory_.write(slot+overlay_offset::scratch,memory_.read(block));
}
void MapObjects::spawn_switch_range(unsigned first,unsigned last){
    // Every record drops its previous block before the range is respawned.
    for(auto record=switch_records+switch_record_offset::linked_object;
        record<0x5f87ecu;record+=switch_record_stride)memory_.write(record,0);
    for(unsigned id=first;id<last;++id)spawn_switch(id);
}
void MapObjects::install_gate_crossing(unsigned crossing){
    memory_.write(active_gate_crossing,crossing);
    const auto position=gate_positions+crossing*8;
    spawn_gate_pair(signed32(memory_.read(position)),signed32(memory_.read(position+4)),0);
    // Two of the four crossings register their own triggers after the gate.
    static constexpr OverlayRegistration first_crossing[]={
        {6,0x29,6,0x29,0x486dfb,0x10f,true,0x26,0,0,0x25,0xb},
        {6,0x29,6,0x29,0x486dfb,0x10f,true,0x27,0,0,0x25,0xb},
        {6,0x29,6,0x29,0x486fab,0x10f,true,0x25,0,0,0x14c,0xd},
        {0,0x2c,0x3f,0x2c,0x486b8c,0x20f,false,0,0,0,0,0}};
    static constexpr OverlayRegistration second_crossing[]={{0x21,0x20,0x21,0x20,0x486bfe,0x804,false,0,0,0,0,0}};
    if(!crossing)replay_registrations(memory_,map_,first_crossing,4);
    else if(crossing==1)replay_registrations(memory_,map_,second_crossing,1);
}
} // namespace fsb::core::map_logic
