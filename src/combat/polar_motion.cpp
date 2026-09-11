#include "fsb_core/combat/object_motion.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/symbols.hpp"
namespace fsb::core::combat {
void ObjectMotion::polar_step(Address object,std::uint32_t angle,std::int32_t radius) {
    const auto mode=memory_.read(object+object_motion::mode);
    constexpr unsigned absolute=2,relative=4;
    if(!(mode&(absolute|relative)))return;
    // FILD(int32) products fit exactly in the x87 significand; scaling by
    // 2^-16 followed by FTOL is precisely this signed integer division.
    const auto offset=[&](std::uint32_t wave) {
        return std::uint32_t((std::int64_t(signed32(wave))*radius)/units::q16_one);
    };
    const auto x=offset(fixed_sin(memory_,angle));
    const auto source_x=(mode&absolute)?object_motion::target_x:actor_offset::world_x;
    memory_.write(object+actor_offset::world_x,memory_.read(object+source_x)+x);
    const auto y=offset(fixed_cos(memory_,angle));
    const auto source_y=(mode&absolute)?object_motion::target_y:actor_offset::world_y;
    memory_.write(object+actor_offset::world_y,memory_.read(object+source_y)-y);
}
}
