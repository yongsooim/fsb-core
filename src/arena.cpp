#include "fsb_core/arena.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/actor_fields.hpp"

namespace fsb::core {
namespace {
constexpr Address free_head = globals::compact_free_head, active_count = globals::compact_active_count;
constexpr Address visible_count = globals::compact_visible_count, runtime_refs = globals::compact_runtime_references;
}
Address Arena::head(unsigned group) {
    if (group >= capacity::object_groups) throw Fault(routines::initialize_object_groups, "invalid runtime owner group");
    return unsigned(globals::object_group_sentinels) + group * unsigned(layout::group_sentinel_stride); // 0xd4 DWORDS, not bytes
}
Address Arena::tail(unsigned group) { return head(group) + unsigned(layout::compact_size); }
void Arena::initialize() {
    for (unsigned index = 0; index < capacity::usable_compact_slots; ++index)
        memory_.write(compact_address(index) + 8, compact_address(index + 1));
    memory_.write(compact_address(capacity::usable_compact_slots) + 8, 0);
    memory_.write(free_head, compact_address(1)); // slot 0 is reserved
    memory_.write(compact_address(0) + 0x10, 1); // invalidates null handle 0
    for (unsigned group = 0; group < capacity::object_groups; ++group) {
        memory_.write(head(group), 0); memory_.write(head(group) + 4, tail(group));
        memory_.write(tail(group), head(group)); memory_.write(tail(group) + 4, 0);
    }
}
Handle Arena::allocate_after(Address predecessor, Address callback, std::uint32_t flags, std::uint32_t argument) {
    const auto object = memory_.read(free_head);
    if (!object) throw Fault(routines::allocate_compact_object, "compact object pool exhausted");
    compact_handle(memory_, object); // alignment/range validation
    const auto next_free = memory_.read(object + compact_offset::next_free), successor = memory_.read(predecessor + compact_offset::next);
    memory_.read(successor); // validate all topology reads before updating links
    memory_.write(free_head, next_free);
    memory_.write(object + compact_offset::callback, callback); memory_.write(object + compact_offset::flags, flags);
    memory_.write(object + compact_offset::argument, argument);
    if (flags & unsigned(compact_flag::skip_constructor)) memory_.write(object + compact_offset::lifecycle, 1);
    if ((flags & compact_flag::exclusive) && !(flags & compact_flag::standby)) {
        memory_.write(object + compact_offset::flags, flags | compact_flag::standby);
        wake(object); //Original arbitration occurs before linking the new node.
    }
    memory_.write(object, predecessor); memory_.write(object + compact_offset::next, successor);
    memory_.write(successor, object); memory_.write(predecessor + compact_offset::next, object);
    memory_.write(object + compact_offset::generation, memory_.read(object + compact_offset::generation) + 1);
    memory_.write(active_count, memory_.read(active_count) + 1);
    if (flags & unsigned(compact_flag::count_visible)) memory_.write(visible_count, memory_.read(visible_count) + 1);
    if (flags & unsigned(compact_flag::count_runtime_reference)) memory_.write(runtime_refs, memory_.read(runtime_refs) + 1);
    return compact_handle(memory_, object);
}
void Arena::standby(Address object) {
    const auto flags = memory_.read(object + compact_offset::flags);
    if (!(flags & compact_flag::standby)) {
        memory_.write(object + compact_offset::flags, flags | compact_flag::standby);
        if (flags & compact_flag::exclusive)
            memory_.write(globals::exclusive_active_count, memory_.read(globals::exclusive_active_count) - 1);
    }
}
Address Arena::active_exclusive(Address excluded) const {
    for (unsigned group = 0; group < capacity::object_groups; ++group) {
        unsigned visited = 0;
        for (auto object = memory_.read(head(group) + compact_offset::next); object != tail(group);
             object = memory_.read(object + compact_offset::next)) {
            //402b67 scans pointers directly; an encoded16-bit generation is
            //not a prerequisite for a node linked in an owner list.
            if (!object || ++visited > capacity::usable_compact_slots) throw Fault(object,"invalid exclusive owner list");
            const auto flags = memory_.read(object + compact_offset::flags);
            if (object != excluded && (flags & compact_flag::exclusive) &&
                (!(flags & compact_flag::standby) || memory_.read(object + compact_offset::lifecycle) == 0xffffffffu))
                return object;
        }
    }
    return 0;
}
bool Arena::activate_exclusive(Address object) {
    if (object) {
        const auto flags = memory_.read(object + compact_offset::flags);
        if (!(flags & compact_flag::exclusive) || !(flags & compact_flag::standby))
            throw Fault(0x402abb, "exclusive activation requires an exclusive standby object");
    }
    if (const auto active = active_exclusive(object)) {
        if (!(memory_.read(active + compact_offset::flags) & compact_flag::immediate_exclusive_handoff)) {
            if (!memory_.read(active + compact_offset::exclusive_wake_target)) {
                memory_.write(active + compact_offset::lifecycle, 0xffffffffu);
                memory_.write(active + compact_offset::exclusive_wake_target, object ? object : 0xffffffffu);
            }
            return object && !(memory_.read(object + compact_offset::flags) & compact_flag::standby);
        }
        standby(active);
    }
    if (!object) return false;
    memory_.write(object + compact_offset::flags, memory_.read(object + compact_offset::flags) & ~compact_flag::standby);
    return true;
}
void Arena::wake(Address object) {
    const auto flags = memory_.read(object + compact_offset::flags);
    if (!(flags & compact_flag::standby)) return;
    if (!(flags & compact_flag::exclusive)) memory_.write(object + compact_offset::flags, flags & ~compact_flag::standby);
    else if (activate_exclusive(object))
        memory_.write(globals::exclusive_active_count, memory_.read(globals::exclusive_active_count) + 1);
}
Handle Arena::clone_event(Address entry, unsigned group) {
    // 0x41a00b passes the value of tail.prev, appending before the sentinel.
    const auto handle = allocate_after(memory_.read(tail(group)), routines::event_vm_tick, 0x10030000, 0);
    memory_.write(*resolve_compact(memory_, handle) + 0x30, entry);
    return handle;
}
Handle Arena::activate_event(unsigned id, std::uint32_t trigger) {
    if (id > 0xa7) throw Fault(id,"event id outside original table");
    const auto definition = memory_.read(tables::event_definitions + id*4);
    if (!definition || signed32(memory_.read(globals::event_activation_counts + id*4)) < 0) return 0;
    const auto handle = clone_event(definition,0), object = *resolve_compact(memory_,handle);
    const auto party_index = memory_.read(globals::active_party_index);
    if (party_index >= 10) throw Fault(party_index,"event activation without a valid player slot");
    set_actor_tile_state(memory_,memory_.read(globals::party_actor_ids+party_index*4),0);
    memory_.write(object+0xe4,trigger == 0xffffffffu ? 0 : trigger); memory_.scene_state().current_event=id;
    return handle;
}
void Arena::release(Handle handle) {
    const auto resolved = resolve_compact(memory_, handle);
    if (!resolved) throw Fault(routines::release_compact_object, "release of stale handle");
    const auto object = *resolved;
    const auto predecessor = memory_.read(object), successor = memory_.read(object + compact_offset::next);
    if (!predecessor || !successor) throw Fault(object, "object has no live owner links");
    const auto generation = memory_.read(object + compact_offset::generation), flags = memory_.read(object + compact_offset::flags);
    if (flags & unsigned(compact_flag::count_visible)) memory_.write(visible_count, memory_.read(visible_count) - 1);
    if (flags & unsigned(compact_flag::count_runtime_reference)) memory_.write(runtime_refs, memory_.read(runtime_refs) - 1);
    memory_.write(successor, predecessor); memory_.write(predecessor + compact_offset::next, successor);
    for (unsigned offset = 0; offset < layout::compact_size; offset += 4) memory_.write(object + offset, 0);
    memory_.write(object + compact_offset::next_free, memory_.read(free_head)); memory_.write(free_head, object);
    memory_.write(object + compact_offset::generation, generation + 1);
    // Preserve walker links after clearing, exactly as 0x402a02 does.
    memory_.write(object, predecessor); memory_.write(object + compact_offset::next, successor);
    memory_.write(active_count, memory_.read(active_count) - 1);
}
std::vector<Handle> Arena::members(unsigned group) const {
    std::vector<Handle> result;
    auto current = memory_.read(head(group) + 4);
    while (current != tail(group)) {
        if (result.size() >= capacity::usable_compact_slots) throw Fault(current, "runtime owner list cycle");
        result.push_back(compact_handle(memory_, current));
        current = memory_.read(current + 4);
    }
    return result;
}
} // namespace fsb::core
