#pragma once
#include "fsb_core/primitives.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::visual_effects {

// A visual effect is a runtime callback object: the same 0x1ac-byte record the
// actor pool holds, spawned by 45d89c and released by 45d91d. Offsets below
// 0x144 keep their actor meaning, so those come from actor_offset. From 0x144
// up the record is per-callback work storage, which means the names here are
// the effect reading of bytes other callback families use for their own state.
// Memory stays the one store; this is a named view over it, never a copy.
namespace field {
// Callback bookkeeping shared by every effect in this scope.
inline constexpr Address tick_count = 0x144;      // 45d89c zeroes it; ticks count up.
inline constexpr Address callback = actor_offset::callback;        // 0x148
inline constexpr Address phase = actor_offset::callback_state;     // 0x14c
inline constexpr Address owner_object = actor_offset::callback_argument; // 0x1a8

// Placement, written straight through to the actor position fields.
inline constexpr Address x = actor_offset::world_x;         // 0x08, q16
inline constexpr Address y = actor_offset::world_y;         // 0x0c, q16
inline constexpr Address z = actor_offset::elevation;       // 0x10, q16
inline constexpr Address sprite_id = actor_offset::layer_q16; // 0x1c

// Work fields the effect callbacks in this scope share. Every name below is
// taken from the writes and reads of the original bodies, not from the
// decompiler cache identifiers.
inline constexpr Address motion_flags = 0x168;   // Selector 45d208 reads to pick an axis set.
// Per-frame delta the motion helpers add to the placement. Two families in this
// scope write all three; which of them are used follows from motion_flags.
inline constexpr Address step_x = 0x16c;
inline constexpr Address step_y = 0x170;
inline constexpr Address step_z = 0x174;
// The effects copy their origin's facing here and index their tables with it.
inline constexpr Address facing = actor_offset::facing; // 0x110
inline constexpr Address notify_target = 0x178;  // Object handed to 461d25 on completion.
// The object an effect was made for: whom it reports to, or what it belongs to.
// Three families in this scope use it that way.
inline constexpr Address linked_object = 0x160;
// The point the motion helpers work relative to: 45d208 orbits around it,
// 462de2 travels towards it. Which of the two applies is chosen by motion_flags.
inline constexpr Address motion_anchor_x = 0x184;
inline constexpr Address motion_anchor_y = 0x188;
inline constexpr Address motion_anchor_z = 0x18c;
}

// Flag bits of the actor flags dword at +4. The originals reach the upper bits
// through a byte store at +6; an or/and-not through the dword leaves the other
// three bytes untouched, so the resulting memory image is identical.
namespace flag {
inline constexpr std::uint32_t visible = 0x40;                 // Shared with EffectScript.
inline constexpr std::uint32_t effect_script_running = 0x20000; // Shared with EffectScript.
inline constexpr std::uint32_t notify_owner_on_finish = 0x40000; // Byte +6 bit 2.
}

// One effect object, addressed by its guest address. Construction is free and
// every accessor is a direct Memory transfer, so passing this by value costs
// what passing the address costs.
class EffectObject {
public:
    EffectObject(Memory& memory, Address at) : memory_(&memory), at_(at) {}
    Address address() const { return at_; }
    explicit operator bool() const { return at_ != 0; }

    std::int32_t get(Address offset) const { return std::int32_t(memory_->read(at_ + offset)); }
    void set(Address offset, std::int32_t value) { memory_->write(at_ + offset, std::uint32_t(value)); }
    void add(Address offset, std::int32_t delta) { memory_->write(at_ + offset, memory_->read(at_ + offset) + std::uint32_t(delta)); }
    std::uint8_t byte(Address offset) const { return std::uint8_t(memory_->read(at_ + offset, 1)); }
    void set_byte(Address offset, std::uint8_t value) { memory_->write(at_ + offset, value, 1); }
    std::int16_t half(Address offset) const { return std::int16_t(memory_->read(at_ + offset, 2)); }
    void set_half(Address offset, std::int16_t value) { memory_->write(at_ + offset, std::uint16_t(value), 2); }

    std::uint32_t flags() const { return memory_->read(at_ + actor_offset::flags); }
    void raise(std::uint32_t bits) { memory_->write(at_ + actor_offset::flags, flags() | bits); }
    void clear(std::uint32_t bits) { memory_->write(at_ + actor_offset::flags, flags() & ~bits); }
    bool holds(std::uint32_t bits) const { return (flags() & bits) != 0; }

    std::int32_t phase() const { return get(field::phase); }
    void set_phase(std::int32_t value) { set(field::phase, value); }

private:
    Memory* memory_;
    Address at_;
};

}
