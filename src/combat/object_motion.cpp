#include "fsb_core/combat/object_motion.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::combat {
namespace {
using namespace object_motion;
} // namespace

void ObjectMotion::approach(Address object, std::uint32_t axis, Address position,
                            Address velocity, Address acceleration, Address target) {
    const auto goal = signed32(memory_.read(object + target));
    // Accelerate toward the target: past it, the same acceleration pulls back.
    auto step = memory_.read(object + acceleration);
    if (signed32(memory_.read(object + position)) >= goal) step = 0u - step;
    const auto speed = signed32(memory_.read(object + velocity) + step);
    memory_.write(object + velocity, std::uint32_t(speed));
    const auto moved = signed32(memory_.read(object + position) + std::uint32_t(speed));
    memory_.write(object + position, std::uint32_t(moved));
    // Still short of the target in the direction of travel: keep going.
    if (speed > 0 && moved < goal) return;
    if (speed < 0 && moved > goal) return;
    memory_.write(object + mode, memory_.read(object + mode) & ~axis);
    memory_.write(object + position, std::uint32_t(goal));
}
void ObjectMotion::home_toward_target(Address object) {
    const auto enabled = memory_.read(object + mode);
    if (!(enabled & homing_enabled)) return;
    if (enabled & homing_x)
        approach(object, homing_x, actor_offset::world_x, velocity_x, acceleration_x, target_x);
    if (enabled & homing_y)
        approach(object, homing_y, actor_offset::world_y, velocity_y, acceleration_y, target_y);
    if (enabled & homing_z)
        approach(object, homing_z, actor_offset::elevation, velocity_z, acceleration_z, target_z);
    memory_.write(object + actor_offset::tile_x_q16,
                  std::uint32_t(signed32(memory_.read(object + actor_offset::world_x)) / tile_width));
    memory_.write(object + actor_offset::tile_y_q16,
                  std::uint32_t(signed32(memory_.read(object + actor_offset::world_y)) / tile_height));
}
} // namespace fsb::core::combat
