#include "fsb_core/map_logic/worldmap.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::map_logic {
Address Worldmap::record(unsigned slot)const{
    return globals::worldmap_city_flags+slot*layout::worldmap_city_stride;
}
void Worldmap::set_destination(unsigned slot,std::uint32_t map,std::uint32_t entry){
    memory_.write(record(slot)+city_offset::first_map,map);
    memory_.write(record(slot)+city_offset::first_entry,entry);
}
void Worldmap::set_script_toggle(unsigned slot,bool on){
    const auto at=record(slot)+city_offset::flags;
    memory_.write(at,on?memory_.read(at)|city_script_toggle:memory_.read(at)&~city_script_toggle);
}
void Worldmap::set_display_state(unsigned slot,std::uint32_t state){
    const auto at=record(slot)+city_offset::flags,flags=memory_.read(at);
    // The original reports an unknown selector and leaves the record alone.
    if(state==0)memory_.write(at,flags&~city_state_mask);
    else if(state==1)memory_.write(at,(flags&~(city_visited_bit|2u))|city_known_bit);
    else if(state==2)memory_.write(at,flags|city_open_bits);
    else throw Fault(0x435ffa,"world map city display state outside the original selector");
}
void Worldmap::force_endpoint_label(unsigned slot){
    const auto at=record(slot)+city_offset::flags;
    memory_.write(at,memory_.read(at)|city_forced_label);
}
bool Worldmap::endpoint_is_present(std::uint32_t map,std::uint32_t entry)const{
    unsigned slot=0;
    if(memory_.read(record(0)+city_offset::present))
        while(true){
            const auto at=record(slot);
            if(memory_.read(at+city_offset::first_map)==map&&memory_.read(at+city_offset::first_entry)==entry)break;
            if(memory_.read(at+city_offset::second_map)==map&&memory_.read(at+city_offset::second_entry)==entry)break;
            ++slot;
            if(!memory_.read(record(slot)+city_offset::present))break;
        }
    // Falls out on the terminator too, whose present word is zero.
    return memory_.read(record(slot)+city_offset::present)!=0;
}
bool Worldmap::enter_from(unsigned slot){
    const auto at=record(slot);
    if(!memory_.read(at+city_offset::present)||slot>=worldmap_travel_slots)return false;
    memory_.write(worldmap_target_entry,memory_.read(at+city_offset::first_entry));
    memory_.write(worldmap_target_map,memory_.read(at+city_offset::first_map));
    memory_.write(globals::game_mode,10);
    return true;
}
bool Worldmap::select_effect_mode(std::int32_t mode){
    if(mode<0||mode>=std::int32_t(worldmap_effect_modes))return false;
    memory_.write(worldmap_effect_mode,std::uint32_t(mode));
    return true;
}
std::uint32_t Worldmap::route_cue(std::uint32_t first,std::uint32_t second)const{
    for(unsigned row=0;memory_.read(route_music+row*route_music_stride+8)!=0xffffffffu;++row){
        const auto at=route_music+row*route_music_stride;
        if(memory_.read(at)==first&&memory_.read(at+4)==second)return memory_.read(at+8);
    }
    return default_route_cue;
}
void Worldmap::clear_visited(){
    if(!memory_.read(record(0)+city_offset::present))return;
    for(unsigned slot=0;;++slot){
        const auto at=record(slot)+city_offset::flags;
        memory_.write(at,memory_.read(at)&~city_visited_bit);
        if(!memory_.read(record(slot+1)+city_offset::present))return;
    }
}
} // namespace fsb::core::map_logic
