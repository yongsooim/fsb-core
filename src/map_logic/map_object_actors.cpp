#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/map_logic/map_patch_table.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::map_logic {
namespace {
constexpr unsigned object_map_id=0x160;   // The map the object was spawned for.
constexpr std::int32_t step_frames=9;     // A pushed block crosses one tile in nine frames.
constexpr std::uint32_t half_tile_q16=0x8000;
constexpr std::uint32_t blocked_bit=0x200;
constexpr std::uint8_t occupied_bit=2;
}
bool MapObjects::still_on_this_map(Address object){
    // A negative callback state means the object is already being torn down.
    if(signed32(memory_.read(object+actor_offset::callback_state))<0)return false;
    if(memory_.read(object+object_map_id)==memory_.read(globals::current_map_id))return true;
    if(!services_.despawn)throw Fault(0x45d91d,"map object despawn is not connected");
    services_.despawn(object);return false;
}
void MapObjects::publish_tile(Address object){
    memory_.write(object+actor_offset::tile_x,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_x_q16))/units::q16_one));
    memory_.write(object+actor_offset::tile_y,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_y_q16))/units::q16_one));
}
Address MapObjects::party_actor()const{return Actors::slot(memory_.read(globals::active_party_index));}
void MapObjects::tick_map_marker(Address object){
    const auto state=memory_.read(object+actor_offset::callback_state);
    // -1 claims the marker for the current map;0 checks it still belongs here.
    if(signed32(state)==-1)memory_.write(object+object_map_id,memory_.read(globals::current_map_id));
    else if(!state&&memory_.read(object+object_map_id)!=memory_.read(globals::current_map_id)){
        if(!services_.despawn)throw Fault(0x45d91d,"map object despawn is not connected");
        services_.despawn(object);
    }
}
void MapObjects::tick_pushable(Address object){
    if(!still_on_this_map(object))return;
    if(memory_.read(object+actor_offset::motion_state)==1){
        // Walk the step command 0x4870e2 left behind, in tile fixed point.
        const auto corner=[&](unsigned at){return memory_.read(object+actor_offset::path_commands+at,2)<<16;};
        const auto from_x=corner(2),from_y=corner(4),to_x=corner(6),to_y=corner(8);
        const auto step=memory_.read(object+actor_offset::motion_frame)+1;
        memory_.write(object+actor_offset::motion_frame,step);
        const auto along=[&](std::uint32_t from,std::uint32_t to){
            return std::uint32_t(signed32(std::uint32_t(to-from)*step)/step_frames)+from+half_tile_q16;
        };
        memory_.write(object+actor_offset::tile_x_q16,along(from_x,to_x));
        memory_.write(object+actor_offset::tile_y_q16,along(from_y,to_y));
        if(!services_.world_from_tile)throw Fault(0x45d82d,"tile to world conversion is not connected");
        services_.world_from_tile(object+actor_offset::world_x,object+actor_offset::tile_x_q16);
        if(signed32(step)==step_frames)memory_.write(object+actor_offset::motion_state,0);
    }
    publish_tile(object);
    // Both ends of the block's switch record stop blocking; the cell it now
    // stands on starts blocking.
    const auto record=switch_records+memory_.read(object+actor_offset::path_commands,2)*switch_record_stride;
    const auto width=memory_.read(globals::map_layer_width);
    for(auto corner:{switch_record_offset::open_x,switch_record_offset::closed_x}){
        const auto attribute=globals::tile_attributes+(memory_.read(record+corner+4)*width+memory_.read(record+corner))*4;
        memory_.write(attribute,memory_.read(attribute)&~blocked_bit);
    }
    const auto here=globals::tile_attributes
        +(memory_.read(object+actor_offset::tile_y)*width+memory_.read(object+actor_offset::tile_x))*4+1;
    memory_.write(here,memory_.read(here,1)|occupied_bit,1);
}
void MapObjects::tick_gate_front_piece(Address object){tick_gate_piece(object,false);}
void MapObjects::tick_gate_back_piece(Address object){tick_gate_piece(object,true);}
void MapObjects::tick_gate_piece(Address object,bool back){
    if(!still_on_this_map(object))return;
    const auto party=party_actor();
    if(memory_.read(party+actor_offset::motion_state)==pushing_state){
        const auto frame=memory_.read(object+actor_offset::motion_frame)+1;
        memory_.write(object+actor_offset::motion_frame,frame);
        const auto facing=memory_.read(party+actor_offset::facing);
        memory_.write(object+actor_offset::facing,facing);
        // Two frames per facing, and the back half sits eight further along.
        memory_.write(object+actor_offset::sprite_frame,
            memory_.read(gate_piece_frames+facing*4)+(frame&1)*4+(back?8u:0u));
    }
    const auto state=memory_.read(party+actor_offset::motion_state);
    if(state==pushing_state||state==pushing_settle_state){
        memory_.write(object+actor_offset::world_x,memory_.read(party+actor_offset::world_x));
        memory_.write(object+actor_offset::world_y,memory_.read(party+actor_offset::world_y)
            +(back?std::uint32_t(units::q16_one):0u-std::uint32_t(units::q16_one)));
        // The halves ride two thirds of the way up the party's own step.
        memory_.write(object+actor_offset::elevation,
            std::uint32_t(signed32(memory_.read(party+actor_offset::elevation)*2)/3));
    }
    publish_tile(object);
}
bool MapObjects::on_conveyor_cell()const{
    const auto party=Actors::slot(memory_.read(globals::active_party_index));
    const auto cell=memory_.read(party+actor_offset::tile_y)*memory_.read(globals::map_layer_width)
                   +memory_.read(party+actor_offset::tile_x)
                   +(std::uint32_t(signed32(memory_.read(party+actor_offset::layer_q16))/units::q16_one)<<12);
    return (memory_.read(globals::tile_attributes+cell*4)&surface_mask)==conveyor_surface;
}
void MapObjects::tick_conveyor_rider(Address object){
    if(!still_on_this_map(object))return;
    if(!on_conveyor_cell())memory_.write(object+actor_offset::elevation,parked_elevation);
    else{
        const auto party=party_actor();
        // The step sound draws from the shared stream and throws the value away,
        // so the draw has to happen anyway to keep the order.
        if(memory_.read(party+actor_offset::motion_frame)==1){crt_rand(memory_);play(conveyor_cue);}
        const auto frame=memory_.read(object+actor_offset::motion_frame)+1;
        memory_.write(object+actor_offset::motion_frame,frame);
        memory_.write(object+actor_offset::facing,memory_.read(party+actor_offset::facing));
        memory_.write(object+actor_offset::sprite_frame,std::uint32_t((signed32(frame)/4)&1)+2);
        memory_.write(object+actor_offset::world_x,memory_.read(party+actor_offset::world_x));
        memory_.write(object+actor_offset::world_y,memory_.read(party+actor_offset::world_y)-std::uint32_t(units::q16_one));
        memory_.write(object+actor_offset::elevation,memory_.read(party+actor_offset::elevation));
    }
    publish_tile(object);
}
void MapObjects::tick_conveyor_shadow(Address object){
    if(!still_on_this_map(object))return;
    if(!on_conveyor_cell())memory_.write(object+actor_offset::elevation,parked_elevation);
    else{
        const auto party=party_actor();
        const auto frame=memory_.read(object+actor_offset::motion_frame)+1;
        memory_.write(object+actor_offset::motion_frame,frame);
        memory_.write(object+actor_offset::facing,memory_.read(party+actor_offset::facing));
        memory_.write(object+actor_offset::sprite_frame,std::uint32_t((signed32(frame)/4)&1));
        memory_.write(object+actor_offset::world_x,memory_.read(party+actor_offset::world_x));
        memory_.write(object+actor_offset::world_y,memory_.read(party+actor_offset::world_y)+3*std::uint32_t(units::q16_one));
        memory_.write(object+actor_offset::elevation,memory_.read(party+actor_offset::elevation));
    }
    publish_tile(object);
}
void MapObjects::tick_platform(Address object){
    if(!still_on_this_map(object))return;
    const auto frame=signed32(memory_.read(object+actor_offset::motion_frame));
    if(frame<=-1)memory_.write(object+actor_offset::elevation,parked_elevation);
    else{
        // Frame3 is the moment the platform seats itself under the party.
        if(frame==3)play(platform_arrive_cue);
        const auto party=party_actor();
        memory_.write(object+actor_offset::facing,memory_.read(party+actor_offset::facing));
        memory_.write(object+actor_offset::sprite_frame,std::uint32_t(frame));
        memory_.write(object+actor_offset::world_x,memory_.read(party+actor_offset::world_x));
        // One tile row below the party, in world units.
        memory_.write(object+actor_offset::world_y,memory_.read(party+actor_offset::world_y)+std::uint32_t(units::tile_height_q16));
        memory_.write(object+actor_offset::elevation,memory_.read(party+actor_offset::elevation));
    }
    publish_tile(object);
}
void MapObjects::tick_chest_timer(Address slot){
    // +44 counts frames here, not a gate flag.
    const auto elapsed=signed32(memory_.read(slot+overlay_offset::gate_flag));
    memory_.write(slot+overlay_offset::gate_flag,std::uint32_t(elapsed+1));
    if(elapsed<=chest_timer_ticks){stop_running(slot);return;}
    if(!services_.spawn_sparkle)throw Fault(0x4552a3,"map pickup spawn is not connected");
    // Three records pack two16-bit fields each.
    const auto low=[&](unsigned at){return std::int32_t(memory_.read(slot+at,2));};
    const auto high=[&](unsigned at){return std::int32_t(memory_.read(slot+at)>>16);};
    services_.spawn_sparkle(low(overlay_offset::patch_id),high(overlay_offset::patch_id),
                            low(overlay_offset::scratch),high(overlay_offset::scratch),
                            std::uint32_t(low(overlay_offset::scratch_frame)),std::uint32_t(high(overlay_offset::scratch_frame)));
    for(unsigned offset=0;offset<overlay_stride;++offset)memory_.write(slot+offset,0,1); // 0x457b7c
}
Address MapObjects::spawn_marker(std::int32_t x,std::int32_t y,std::int32_t z,std::int32_t script){
    if(!services_.allocate)throw Fault(0x45d89c,"map object allocator is not connected");
    const auto object=services_.allocate(0x4874a7);
    set_actor_tile_position(memory_,object,x,y,z);
    const auto index=script<0?0:script>std::int32_t(marker_script_count-1)?std::int32_t(marker_script_count-1):script;
    if(!services_.start_effect)throw Fault(0x447841,"map marker effect script is not connected");
    services_.start_effect(object,memory_.read(marker_scripts+std::uint32_t(index)*4));
    return object;
}
void MapObjects::clear_region_animation(Address slot){
    const auto patch=patch_of(slot);
    map_.apply_patch(std::uint32_t(patch),true);
    if(!memory_.read(slot+overlay_offset::gate_flag))return;
    const auto record=tables::map_patches+std::uint32_t(patch)*map_patch_stride;
    const auto x0=signed32(memory_.read(record+map_patch_offset::x)),y0=signed32(memory_.read(record+map_patch_offset::y));
    const auto width=signed32(memory_.read(record+map_patch_offset::width)),height=signed32(memory_.read(record+map_patch_offset::height));
    const auto layer=memory_.read(record+12),stride=memory_.read(globals::map_layer_width);
    for(std::int32_t x=x0;x<x0+width;++x)for(std::int32_t y=y0;y<y0+height;++y){
        const auto lookup=globals::tile_animation_lookup
            +((layer<<12)+std::uint32_t(x)+std::uint32_t(y)*stride)*12;
        // Three water descriptors become their drained counterparts.
        switch(memory_.read(lookup)){
        case 0x32:memory_.write(lookup,0xffffffffu);break;
        case 0x33:memory_.write(lookup,6);break;
        case 0x34:memory_.write(lookup,9);break;
        default:break;
        }
        memory_.write(lookup+4,0xffffffffu); // The cell stops cycling either way.
    }
}
void MapObjects::trigger_position_event(Address slot){
    if(!services_.position_event)throw Fault(0x413787,"map position event is not connected");
    services_.position_event(signed32(memory_.read(slot+overlay_offset::left)),
                             signed32(memory_.read(slot+overlay_offset::top)));
    memory_.write(slot+overlay_offset::ticks,0);
    stop_running(slot);
}
} // namespace fsb::core::map_logic
