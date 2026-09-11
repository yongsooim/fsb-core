#pragma once
#include "fsb_core/debug_state.hpp"
#include "fsb_core/presentation.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>

namespace fsb::host {
// Host-only pixels/input. Explicit development requests are consumed by the host;
// the panel never holds a Runtime reference or writes guest memory.
class DebugPanel {
public:
    DebugPanel(std::vector<std::uint8_t> font,std::filesystem::path directory);
    ~DebugPanel();
    DebugPanel(const DebugPanel&)=delete;
    DebugPanel& operator=(const DebugPanel&)=delete;
    bool visible()const;
    void show(bool enabled);
    int width_points(int window_width)const;
    bool handle(const SDL_Event& event,int window_width,int window_height);
    void reward_boost(bool enabled);
    std::optional<bool> take_reward_boost_request();
    bool wants_snapshot(std::uint64_t now)const;
    void update(core::DebugSnapshot snapshot,std::uint64_t now);
    const core::ImageRgba& image(unsigned width,unsigned height,double dpi);
    std::uint64_t revision()const;
    const core::DebugSnapshot& snapshot()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace fsb::host
