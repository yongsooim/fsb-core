#pragma once
#include "raster.hpp"

namespace fsb::core {
struct FillCommand { Address surface; Rect rect; std::uint8_t index; };
class Viewport {
public:
    explicit Viewport(Memory& memory) : memory_(memory) {}
    void configure_framebuffer(int width, int height); // Logical game resolution, supplied by host configuration.
    void center(int width, int height, bool publish_clip = true, bool border = true);
    void tween_center(unsigned duration, int width, int height, bool publish_clip = true, bool border = true);
    void event_view(unsigned subop); // 42a2f4 subops0..4; table/resume/dialog routes.
    void set_rect(Rect rect,bool clip=true,bool border=true){publish(rect,clip,border);}
    void blit_borders(std::uint32_t color=0); //406996/4069e9: COLORFILL value, not a source surface.
    void tick(Address object, unsigned jobs);
    std::vector<FillCommand> take_fills();
    static void fill(Image8& target, const FillCommand& command);
private:
    Memory& memory_;
    std::vector<FillCommand> fills_;
    Rect requested() const;
    Rect centered(int width, int height) const;
    void publish(Rect rect, bool clip, bool border);
    void borders(Address block, Rect old_rect, Rect new_rect);
};
} // namespace fsb::core
