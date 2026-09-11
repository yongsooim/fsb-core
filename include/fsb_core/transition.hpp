#pragma once
#include "surfaces.hpp"

namespace fsb::core {
class Transition {
public:
    Transition(Memory& memory,Surfaces& surfaces):memory_(memory),surfaces_(surfaces){}
    void start(unsigned kind,unsigned mode,std::uint32_t duration,Address actor);
    void rectangle(unsigned kind,unsigned mode,Rect source,std::uint32_t duration,Address anchor_pointer=0);
    void zoom(unsigned mode,Rect source,std::uint32_t duration,std::optional<Rect> destination=std::nullopt);
    bool present(std::uint32_t now); // True when the original frame-idle hook handled presentation.
private:
    Memory& memory_;
    Surfaces& surfaces_;
    void buffers();
    void begin(unsigned mode,std::uint32_t duration);
    unsigned tick(std::uint32_t now);
};
} // namespace fsb::core
