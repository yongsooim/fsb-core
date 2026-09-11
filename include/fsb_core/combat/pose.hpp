#pragma once
#include "fsb_core/primitives.hpp"

namespace fsb::core::combat {
// Battle pose callbacks. Each scripted character and fixed battle NPC has its
// own original callback that publishes the actor's tile from its Q16 position
// and selects the sprite the battle renderer draws. They are code clones of one
// another, differing only in sprite selector, frame and sprite anchor, so the
// reconstruction keeps one routine per shape and one descriptor per callback.
//
// The callback address is the pose identity: actor records and battle setup
// store it in the actor's callback field, so it is a real identifier here and
// not a stand-in for original control flow.
struct Appearance {
    std::uint32_t selector = 0;          // +134 sprite selector
    std::uint32_t frame = 0;             // +138 sprite frame
    std::int32_t anchor_y = 0;           // +124 sprite anchor, in screen pixels
    std::uint32_t clear_flags = 0;       // cleared in the low byte of +4
    std::uint32_t set_flags = 0x40;      // set in the low byte of +4; 40 shows the actor
};

class Pose {
public:
    explicit Pose(Memory& memory) : memory_(memory) {}
    // Runs the reconstructed pose callback registered at `entry`. Returns false
    // when the address is not one of them.
    bool apply(Address entry, Address actor);
    // Publishes the actor's grid tile from its Q16 tile position.
    void locate(Address actor);
    // 4480e8 instead derives the tile from world pixels, dividing by the tile
    // size after the fixed-point shift. Two truncating divisions are not one.
    void locate_from_world(Address actor);
    void show(Address actor, const Appearance& appearance);

private:
    Memory& memory_;
    // Facing-indexed frame tables address four rotations; the original reads a
    // four-byte local, so a fifth facing has no defined pose.
    std::uint32_t facing_frame(Address actor, Address field, const std::uint8_t (&frames)[4]) const;
    bool alternate(Address actor) const;
};
} // namespace fsb::core::combat
