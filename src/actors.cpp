#include "fsb_core/actors.hpp"
#include "fsb_core/actor_core/actor_lifecycle.hpp"
#include "fsb_core/actor_core/actor_runtime.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/arena.hpp"
#include <algorithm>

namespace fsb::core {
void Actors::cleanup_event(Dialogue& dialogue){
    unsigned count=0,first=10;const auto player=player_id();
    for(unsigned i=0;i<10;++i){const auto id=memory_.read(tables::party_member_ids+i*4);if(in_party(id))++count;if(id==player)first=i;}
    if(!count||first==10)throw Fault(0x430958,"event cleanup has no valid live-party iterator");
    memory_.write(globals::party_iterator,first);
    for(unsigned i=0;i<count;++i){
        unsigned cursor=memory_.read(globals::party_iterator);std::uint32_t id;
        for(unsigned guard=0;;++guard){
            if(guard>=10)throw Fault(0x4307ac,"live-party iterator exhausted");
            cursor%=10;id=memory_.read(tables::party_member_ids+cursor*4);if(in_party(id))break;++cursor;
        }
        memory_.write(globals::party_iterator,cursor+1);const auto actor=lookup_actor(memory_,id);
        if(!actor)throw Fault(id,"cleanup actor is not materialized");
        dialogue.clear_sequence(actor+actor_offset::sequence_handle);dialogue.release_reference(actor+actor_offset::dialogue_handle);
    }
    release_extra_party(dialogue);
}
void Actors::rebuild_extra_party(){
    for(unsigned i=0;i<2;++i)if(memory_.read(globals::party_selection_mask)&(1u<<(16+i))){
        const auto id=0x280+i,actor=spawn_sequence(id);visible(actor,false);set_actor_tile_state(memory_,actor,0);
        const auto handle=Arena(memory_).clone_event(i?0x6cab3b:0x6caa1f,1),child=*resolve_compact(memory_,handle);
        memory_.write(child+vm_offset::actor_id,id);memory_.write(child+vm_offset::actor_object,lookup_actor(memory_,id));memory_.write(0x7693a0+i*4,handle);
    }
}
void Actors::release_extra_party(Dialogue& dialogue){
    for(unsigned i=0;i<2;++i)if(memory_.read(globals::party_selection_mask)&(1u<<(16+i))){
        dialogue.release_reference(0x7693a0+i*4);
        if(const auto actor=lookup_actor(memory_,0x280+i)){dialogue.clear_sequence(actor+actor_offset::sequence_handle);dialogue.release_reference(actor+actor_offset::dialogue_handle);finalize(actor);}
    }
}
Address Actors::allocate_extra_slot(){
    // Original45d89c with no callback: claim a tail record and clear its tick.
    return actor_core::spawn_callback_object(memory_,0,{});
}
Address Actors::spawn_map_marker(){
    const auto object=actor_core::spawn_callback_object(memory_,routines::map_marker_tick,
        [&](Address,Address record){tick_map_marker(record);},
        [&]{throw Fault(routines::map_marker_tick,"map marker actor pool exhausted");});
    return object;
}
void Actors::tick_map_marker(Address object){
    const auto state=memory_.read(object+actor_offset::callback_state);
    if(state==0xffffffffu)memory_.write(object+0x160,memory_.read(globals::current_map_id));
    else if(!state&&memory_.read(object+0x160)!=memory_.read(globals::current_map_id))finalize(object);
}
Address Actors::slot(unsigned index) {
    if (index >= 0x300) throw Fault(index, "actor slot outside0x300 pool");
    return globals::actor_objects + index * layout::actor_size;
}
Actors::Snapshot Actors::snapshot(Address object) const {
    Snapshot result{}; for (unsigned i = 0; i < result.size(); ++i) result[i] = memory_.read(object + i * 4); return result;
}
void Actors::finalize(Address object) {
    // Original45d91d. The chain unlink, the last callback run and the record
    // wipe live in actor_core; this supplies the indirect callback dispatch,
    // which in the product is a fixed set of ported handlers.
    actor_core::finalize_object(memory_, object, [&](Address callback, Address record) {
        finalize_callback(callback, record);
    });
}
void Actors::finalize_callback(Address callback, Address object) {
    if(callback==routines::map_marker_tick){
        tick_map_marker(object);
    }else if(callback==0x45b8a0){
        if(tick_npc(object).sound)throw Fault(callback,"NPC finalizer sound requires its audio dispatcher");
    }else if(callback==0x447e31){
        tick_bound_son(object);
    }else if(callback==0x488737){
        tick_platform(object);
    }else if(callback==0x45c526){
        if(tick_default_visual(object).sound)throw Fault(callback,"default visual finalizer sound needs audio dispatcher");
    }else if(callback_finalizer){
        callback_finalizer(callback,object);
    }else{
        // Event2 compacts the party, then finalizes the old tail slot. The
        // original458ed7 callback still runs with +14c=-2. On this idle,
        // field path during an event its only surviving (non-actor) write is
        // the field phase increment; all actor-local writes are wiped below.
        // Actual45d91d execution on the transition snapshot verifies this.
        const auto input=memory_.read(globals::input_message),key=memory_.read(globals::input_key);
        const bool ordinary_input=!input||input==input_message::key_up||input==input_message::system_key_up||input==input_message::mouse_button||
            ((input==input_message::key_down||input==input_message::system_key_down)&&(key==13||(key>=16&&key<=19)||key==32||key==35||key==88||key==97||key==101||(key>=37&&key<=40)));
        if(callback!=routines::field_actor_movement||memory_.read(object+actor_offset::motion_state)||memory_.read(globals::game_mode)!=3||
           signed32(memory_.read(globals::current_event_id))<0||!ordinary_input||
           signed32(memory_.read(globals::field_transition_phase))<0||signed32(memory_.read(0x5d0768))>=0||memory_.read(0x8021d8))
            throw Fault(callback,"actor finalizer callback path is not connected");
        // Original execution with confirm/repeat/release, each arrow and mouse
        // packets has the same surviving write. Actor-local moves are discarded.
        const auto x=signed32(memory_.read(object+actor_offset::tile_x_q16))/units::q16_one,y=signed32(memory_.read(object+actor_offset::tile_y_q16))/units::q16_one,z=signed32(memory_.read(object+actor_offset::layer_q16))/units::q16_one;
        const auto width=memory_.read(globals::map_layer_width+std::uint32_t(z)*0x8028),cell=std::uint32_t(z)*4096+std::uint32_t(y)*width+std::uint32_t(x);
        if(memory_.read(globals::tile_attributes+cell*4))throw Fault(callback,"special-tile field finalization is not connected");
        memory_.write(globals::field_transition_phase,memory_.read(globals::field_transition_phase)+1);
    }
}
void Actors::reset_range(unsigned first, unsigned end) {
    if (end > 0x300 || first > end) throw Fault(first, "invalid actor reset range");
    actor_core::reset_slot_range(memory_, first, end,
        [&](Address callback, Address record) { finalize_callback(callback, record); });
}
void Actors::reset_party() { actor_core::reset_party_slots(memory_); } // Original45f47e.
bool Actors::in_party(unsigned id) const {
    for (unsigned i = 0; i < memory_.read(globals::party_count); ++i) if (memory_.read(globals::party_actor_ids + i * 4) == id) return true;
    return false;
}
unsigned Actors::player_id() const {
    const auto index = memory_.read(globals::active_party_index);
    if (index >= memory_.read(globals::party_count) || index >= 10) throw Fault(index, "active player not initialized");
    return memory_.read(globals::party_actor_ids + index * 4);
}
void Actors::compose_stats(unsigned id) {
    const auto delta = id * 0xbc;
    for (unsigned i = 0; i < 6; ++i) memory_.write(0x607a60 + delta + i * 4, memory_.read(0x607a44 + delta + i * 4));
    std::uint32_t flags = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const auto item = memory_.read(0x607a7c + delta + i * 4);
        if (signed32(item) < 0) continue;
        if (item > 1023) throw Fault(item, "equipment item outside known catalog scope");
        for (unsigned stat = 0; stat < 6; ++stat) {
            const auto dst = 0x607a60 + delta + stat * 4;
            memory_.write(dst, memory_.read(dst) + memory_.read(0x6131ac + item * 0x4c + stat * 4));
        }
        flags |= memory_.read(0x61319c + item * 0x4c);
    }
    const auto before = memory_.read(0x607a10 + delta);
    memory_.write(0x607a10 + delta, before ^ ((flags ^ before) & 31));
}
int Actors::spawn_party(unsigned id) {
    if (id >= 16) throw Fault(id, "party id outside stat table scope");
    for (unsigned i = 0; i < 10; ++i) if (memory_.read(globals::party_actor_ids + i * 4) == id) return -2;
    const auto index = memory_.read(globals::party_count); if (index >= 10) return -1;
    const auto object = slot(index);
    memory_.write(globals::party_actor_ids + index * 4, id);
    for (auto offset : {0x20,0x24,0x28,0x2c,0x30,0x34,0x38,0x104,0x108,0x138,0x148,0x13c}) memory_.write(object + offset, 0);
    memory_.write(object + actor_offset::flags, memory_.read(0x607a0c + id * 0xbc) | 0x1010e);
    memory_.write(object + actor_offset::sprite_base, memory_.read(0x5b3598 + id * 0x44)); memory_.write(object + actor_offset::sprite_selector, 0x20000);
    const auto pcpos = memory_.read(0x803a28); //45f18d: wrapped table index, including the saved -1 sentinel.
    memory_.write(object + actor_offset::facing, memory_.read(0x7ab9ac + pcpos * 24));
    memory_.write(globals::actor_active_count, memory_.read(globals::actor_active_count) + 1); memory_.write(globals::party_count, index + 1);
    compose_stats(id); memory_.write(tables::actor_object_pointers + id * 0x44, object); return int(index);
}
void Actors::push_snapshot(unsigned index) {
    if (index > 2) throw Fault(index, "character snapshot slot outside small table");
    const auto active = memory_.read(globals::active_party_index), object = slot(active), count = memory_.read(globals::party_count);
    memory_.write(0x803a08 + index * 4, active); memory_.write(0x803a30 + index * 4, count);
    memory_.write(0x8039d8 + index * 16, memory_.read(object + actor_offset::tile_x));
    memory_.write(0x8039dc + index * 16, memory_.read(object + actor_offset::tile_y));
    memory_.write(0x8039e0 + index * 16, sequence_alu(alu::arithmetic_shift_right, memory_.read(object + actor_offset::layer_q16), 16));
    memory_.write(0x8039e4 + index * 16, memory_.read(object + actor_offset::facing));
    for (unsigned i = 0; i < count; ++i) memory_.write(0x803960 + index * 40 + i * 4, memory_.read(globals::party_actor_ids + i * 4));
}
void Actors::bootstrap_new_game_actors() {
    reset_range(0, 0x300); reset_party();
    for (auto id : {9u, 4u, 8u, 6u, 11u}) spawn_party(id);
    memory_.write(globals::active_party_index, 0); push_snapshot(1);
    reset_party(); spawn_party(3); // Original title NEW_GAME_BOOT after common seed.
}
void Actors::visible(Address object, bool on) {
    if (!object) throw Fault(0x42ff9b, "missing actor for visibility");
    const auto flags = memory_.read(object + actor_offset::flags); memory_.write(object + actor_offset::flags, on ? flags | 64 : flags & ~64u);
}
void Actors::clear_frame(Address object) {
    constexpr unsigned rows[] = {0,6,12,18,5,11,17,23};
    const auto direction = memory_.read(object + actor_offset::facing);
    if (direction >= 8) throw Fault(object, "actor facing outside frame-row table");
    memory_.write(object + actor_offset::sprite_selector, 0x20000); memory_.write(object + actor_offset::sprite_frame, rows[direction]);
}
void Actors::dialog_anchor(Address object) {
    const auto x = (memory_.read(object + actor_offset::world_x) + memory_.read(object + actor_offset::draw_offset_x)) >> 16;
    const auto y = (memory_.read(object + actor_offset::world_y) + memory_.read(object + actor_offset::draw_offset_y) - memory_.read(object + actor_offset::elevation)) >> 16;
    memory_.write(object + actor_offset::screen_anchor_x, x + memory_.read(globals::viewport_width) / 2 - memory_.read(globals::camera_x) + memory_.read(globals::viewport_left));
    memory_.write(object + actor_offset::screen_anchor_y, y + memory_.read(globals::viewport_height) / 2 - memory_.read(globals::camera_y) - 38 + memory_.read(globals::viewport_top));
}
void Actors::copy_pose(Address dst, const Snapshot& src) {
    for (auto offset : {8,12,16,20,24,28,32,36,40,0x128,0x12c,0x110}) memory_.write(dst + offset, src[offset / 4]);
}
unsigned Actors::swap_player(unsigned id) {
    // Original430527. Returns the character that was driving before the swap.
    const auto current_id = player_id(), saved_focus = memory_.read(globals::camera_focus_actor_index);
    if (current_id == id) { visible(lookup_actor(memory_, id), true); return current_id; }
    const auto from = lookup_actor(memory_, current_id), to = lookup_actor(memory_, id);
    if (!in_party(id) || !to) throw Fault(id, "player swap needs an active party actor");
    // The original copies both records onto its own frame first, so every
    // comparison below reads the appearance from before the handoff.
    const auto old = snapshot(from), target = snapshot(to);
    visible(from, true);
    actor_core::focus_party_character(memory_, actor_core::memory_words(memory_), id);
    const auto tile = [&](const Snapshot& data, unsigned axis) { return sequence_alu(alu::arithmetic_shift_right, data[5 + axis], 16); };
    bool positions_equal = true; for (unsigned i = 0; i < 3; ++i) positions_equal &= tile(old, i) == tile(target, i);
    if ((target[1] & 64) && !positions_equal) {
        copy_pose(from, old); dialog_anchor(from); visible(from, true); copy_pose(to, target);
    } else { visible(from, false); memory_.write(to + actor_offset::facing, old[0x110 / 4]); memory_.write(to + actor_offset::target_facing, old[0x114 / 4]); clear_frame(to); }
    set_actor_tile_state(memory_, to, (old[1] >> 16) & 1); dialog_anchor(to);
    if (saved_focus != old[0]) memory_.write(globals::camera_focus_actor_index, saved_focus);
    visible(to, true);
    return current_id;
}
void Actors::copy_party_slot(Address dst, Address src) {
    for (unsigned offset = 0x3c; offset < 0x104; offset += 2) memory_.write(dst + offset, memory_.read(src + offset, 2), 2);
    for (unsigned offset = 4; offset <= 0x38; offset += 4) memory_.write(dst + offset, memory_.read(src + offset));
    for (unsigned offset = 0x104; offset <= 0x168; offset += 4) memory_.write(dst + offset, memory_.read(src + offset));
    memory_.write(dst + actor_offset::callback_argument, memory_.read(src + actor_offset::callback_argument));
}
void Actors::replace_with_party_actor(unsigned id){
    if(in_party(id))return;
    const auto old=lookup_actor(memory_,id);if(!old||memory_.read(old)<10){spawn_party(id);return;}
    const auto saved=snapshot(old);if(spawn_party(id)<0)throw Fault(id,"replacement party allocation failed");
    const auto fresh=lookup_actor(memory_,id),identity=memory_.read(fresh),flags=memory_.read(fresh+4);
    for(unsigned i=0;i<saved.size();++i)memory_.write(fresh+i*4,saved[i]);
    memory_.write(fresh,identity);memory_.write(fresh+4,flags);visible(fresh,true);
    if(const auto parent=resolve_compact(memory_,memory_.read(old+0x140)))memory_.write(*parent+0xfc,fresh);
    if(const auto child=resolve_compact(memory_,memory_.read(old+0x13c)))memory_.write(*child+0xf8,id);
    finalize(old);
}
void Actors::remove_party(unsigned id) {
    const auto count = memory_.read(globals::party_count); unsigned index = 0;
    while (index < count && memory_.read(globals::party_actor_ids + index * 4) != id) ++index;
    if (index == count) return;
    memory_.write(tables::actor_object_pointers + id * 0x44, 0); memory_.write(globals::party_count, count - 1);
    const auto removed = index;
    for (; index < count - 1; ++index) {
        const auto moved = memory_.read(globals::party_actor_ids + (index + 1) * 4);
        memory_.write(globals::party_actor_ids + index * 4, moved); copy_party_slot(slot(index), slot(index + 1));
        memory_.write(tables::actor_object_pointers + moved * 0x44, slot(index));
    }
    if (removed < count - 1 && removed < memory_.read(globals::active_party_index)) {
        const auto active = memory_.read(globals::active_party_index) - 1;
        memory_.write(globals::camera_focus_actor_index, active); memory_.write(globals::active_party_index, active); memory_.write(globals::character_index, active);
    }
    memory_.write(globals::party_actor_ids + (count - 1) * 4, 0xffffffff); finalize(slot(count - 1));
}
void Actors::set_party_mask(std::uint32_t mask, bool remove_dropped) {
    memory_.write(globals::party_selection_mask, mask); if (!mask) return;
    if (!(mask & 1023)) throw Fault(0x4307e3, "nonzero party mask must select at least one member");
    auto current = player_id();
    for (unsigned i = 0; i < 10; ++i) if (mask & (1u << i)) {
        const auto id = memory_.read(tables::party_member_ids + i * 4);
        if (!in_party(id) && spawn_party(id) < 0) throw Fault(id, "party allocation failed");
        set_actor_tile_state(memory_, id, 0);
    }
    if (remove_dropped) for (unsigned i = 0; i < 10; ++i) if (!(mask & (1u << i))) {
        const auto id = memory_.read(tables::party_member_ids + i * 4); if (!in_party(id)) continue;
        if (id == current) { unsigned first = 0; while (!(mask & (1u << first))) ++first; current = memory_.read(tables::party_member_ids + first * 4); swap_player(current); }
        const auto focus = memory_.read(globals::camera_focus_actor_index); remove_party(id); if (focus == 0x2ff) memory_.write(globals::camera_focus_actor_index, focus);
    }
    set_actor_tile_state(memory_, current, 0); visible(lookup_actor(memory_, current), true);
    rebuild_extra_party();
}
void Actors::hide_party() { // Original430a7f.
    actor_core::hide_all_party(memory_, actor_core::memory_words(memory_), actor_core::memory_resolver(memory_));
}
void Actors::reset_sequence_list() {
    memory_.write(globals::collected_actor_count, 0); memory_.write(globals::actor_active_count, 10);
    for (Address at = 0x8019d0; at < 0x8021a0; at += 40) memory_.write(at, 0xffffffff);
}
Address Actors::spawn_effect(std::int32_t x, std::int32_t y, std::int32_t z, unsigned direction, std::uint32_t effect) {
    const auto count = memory_.read(globals::collected_actor_count); memory_.write(globals::collected_actor_count, count + 1);
    if (count + 10 >= 60) return 0;
    if (direction >= 8) throw Fault(direction, "effect direction outside original table");
    const auto object = slot(count + 10); set_actor_tile_position(memory_, object, x, y, z);
    memory_.write(object, count + 10); memory_.write(object + actor_offset::flags, 0x24e);
    for (auto offset : {0x20,0x24,0x28,0x2c,0x30,0x34,0x38,0x104,0x108,0x120,0x124}) memory_.write(object + offset, 0);
    memory_.write(object + actor_offset::facing, direction); memory_.write(object + actor_offset::target_facing, direction);
    memory_.write(object + actor_offset::sprite_base, effect); memory_.write(object + actor_offset::sprite_selector, 0x20000);
    memory_.write(object + actor_offset::sprite_frame, memory_.read(tables::direction_to_cardinal + direction * 4) * 6);
    return object;
}
} // namespace fsb::core
