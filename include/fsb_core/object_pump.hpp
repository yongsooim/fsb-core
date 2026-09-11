#pragma once
#include "arena.hpp"
#include <functional>

namespace fsb::core {
struct ObjectDraw {
    Address object, destination, source;
    std::int32_t x, y, left, top, right, bottom;
    std::uint32_t flags;
};
// Dispatch/draw hooks connect translated core subsystems, never OS callbacks.
// Absent handlers fail. This is the original object-group primitive, not a
// replacement for the game's dispatcher between early and late group phases.
class ObjectPump {
public:
    using Dispatch = std::function<void(Address callback, Address object, unsigned jobs)>;
    using Draw = std::function<void(const ObjectDraw&)>;
    ObjectPump(Memory& memory, Arena& arena, Dispatch dispatch, Draw draw = {})
        : memory_(memory), arena_(arena), dispatch_(std::move(dispatch)), draw_(std::move(draw)) {}
    void update_group(unsigned group, unsigned jobs, const std::function<bool()>& stop = {});
    void update_object(Address object, unsigned jobs);
    void latch_frame_time(std::uint32_t now);
private:
    Memory& memory_;
    Arena& arena_;
    Dispatch dispatch_;
    Draw draw_;
};
} // namespace fsb::core
