#pragma once
#include "../primitives.hpp"

namespace fsb::core::map_logic {
// World map city/route table at globals::worldmap_city_flags, stride 0x54.
// A record is present while its +4 word is non-zero; the table ends at the
// first record with a zero there, so the walk is a terminator scan, not a count.
namespace city_offset {
inline constexpr unsigned flags=0,present=4,first_map=8,first_entry=12,second_map=16,second_entry=20;
}
// Low three bits carry the display state the script selects; bit0 doubles as the
// per-route visited mark that 0x43700a wipes. Bit4 is the script's own toggle
// and bit8 forces the label on for a route endpoint.
inline constexpr std::uint32_t city_state_mask=7,city_visited_bit=1,city_known_bit=4,
                               city_open_bits=6,city_script_toggle=0x10,city_forced_label=0x100;
inline constexpr unsigned worldmap_travel_slots=0x2c; // 0x4360be refuses to travel past this.
inline constexpr unsigned worldmap_effect_modes=15;   // 0x436100 range checks the selector.
inline constexpr Address worldmap_effect_mode=0x5aff7c;
inline constexpr Address worldmap_target_map=0x77149c,worldmap_target_entry=0x7714a0;
// Route music table: {first, second, cue} rows ending at a cue of -1.
inline constexpr Address route_music=0x5b0e50;
inline constexpr unsigned route_music_stride=0xc,default_route_cue=0x17;

class Worldmap {
public:
    explicit Worldmap(Memory& memory):memory_(memory){}
    void set_destination(unsigned slot,std::uint32_t map,std::uint32_t entry); // 0x435fac
    void set_script_toggle(unsigned slot,bool on);                             // 0x435fca
    void set_display_state(unsigned slot,std::uint32_t state);                 // 0x435ffa
    void force_endpoint_label(unsigned slot);                                  // 0x436060
    bool endpoint_is_present(std::uint32_t map,std::uint32_t entry)const;       // 0x436077
    bool enter_from(unsigned slot);                                            // 0x4360be
    bool select_effect_mode(std::int32_t mode);                                // 0x436100
    std::uint32_t route_cue(std::uint32_t first,std::uint32_t second)const;     // 0x43611c
    void clear_visited();                                                      // 0x43700a
private:
    Memory& memory_;
    Address record(unsigned slot)const;
};
} // namespace fsb::core::map_logic
