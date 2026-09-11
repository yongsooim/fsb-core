#pragma once
#include "actor_fields.hpp"
#include <functional>

namespace fsb::core {
class Dialogue;
struct Rect;
struct ActorMotionStep { bool active=false;std::optional<unsigned> sound; };
class Actors {
public:
    explicit Actors(Memory& memory) : memory_(memory) {}
    std::function<void(Address,Address)> callback_finalizer;
    static Address slot(unsigned index);
    void reset_range(unsigned first, unsigned end);
    void finalize(Address object); // Original45d91d.
    void finalize_callback(Address callback, Address object); // The ported +0x148 handlers.
    void reset_party();
    int spawn_party(unsigned id);
    void bootstrap_new_game_actors(); // Actor portion of0x460872, not complete engine startup.
    void push_snapshot(unsigned index);
    bool in_party(unsigned id) const;
    unsigned player_id() const;
    void remove_party(unsigned id);
    unsigned swap_player(unsigned id); //430527: returns the previous player.
    void replace_with_party_actor(unsigned id); // 430434: preserve pose and relink live sequences.
    void set_party_mask(std::uint32_t mask, bool remove_dropped);
    void hide_party();
    void reset_sequence_list();
    Address spawn_effect(std::int32_t x, std::int32_t y, std::int32_t z, unsigned direction, std::uint32_t effect);
    void visible(Address object, bool on);
    void clear_frame(Address object);
    void dialog_anchor(Address object);
    Handle attach_child(std::uint32_t actor_id,Address definition); //430d1c: group1 child with resolved actor binding.
    Handle start_turn(std::uint32_t actor_id, std::uint32_t direction, std::uint32_t extra);
    Handle start_walk(std::uint32_t actor_id,std::uint32_t direction,std::uint32_t count,unsigned variant=0,std::uint32_t extra=0);
    Handle follow_path(std::uint32_t actor_id,int x,int y,unsigned facing,unsigned variant,bool block_party,bool block_objects,bool wait_mode);
    unsigned trace_path(Address actor,int x,int y,unsigned facing,unsigned flags,unsigned max_steps=100);
    std::uint32_t step_attribute(Address actor,unsigned direction) const;
    Handle tween_raw(std::uint32_t actor_id, std::uint32_t x, std::uint32_t y, std::uint32_t duration, bool input_pause);
    void tick_raw_tween(Address object, unsigned jobs);
    Handle tween_tiles(std::uint32_t actor_id, std::uint32_t dx, std::uint32_t dy, std::uint32_t duration,
                       std::uint32_t arc_x, std::uint32_t arc_y, bool hide_on_complete, bool input_pause);
    void tick_tile_tween(Address object, bool input_pause);
    Address allocate_extra_slot(); // Null-callback branch of0x45d89c; returns a1ac actor pointer.
    Address spawn_map_marker(); // 45d89c + the original4874a7 map-lifetime callback.
    void tick_map_marker(Address object);
    ActorMotionStep step_motion(Address object); // Original45c55c, independent of input/map decisions.
    void resolve_field_frame(Address object); // Original45cc1b field-mode branches.
    void spawn_collected(); // 457e1c/457d24, using the original collected NPC rows.
    Address spawn_sequence(unsigned id); // 4302f8/457f3e, canonical template chain.
    ActorMotionStep tick_npc(Address object); // 45b8a0, shared CRT RNG stream.
    void tick_bound_son(Address object); // Original447e31 pre-release Son Goku pose.
    std::optional<unsigned> tick_platform(Address object); // 488737 visual follower.
    void apply_entry_direction(Address object,std::uint32_t direction); // 458dc4 pcpos first step.
    void refresh_stats(unsigned id){compose_stats(id);}
    void flood_costs(unsigned grid,int x,int y,unsigned flags,Rect bounds);
    void resolve_battle_frame(Address object); // 45cc1b battle idle/status branches.
    ActorMotionStep tick_default_visual(Address object); // 45c526 motion and cache refresh.
    void cleanup_event(Dialogue& dialogue);
    void rebuild_extra_party(); //430a08: the two high-mask scene helper actors.
    void release_extra_party(Dialogue& dialogue); //4309c7, paired lifecycle.
private:
    Memory& memory_;
    using Snapshot = std::array<std::uint32_t, 107>;
    Snapshot snapshot(Address object) const;
    void copy_pose(Address destination, const Snapshot& source);
    void copy_party_slot(Address destination, Address source);
    void compose_stats(unsigned id);
};
} // namespace fsb::core
