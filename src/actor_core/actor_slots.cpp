#include "fsb_core/actor_core/actor_slots.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
// The chain table stores the alias id at +0 and the actor record address at
// +0x40 of the same0x44-byte record, which is why the original reaches the
// pointer either as [record+0x40] or through the0x5b35a0 base.
std::int32_t alias_of(const Memory& memory, std::int32_t index) {
    return signed32(memory.read(tables::actor_alias_ids + std::uint32_t(index) * 0x44));
}
Address record_of(const Memory& memory, std::int32_t index) {
    return memory.read(tables::actor_object_pointers + std::uint32_t(index) * 0x44);
}
} // namespace

ResolveActor memory_resolver(const Memory& memory) {
    return [&memory](std::uint32_t selector) { return lookup_actor(memory, selector); };
}

GuestWords memory_words(Memory& memory) {
    return {[&memory](Address at) { return memory.read(at); },
            [&memory](Address at, std::uint32_t value) { memory.write(at, value); }};
}

bool is_script_selector(std::uint32_t selector) { return selector < direct_object_selector; }

Address resolve_slot(const Memory& memory, std::uint32_t slot) {
    // The original indexes the chain table with no bound check, so an out of
    // range slot reads whatever the table stride lands on. Keep that: Memory
    // reports an unmapped address the same way the original would trap.
    const auto self = signed32(slot);
    const auto link = alias_of(memory, self);
    if (link == self) return record_of(memory, self);
    // The slot points elsewhere in the chain, so walk toward the link and take
    // the first neighbour that claims this slot.
    if (link > self) {
        for (auto index = self - 1; index > std::int32_t(alias_scan_low_exclusive); --index)
            if (alias_of(memory, index) == self) return record_of(memory, index);
    } else {
        // The entry guard rejects0x29c, but the loop bound is a pointer
        // comparison against0x5be6d0, so indices up to0x29f are still scanned.
        if (self + 1 >= std::int32_t(alias_entry_guard)) return 0;
        for (auto index = self + 1; index < std::int32_t(alias_scan_high_exclusive); ++index)
            if (alias_of(memory, index) == self) return record_of(memory, index);
    }
    return 0;
}

Address resolve_selector(const Memory& memory, Address sequence, std::uint32_t selector) {
    if (selector == 0xffffffffu) return memory.read(sequence + vm_offset::actor_object);
    if (is_script_selector(selector)) return resolve_slot(memory, selector);
    return selector;
}

Address attach_sequence_actor(Memory& memory, Address sequence, const ResolveActor& resolve) {
    const auto object = resolve(memory.read(sequence + vm_offset::actor_id));
    memory.write(sequence + vm_offset::actor_object, object);
    return object;
}

Address resolve_or_report(std::uint32_t selector, const ResolveActor& resolve,
                          const MissingActorReport& report) {
    const auto object = resolve(selector);
    if (!object && report) report(selector);
    return object;
}

bool visible(const GuestWords& words, Address object) {
    if (!object) throw Fault(0x42ff47, "actor visibility query needs a materialized actor");
    return (words.read(object + actor_offset::flags) >> 6) & 1;
}

bool visible_by_selector(const GuestWords& words, std::uint32_t selector, const ResolveActor& resolve) {
    const auto object = resolve(selector);
    return object && visible(words, object);
}

void set_visible_by_selector(const GuestWords& words, std::uint32_t selector,
                             std::uint32_t mode, const ResolveActor& resolve) {
    const auto object = resolve(selector);
    if (!object) return;
    if (mode > 1) throw Fault(0x42ff9b, "actor visibility mode must be zero or one");
    const auto flags = words.read(object + actor_offset::flags);
    words.write(object + actor_offset::flags, mode ? flags | 0x40u : flags & ~0x40u);
}

bool tile_state(const GuestWords& words, Address object) {
    if (!object) throw Fault(0x43001f, "actor tile-state query needs a materialized actor");
    return (words.read(object + actor_offset::flags) >> 16) & 1;
}

void set_facing(const GuestWords& words, Address object, std::uint32_t facing) {
    words.write(object + actor_offset::facing, facing);
}
void set_target_facing(const GuestWords& words, Address object, std::uint32_t facing) {
    words.write(object + actor_offset::target_facing, facing);
}
void set_both_facings(const GuestWords& words, Address object, std::uint32_t facing) {
    set_facing(words, object, facing);
    set_target_facing(words, object, facing);
}

void reset_pose_row(const GuestWords& words, Address object) {
    // Rows0-3 are the cardinal walking directions, rows4-7 their variants.
    constexpr std::uint32_t rows[] = {0, 6, 12, 18, 5, 11, 17, 23};
    const auto facing = words.read(object + actor_offset::facing);
    words.write(object + actor_offset::sprite_selector, animated_sprite_selector);
    if (facing >= std::size(rows)) throw Fault(0x4300e1, "actor facing outside the pose row table");
    words.write(object + actor_offset::sprite_frame, rows[facing]);
}

void turn_by_selector(const GuestWords& words, std::uint32_t selector, std::uint32_t facing,
                      const ResolveActor& resolve, const MissingActorReport& report) {
    set_both_facings(words, resolve_or_report(selector, resolve, report), facing);
}

bool character_active(const Memory& memory, std::uint32_t character) {
    const auto count = signed32(memory.read(globals::party_count));
    for (std::int32_t slot = 0; slot < count; ++slot)
        if (memory.read(globals::party_actor_ids + std::uint32_t(slot) * 4) == character) return true;
    return false;
}

bool actor_exists(const Memory& memory, std::uint32_t character) {
    if (!is_script_selector(character)) throw Fault(0x4306cb, "IFC actor query needs a script actor id");
    return character_active(memory, character);
}

std::uint32_t player_character(const Memory& memory) {
    return memory.read(globals::party_actor_ids + memory.read(globals::active_party_index) * 4);
}
} // namespace fsb::core::actor_core
