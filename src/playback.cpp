#include "fsb_core/playback.hpp"
#include <algorithm>
#include <limits>

namespace fsb::core {
void PlaybackClock::set_speed(unsigned speed){
    // The presets the hosts offer. Every rate executes each1ms tick, so the
    // set is a product decision about pacing, not a limit of the clock;4 is
    // here because a phone's two-step fast-forward wanted a gentler first step.
    if(speed!=1&&speed!=4&&speed!=8&&speed!=16)throw Fault(speed,"playback speed must be1,4,8or16");
    const auto previous=this->speed();speed_=speed;if(previous!=this->speed())pending_=0;
}
void PlaybackClock::set_fast_forward_held(bool held){
    const auto previous=speed();fast_forward_held_=held;if(previous!=speed())pending_=0;
}
void PlaybackClock::elapse(std::uint64_t elapsed,bool active){
    // A suspended process/long blocking load must not turn into a giant time
    // jump on resume. Bounded debt also keeps speed changes responsive when
    // the requested rate is higher than the device can execute.
    constexpr unsigned max_debt_ms=1000;
    if(!active||elapsed>max_debt_ms){pending_=0;return;}
    pending_=unsigned(std::min<std::uint64_t>(max_debt_ms,pending_+elapsed*speed()));
}
bool PlaybackClock::step(){
    if(!pending_)return false;
    if(now_==std::numeric_limits<std::uint32_t>::max())throw Fault(now_,"playback clock exhausted");
    --pending_;++now_;return true;
}
} // namespace fsb::core
