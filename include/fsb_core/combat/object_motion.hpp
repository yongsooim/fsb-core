#pragma once
#include "fsb_core/primitives.hpp"

namespace fsb::core::combat {
namespace object_motion {
// Battle effect objects carry their own motion beside the actor fields:
// a per-axis acceleration and velocity, and the point they are heading for.
inline constexpr Address mode = 0x168;
inline constexpr Address velocity_x = 0x16c, velocity_y = 0x170, velocity_z = 0x174;
inline constexpr Address acceleration_x = 0x178, acceleration_y = 0x17c, acceleration_z = 0x180;
inline constexpr Address target_x = 0x184, target_y = 0x188, target_z = 0x18c;
// mode bit0 enables the motion at all; one bit per axis still travelling.
inline constexpr std::uint32_t homing_enabled = 1;
inline constexpr std::uint32_t homing_x = 0x10, homing_y = 0x20, homing_z = 0x40;
// World pixels per tile, used to republish the object's tile position.
inline constexpr std::int32_t tile_width = 0x40, tile_height = 0x30;
} // namespace object_motion

// Motion shared by the battle's effect objects. Every field lives in the
// existing object record; this owns no copy of it.
class ObjectMotion {
public:
    explicit ObjectMotion(Memory& memory) : memory_(memory) {}
    // 462de2: accelerate each enabled axis toward its target, snapping to the
    // target and dropping that axis once it arrives, then republish the tile
    // position the renderer sorts by.
    void home_toward_target(Address object);
    //4631a2: original has three arguments; Q16 waves times a signed radius.
    void polar_step(Address object,std::uint32_t angle,std::int32_t radius);
    //462f5b: the original axis selection includes its composite XY metric.
    std::uint32_t distance_to_target(Address object) const;
    //463038/4630ed: original quadrant rules; reverse measures target->current.
    std::uint32_t heading_to_target(Address object,bool reverse);

private:
    Memory& memory_;
    // Runs one axis. `enabled` is read from the mode word the call started with,
    // so clearing one axis never changes whether the next one runs.
    void approach(Address object, std::uint32_t axis, Address position,
                  Address velocity, Address acceleration, Address target);
};
} // namespace fsb::core::combat
