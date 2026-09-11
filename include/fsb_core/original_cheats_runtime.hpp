#pragma once
#include "original_cheats.hpp"
#include "fsb_core/runtime.hpp"

namespace fsb::original_cheats {
inline void tick(Runtime& runtime) {
    const auto dispatch = [&](Address entry, const std::vector<std::uint32_t>& args) {
        return runtime.battle.recovered.invoke(entry,args);
    };
    frame(runtime.memory,[&](Address entry,const std::vector<std::uint32_t>& args) {
        if(entry==0x45f600)draw_time(runtime.memory,args.at(0),args.at(1),dispatch);
        else dispatch(entry,args);
    });
}
}
