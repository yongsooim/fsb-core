#pragma once
#include "primitives.hpp"

namespace fsb::core {
class ProgressTimer {
public:
    static constexpr std::uint32_t complete = 30030;
    ProgressTimer(Memory& memory, Address state) : memory_(memory), state_(state) {}
    void start(std::uint32_t duration, bool count_up, bool preserve_direction_flip, std::uint32_t now);
    std::uint32_t tick(std::uint32_t now);
    void scale_speed(unsigned numerator, unsigned denominator, std::uint32_t now);
private:
    Memory& memory_; Address state_;
};
} // namespace fsb::core
