#include "fsb_core/progress_timer.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
void ProgressTimer::start(std::uint32_t duration, bool up, bool preserve, std::uint32_t now) {
    const bool flip = preserve && memory_.read(state_+timer_offset::counts_up) != unsigned(up);
    memory_.write(state_, signed32(duration) > 0);
    if (signed32(duration) <= 0) {
        const auto remaining = flip ? complete-memory_.read(state_+timer_offset::remaining) : complete;
        const auto frames = 1u-duration;
        const auto step = std::uint32_t(std::int64_t(complete) / signed32(frames));
        if (!step) throw Fault(state_,"progress timer frame-step would be zero");
        memory_.write(state_+timer_offset::frame_count,frames); memory_.write(state_+timer_offset::frame_step,step);
        memory_.write(state_+timer_offset::remaining,remaining-sequence_alu(alu::signed_remainder,remaining,step));
    } else {
        memory_.write(state_+timer_offset::duration_ms,duration); auto deadline = now+duration;
        if (flip) deadline -= sequence_alu(alu::signed_divide,memory_.read(state_+timer_offset::remaining)*duration,complete);
        memory_.write(state_+timer_offset::deadline_ms,deadline);
    }
    memory_.write(state_+timer_offset::counts_up,up);
}
std::uint32_t ProgressTimer::tick(std::uint32_t now) {
    auto remaining = memory_.read(state_+timer_offset::remaining);
    if (!memory_.read(state_)) {
        remaining -= memory_.read(state_+timer_offset::frame_step); if (signed32(remaining) < 0) remaining = 0;
    } else {
        const auto total = memory_.read(state_+timer_offset::duration_ms), deadline = memory_.read(state_+timer_offset::deadline_ms);
        remaining = total && now <= deadline ? sequence_alu(alu::signed_divide,(deadline-now)*complete,total) : 0;
    }
    memory_.write(state_+timer_offset::remaining,remaining);
    return memory_.read(state_+timer_offset::counts_up) ? complete-remaining : remaining;
}
void ProgressTimer::scale_speed(unsigned numerator, unsigned denominator, std::uint32_t now) {
    if (!numerator || !denominator) throw Fault(state_,"invalid progress speed ratio");
    const auto scale = [&](std::uint32_t value,unsigned n,unsigned d) { return std::uint32_t(std::int64_t(signed32(value))*n/d); };
    //0x406fc8: frame step *=speed; time-domain remaining and total /=speed.
    if (!memory_.read(state_)) memory_.write(state_+timer_offset::frame_step,scale(memory_.read(state_+timer_offset::frame_step),numerator,denominator));
    else {
        memory_.write(state_+timer_offset::deadline_ms,now+scale(memory_.read(state_+timer_offset::deadline_ms)-now,denominator,numerator));
        const auto total = scale(memory_.read(state_+timer_offset::duration_ms),denominator,numerator);
        if (signed32(total) < 0) throw Fault(state_,"scaled timer total became negative");
        memory_.write(state_+timer_offset::duration_ms,total);
    }
}
} // namespace fsb::core
