#pragma once
#include <cstdint>

namespace fsb::core {
// Actual persistent values, independent of PE addresses and platform services.
struct RandomState {
    std::uint32_t crt_seed=0;
    std::uint32_t sequence_seed=0;
    static std::uint32_t advance_crt(std::uint32_t& seed) {
        constexpr std::uint32_t multiplier=214013,increment=2531011;
        seed=seed*multiplier+increment;
        return (seed>>16)&0x7fff;
    }
    std::uint32_t next_crt(){return advance_crt(crt_seed);}
    std::uint32_t next_sequence(){sequence_seed=sequence_seed*0x41c64e6du+0x3039u;return sequence_seed;}
};
}
