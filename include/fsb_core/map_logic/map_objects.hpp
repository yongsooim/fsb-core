#pragma once
#include "../primitives.hpp"
#include <functional>

namespace fsb::core {
class Map;
namespace map_logic {
// Overlay effect record, 48 bytes from globals::overlay_effects. The words at
// +36 and +40 are scratch whose meaning belongs to the registered callback, so
// each reconstruction names them locally instead of the record naming them once.
namespace overlay_offset {
inline constexpr unsigned flags=0,left=4,top=8,right=12,bottom=16,callback=20,ticks=24,
                          patch_id=32,scratch=36,scratch_frame=40,gate_flag=44;
}
inline constexpr unsigned overlay_stride=48;
inline constexpr std::uint32_t overlay_registered=0x1000000,overlay_running=0x2000000;
// Byte 3 of the flags word carries the running bit; the callbacks clear it there.
inline constexpr unsigned overlay_flag_byte=3;
inline constexpr std::uint8_t overlay_running_bit=2;
// Gate the field sets while a walk-on marker may hand control to the event VM.
// The marker callbacks do nothing at all while it is clear.
inline constexpr Address field_marker_events_enabled=0x5f8588;
// Block switches: one raised/lowered bit per switch id, the number of blocks
// currently lifted, and the 32-byte record each trigger reads its tiles from.
inline constexpr Address switch_raised_bits=0x806b24,lifted_block_count=0x8073b0,switch_records=0x5f85b0;
inline constexpr unsigned switch_record_stride=0x20;
namespace switch_record_offset {
inline constexpr unsigned id=0,trigger_x=4,trigger_y=8,closed_x=0xc,closed_y=0x10,open_x=0x14,open_y=0x18,linked_object=0x1c;
}
// Party stat records; the vitals refill copies each cap into its live value.
inline constexpr Address party_stat_records=0x607a08;
inline constexpr unsigned party_stat_stride=188;
namespace party_stat_offset {
inline constexpr unsigned max_health=24,health=28,max_resource=32,resource=36;
}
inline constexpr unsigned switch_toggle_cue=0x15a,platform_arrive_cue=0x158;
// Marker sprite scripts, selected by a clamped index.
inline constexpr Address marker_scripts=0x5f89e8;
inline constexpr unsigned marker_script_count=8;
// A timed chest drops its pickup once this many ticks have passed.
inline constexpr std::int32_t chest_timer_ticks=0x64;
// Gate installers: the crossing the field is in, and the tile pairs it uses.
inline constexpr Address active_gate_crossing=0x5f858c,gate_positions=0x5f8590;
// Sprite sheet the map objects share, and the selectors each spawner uses.
inline constexpr std::uint32_t map_object_sheet=0xd0;
inline constexpr std::uint32_t conveyor_selector=0xf2,platform_selector=0xf4,
                               pushable_selector=0xf5,gate_piece_selector=0xf6;
inline constexpr unsigned patch_moved_cue=0x150,patch_toggle_cue=0x151,gate_toggle_cue=0x15f;
// A pending step the field takes once the party lands from a gate warp point.
inline constexpr Address pending_switch_hold=0x8021b0;
// The two script slots and the marker 0x492a85 releases with the event id it owns.
inline constexpr Address event_entry_slots[]={0x607cbc,0x607cc0};
inline constexpr Address event_entry_marker=0x607cf4,event_entry_pending=0x8071c8;
inline constexpr std::uint32_t released_event_entry=0xe6;
// One term of the flag combination a gate patch republishes.
struct GateCondition { std::int32_t flag; bool wanted; };
// The three original handlers differ only in where they sample the gate and
// where the toggle cue lands, so the sequence is a parameter, not three copies.
struct GateSequence {
    const GateCondition* conditions;
    unsigned count;
    bool cue_before_patch;      // 0x493e8e plays the toggle cue before the patch.
    bool snapshot_after_patch;  // 0x491b53 samples the gate after the patch.
};

// The party's pushing animation states; a rider only follows while one is set.
inline constexpr std::uint32_t pushing_state=0xb,pushing_settle_state=0xa;
// Facing to sprite frame for the two halves of a pushed gate.
inline constexpr Address gate_piece_frames=0x5f8a08;
// A rider parks itself far below the map while it is not being carried.
inline constexpr std::uint32_t parked_elevation=0xf0000000;
inline constexpr unsigned conveyor_cue=0x159;
// Tile attribute selector a conveyor cell must match.
inline constexpr std::uint32_t surface_mask=0x18000,conveyor_surface=0x10000;

// Connections the reconstructed callbacks need but do not own.
struct MapObjectServices {
    std::function<void(unsigned cue)> play_cue;        // 0x435373
    std::function<bool()> position_event_trigger;      // 0x412181
    std::function<void(Address object)> despawn;       // 0x45d91d
    std::function<void(Address world,Address tile)> world_from_tile;  // 0x45d82d
    std::function<void(std::int32_t x,std::int32_t y)> position_event; // 0x413787
    std::function<Address(Address callback)> allocate;  // 0x45d89c
    std::function<void(Address object,std::uint32_t script)> start_effect; // 0x447841
    // 0x4552a3: the pickup a timed chest drops when it runs out.
    std::function<void(std::int32_t x,std::int32_t y,std::int32_t layer,
                       std::int32_t variant,std::uint32_t item,std::uint32_t quantity)> spawn_sparkle;
};

// The callbacks the original registers in the overlay effect table. Each takes
// the record address; the surrounding frame loop owns activation and the tick
// counter, so nothing here advances it.
class MapObjects {
public:
    MapObjects(Memory& memory,Map& map,MapObjectServices services)
        :memory_(memory),map_(map),services_(std::move(services)){}
    // Runs the reconstruction registered for this original callback address.
    // Returns false when the address is still an unported original body.
    bool tick(Address callback,Address slot);
    void clear_entry_on_event(Address slot);       // 0x486b8c
    void stop_running_on_event(Address slot);      // 0x486bfe
    void tick_patch_toggle(Address slot);          // 0x486c6f
    void tick_patch_open(Address slot);            // 0x486cf9
    void tick_patch_close(Address slot);           // 0x486d71
    void tick_patch_trigger(Address slot);         // 0x486dfb
    void tick_platform_lower_track(Address slot);  // 0x488631
    void tick_platform_upper_track(Address slot);  // 0x4886b4
    void tick_block_switch(Address slot);          // 0x487122
    void tick_falling_floor(Address slot);         // 0x493a26
    // Not overlay records: these two take the object and the party table.
    void set_step_target(Address object,std::uint32_t x,std::uint32_t y); // 0x4870e2
    void restore_active_party_vitals();            // 0x486b53
    // Actor callbacks: the object pump drives these, not the overlay table.
    void tick_map_marker(Address object);          // 0x4874a7
    void tick_pushable(Address object);            // 0x4897a3
    void tick_gate_front_piece(Address object);    // 0x489a60
    void tick_gate_back_piece(Address object);     // 0x489b53
    void tick_conveyor_rider(Address object);      // 0x48897a
    void tick_conveyor_shadow(Address object);     // 0x488aa7
    void trigger_position_event(Address slot);     // 0x4870c6
    // 0x493e8e / 0x48bd7d / 0x491b53: toggle the record's patch, then republish a
    // gate patch from a fixed combination of other event flags.
    void tick_patch_and_gate(Address slot,const GateSequence& sequence);
    void tick_patch_toggle_pair(Address slot);     // 0x494066
    void tick_event_flag_toggle(Address slot);     // 0x49356f
    void tick_gated_patch_claim(Address slot);     // 0x48bea6
    void seed_tile_animation(unsigned column,unsigned row,std::uint32_t index);
    // Spawners. Each takes a slot from the original allocator, which runs the
    // object's own callback once before handing it back.
    Address spawn_pushable(std::int32_t x,std::int32_t y,std::int32_t z);   // 0x4898d3
    void spawn_conveyor_pair();                                            // 0x488bb6
    void spawn_gate_pair(std::int32_t x,std::int32_t y,std::int32_t z);     // 0x489c47
    void spawn_platform_pair();                                            // 0x48880e
    void spawn_switch(unsigned id);                                        // 0x489929
    void spawn_switch_range(unsigned first,unsigned last);                 // 0x4899f6/0x489a18/0x489a3c
    void install_gate_crossing(unsigned crossing);                         // 0x489ed7/0x489ef5 and the two with extra triggers
    Address spawn_marker(std::int32_t x,std::int32_t y,std::int32_t z,std::int32_t script); // 0x4874df
    void tick_platform(Address object);            // 0x488737
    void tick_chest_timer(Address slot);           // 0x48745b
    void clear_region_animation(Address slot);     // 0x486eb7
    void release_pending_event_entry();            // 0x492a85
    void arm_switch_hold();                        // 0x494352
    // True when the object still belongs to this map; despawns it when not.
    bool still_on_this_map(Address object);
private:
    Memory& memory_;
    Map& map_;
    MapObjectServices services_;
    // -1 always passes; any other negative id keeps the object shut.
    bool gate_open(Address slot)const;
    std::int32_t patch_of(Address slot)const;
    std::int32_t patch_sound(std::int32_t patch)const;
    void stop_running(Address slot);
    void tick_gate_piece(Address object,bool back);
    bool on_conveyor_cell()const;
    void publish_tile(Address object);
    Address party_actor()const;
    Address take_object(Address callback,std::uint32_t extra_flags,std::uint32_t selector,std::uint32_t frame,
                        std::int32_t x,std::int32_t y,std::int32_t z);
    void move_party_to_its_own_tile();
    void play(unsigned cue)const;
};
} // namespace map_logic
} // namespace fsb::core
