#pragma once
#include "primitives.hpp"

namespace fsb::core {
class Camera {
public:
    explicit Camera(Memory& memory) : memory_(memory) {}
    void enter_tile_focus();
    void focus_actor(Address actor);
    Handle spawn_followup(std::uint32_t duration, std::uint32_t x, std::uint32_t y, std::uint32_t actor_id);
    void tick_followup(Address object, unsigned jobs);
    void clamp_target(std::int32_t x, std::int32_t y, bool enabled);
    void update_scroll_bounds(std::int32_t x, std::int32_t y, bool enabled);
    void line_focus(unsigned from,unsigned to,unsigned frames);
    void line_focus(unsigned to,unsigned frames);
    void tick_lines();
private:
    Memory& memory_;
};
} // namespace fsb::core
