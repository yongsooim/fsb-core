#pragma once
#include "primitives.hpp"

namespace fsb::core {
// Host-independent playback pacing. Each successful step advances exactly1ms;
// the caller still executes Runtime::advance(now()) for every step. Speed
// changes and stalls discard only unexecuted pacing debt, never game ticks.
class PlaybackClock {
public:
    explicit PlaybackClock(unsigned speed=1){set_speed(speed);}
    void set_speed(unsigned speed);
    unsigned speed()const{return fast_forward_held_?16:speed_;}
    void set_fast_forward_held(bool held);
    bool fast_forward_held()const{return fast_forward_held_;}
    void elapse(std::uint64_t real_ms,bool active=true);
    bool step();
    std::uint32_t now()const{return now_;}
    unsigned pending()const{return pending_;}
private:
    unsigned speed_=1,pending_=0;
    std::uint32_t now_=0;
    bool fast_forward_held_=false;
};
} // namespace fsb::core
