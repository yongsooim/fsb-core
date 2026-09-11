#pragma once
#include "fsb_core/presentation.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace fsb::host {
using RendererPtr=std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)>;
RendererPtr create_renderer(SDL_Window* window,bool software,bool require_gpu=false);
// Host-owned texture and shader. Scaling has no access to game state.
class HostPresentation {
public:
    explicit HostPresentation(SDL_Renderer* renderer);
    ~HostPresentation();
    HostPresentation(const HostPresentation&)=delete;
    HostPresentation& operator=(const HostPresentation&)=delete;
    void reset();
    void prepare_window();
    void draw(const core::Image8& source,core::Rect destination,core::PresentationMode mode,bool vga6);
    bool gpu_scaling()const{return gpu_scaling_;}
    unsigned texture_width()const{return upload_.width;}
    unsigned texture_height()const{return upload_.height;}
private:
    SDL_Renderer* renderer_;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture_{nullptr,SDL_DestroyTexture};
#if SDL_VERSION_ATLEAST(3,4,0)
    SDL_GPUShader* shader_=nullptr;
    SDL_GPURenderState* state_=nullptr;
#endif
    core::ImageRgba upload_;
    bool gpu_scaling_=false;
    int drawable_width_=0,drawable_height_=0;
};
core::ImageRgba read_renderer(SDL_Renderer* renderer);
}
