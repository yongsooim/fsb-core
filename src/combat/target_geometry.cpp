#include "fsb_core/combat/object_motion.hpp"
#include "fsb_core/symbols.hpp"
#include <cmath>
#include <limits>
namespace fsb::core::combat {
namespace {
// Exact binary64 values stored at4a2808..4a282f in the original EXE.
// Do not replace these finite-decimal constants with std::numbers::pi.
constexpr double half_pi=1.57079632675, three_half_pi=4.71238898025;
constexpr double pi=3.1415926535, two_pi=6.283185307, radians_to_turn=10430.378350768575;
constexpr std::int32_t turn_units=65536;
constexpr unsigned math_domain_error=0x21;
enum class DistanceAxes : unsigned { None=0, X=0x10, Y=0x20, XY=0x30, Z=0x40, XZ=0x50, YZ=0x60, CompositeXY=0x70 };
constexpr unsigned distance_axis_mask=0x70;
// Every finite result in these routines fits int64. Original FTOL returns the
// low32 bits; masked invalid conversion yields integer-indefinite, whose low32 is0.
std::uint32_t truncated_result(double value) {
    return std::isfinite(value)?std::uint32_t(std::int64_t(value)):0;
}
double squared(double value) { return value*value; }
double pair_distance(std::int32_t a,std::int32_t b) {
    // fsb_core disables contraction. The separate products and addition round
    // to binary64 in the original CRT's53-bit arithmetic environment.
    const double first=squared(double(a)),second=squared(double(b));
    return std::sqrt(first+second);
}
double slope(std::int32_t numerator,std::int32_t denominator) {
    if(!denominator)return std::copysign(std::numeric_limits<double>::infinity(),double(numerator));
    return double(numerator)/double(denominator);
}
}
std::uint32_t ObjectMotion::distance_to_target(Address object) const {
    const auto x=memory_.read(object+actor_offset::world_x),y=memory_.read(object+actor_offset::world_y);
    const auto z=memory_.read(object+actor_offset::elevation);
    const auto dx=x-memory_.read(object+object_motion::target_x);
    const auto dy=y-memory_.read(object+object_motion::target_y);
    const auto dz=z-memory_.read(object+object_motion::target_z);
    const auto axes=DistanceAxes(memory_.read(object+object_motion::mode)&distance_axis_mask);
    switch(axes) {
    case DistanceAxes::None:return 0;
    case DistanceAxes::X:return signed_magnitude(dx);
    case DistanceAxes::Y:return signed_magnitude(dy);
    case DistanceAxes::Z:return signed_magnitude(dz);
    case DistanceAxes::XY:return truncated_result(pair_distance(signed32(dx),signed32(dy)));
    case DistanceAxes::XZ:return truncated_result(pair_distance(signed32(dz),signed32(dx)));
    case DistanceAxes::YZ:return truncated_result(pair_distance(signed32(dy),signed32(dz)));
    case DistanceAxes::CompositeXY: {
        // This is the shipped expression: sqrt(sqrt(dx²+dy²)+dy²).
        // The saved dy² is reused after the first root; dz is not consumed here.
        const double y_squared=squared(double(signed32(dy)));
        const double inner=std::sqrt(squared(double(signed32(dx)))+y_squared);
        return truncated_result(std::sqrt(inner+y_squared));
    }
    }
    return 0;
}
std::uint32_t ObjectMotion::heading_to_target(Address object,bool reverse) {
    std::uint32_t dx,dy;
    // Preserve the first failing field read as well as the wrapping differences.
    if(reverse) {
        const auto y=memory_.read(object+actor_offset::world_y),x=memory_.read(object+actor_offset::world_x);
        dy=y-memory_.read(object+object_motion::target_y);dx=x-memory_.read(object+object_motion::target_x);
    } else {
        const auto y=memory_.read(object+object_motion::target_y),x=memory_.read(object+object_motion::target_x);
        dy=y-memory_.read(object+actor_offset::world_y);dx=x-memory_.read(object+actor_offset::world_x);
    }
    if(!dx&&!dy) {
        // Original0/0 -> atan domain tail -> invalid FTOL, not a valid zero-angle computation.
        memory_.write(globals::crt_math_errno,math_domain_error);return 0;
    }
    // ABS(INT_MIN) remains0x80000000, then CMP/JL interprets it as signed.
    // A conventional unsigned magnitude comparison would choose a different axis.
    const bool y_dominant=signed32(signed_magnitude(dx))<signed32(signed_magnitude(dy));
    if(!y_dominant) {
        const double angle=std::atan(slope(signed32(dy),signed32(dx)));
        const double adjusted=angle+(signed32(dx)<0?three_half_pi:half_pi);
        return truncated_result(adjusted*radians_to_turn);
    }
    const double angle=std::atan(slope(signed32(dx),signed32(dy)));
    const double adjusted=(signed32(dy)<0?two_pi:pi)-angle;
    const auto result=truncated_result(adjusted*radians_to_turn);
    return signed32(dy)<0?std::uint32_t(signed32(result)%turn_units):result;
}
}
