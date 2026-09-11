#include "host_presentation.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#if SDL_VERSION_ATLEAST(3,4,0)
#include "shaders/presentation_shaders.hpp"
#endif
namespace fsb::host {
namespace {
void check(bool ok){if(!ok)throw std::runtime_error(SDL_GetError());}
core::ImageRgba source_texture(const core::Image8& source,core::PresentationMode mode,bool vga6){
    if(mode==core::PresentationMode::original||!source.has_detail())
        return core::present_image(source,source.width,source.height,core::PresentationMode::original,vga6);
    // Detail is source data, not a resize to the window: unpack its exact
    // samples once per source cell, without averaging or applying a filter.
    constexpr auto scale=core::SubpixelImage::scale;
    core::ImageRgba out{source.width*scale,source.height*scale,{}};
    out.pixels.resize(std::size_t(out.width)*out.height*4);
    std::array<std::array<std::uint8_t,4>,256> palette;
    for(unsigned i=0;i<256;++i){auto c=source.palette[i];if(vga6){const auto expand=[](unsigned v){return std::uint8_t(((v>>2)<<2)|(v>>6));};c={expand(c.r),expand(c.g),expand(c.b)};}palette[i]={c.r,c.g,c.b,255};}
    for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x)
        std::memcpy(out.pixels.data()+(std::size_t(y)*out.width+x)*4,palette[source.detail_pixel(x,y)].data(),4);
    return out;
}
}
RendererPtr create_renderer(SDL_Window* window,bool software,bool require_gpu){
    SDL_Renderer* result=nullptr;
#if SDL_VERSION_ATLEAST(3,4,0)
    if(!software){
        const auto props=SDL_CreateProperties();check(props!=0);
        SDL_SetStringProperty(props,SDL_PROP_RENDERER_CREATE_NAME_STRING,"gpu");
        SDL_SetPointerProperty(props,SDL_PROP_RENDERER_CREATE_WINDOW_POINTER,window);
        SDL_SetBooleanProperty(props,SDL_PROP_RENDERER_CREATE_GPU_SHADERS_MSL_BOOLEAN,true);
        SDL_SetBooleanProperty(props,SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN,true);
        result=SDL_CreateRendererWithProperties(props);SDL_DestroyProperties(props);
    }
#endif
    if(!result&&require_gpu)throw std::runtime_error(std::string("GPU renderer unavailable: ")+SDL_GetError());
    if(!result)result=SDL_CreateRenderer(window,software?SDL_SOFTWARE_RENDERER:nullptr);
    if(!result&&!software)result=SDL_CreateRenderer(window,SDL_SOFTWARE_RENDERER);
    check(result!=nullptr);return {result,SDL_DestroyRenderer};
}
HostPresentation::HostPresentation(SDL_Renderer* renderer):renderer_(renderer){reset();}
HostPresentation::~HostPresentation(){
#if SDL_VERSION_ATLEAST(3,4,0)
    if(state_)SDL_DestroyGPURenderState(state_);
    if(shader_)SDL_ReleaseGPUShader(SDL_GetGPURendererDevice(renderer_),shader_);
#endif
}
void HostPresentation::reset(){
    texture_.reset();upload_={};gpu_scaling_=false;drawable_width_=drawable_height_=0;
#if SDL_VERSION_ATLEAST(3,4,0)
    if(state_)SDL_DestroyGPURenderState(state_);state_=nullptr;
    if(shader_)SDL_ReleaseGPUShader(SDL_GetGPURendererDevice(renderer_),shader_);shader_=nullptr;
    if(std::strcmp(SDL_GetRendererName(renderer_),"gpu")!=0)return;
    auto* device=SDL_GetGPURendererDevice(renderer_);check(device!=nullptr);
    const auto formats=SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info{};info.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;info.num_samplers=1;info.num_uniform_buffers=1;
    if(formats&SDL_GPU_SHADERFORMAT_SPIRV){info.format=SDL_GPU_SHADERFORMAT_SPIRV;info.code=shaders::spirv;info.code_size=sizeof(shaders::spirv);info.entrypoint="main";}
    else if(formats&SDL_GPU_SHADERFORMAT_MSL){info.format=SDL_GPU_SHADERFORMAT_MSL;info.code=shaders::msl;info.code_size=sizeof(shaders::msl);info.entrypoint="main0";}
    else throw std::runtime_error("GPU renderer has no supported presentation shader format");
    shader_=SDL_CreateGPUShader(device,&info);check(shader_!=nullptr);
    SDL_GPURenderStateCreateInfo create{};create.fragment_shader=shader_;
    state_=SDL_CreateGPURenderState(renderer_,&create);
    if(!state_){SDL_ReleaseGPUShader(device,shader_);shader_=nullptr;check(false);}
#endif
}
void HostPresentation::prepare_window(){
    int width=0,height=0;check(SDL_GetRenderOutputSize(renderer_,&width,&height));
    if(width<1||height<1)return;
    if(width==drawable_width_&&height==drawable_height_)return;
    if(std::strcmp(SDL_GetRendererName(renderer_),"gpu")==0){
        // SDL 3.4's GPU renderer replaces its backbuffer when acquiring the
        // swapchain in RenderPresent, not when the window resize is reported.
        // Retain the previous completed frame while that resize is committed;
        // then draw and read back against the new dimensions, never stale ones.
        if(!drawable_width_){check(SDL_SetRenderDrawColor(renderer_,0,0,0,255));check(SDL_RenderClear(renderer_));}
        check(SDL_RenderPresent(renderer_));
    }
    drawable_width_=width;drawable_height_=height;
}
void HostPresentation::draw(const core::Image8& source,core::Rect destination,core::PresentationMode mode,bool vga6){
    const auto width=unsigned(destination.right-destination.left),height=unsigned(destination.bottom-destination.top);
    const unsigned scale=mode==core::PresentationMode::enhanced&&source.has_detail()?core::SubpixelImage::scale:1;
    gpu_scaling_=false;
#if SDL_VERSION_ATLEAST(3,4,0)
    const auto max_texture=SDL_GetNumberProperty(SDL_GetRendererProperties(renderer_),SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER,8192);
    const auto samples_x=width?(std::uint64_t(source.width)*scale+width-1)/width+1:0;
    const auto samples_y=height?(std::uint64_t(source.height)*scale+height-1)/height+1:0;
    // The shader uses exact uint arithmetic. Keep extreme developer-selected
    // source sizes on the 64-bit CPU reference rather than allowing overflow.
    gpu_scaling_=state_&&width&&height&&width<=8192&&height<=8192&&
        std::uint64_t(source.width)*scale<=std::uint64_t(max_texture)&&std::uint64_t(source.height)*scale<=std::uint64_t(max_texture)&&
        std::uint64_t(source.width)*source.height*scale*scale<=std::numeric_limits<std::uint32_t>::max()/256u&&
        (mode==core::PresentationMode::original||samples_x*samples_y<=4096);
    // Extremely small targets can require millions of serial samples per
    // fragment (e.g. a detailed frame reduced to one pixel). Some drivers end
    // that invocation without a result. Bound it and preserve exact CPU output.
#endif
    auto next=gpu_scaling_?source_texture(source,mode,vga6):core::present_image(source,width,height,mode,vga6);
    if(!texture_||upload_.width!=next.width||upload_.height!=next.height){
        texture_.reset(SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,int(next.width),int(next.height)));check(bool(texture_));
        check(SDL_SetTextureBlendMode(texture_.get(),SDL_BLENDMODE_NONE));check(SDL_SetTextureScaleMode(texture_.get(),SDL_SCALEMODE_NEAREST));
    }
    upload_=std::move(next);check(SDL_UpdateTexture(texture_.get(),nullptr,upload_.pixels.data(),int(upload_.width)*4));
#if SDL_VERSION_ATLEAST(3,4,0)
    // An identity copy needs no area shader or uniform upload.
    const bool filter=gpu_scaling_&&(upload_.width!=width||upload_.height!=height);
    if(filter){
        const std::array<std::uint32_t,8> uniforms{upload_.width,upload_.height,width,height,unsigned(destination.left),unsigned(destination.top),mode==core::PresentationMode::original?1u:0u,0};
        check(SDL_SetGPURenderStateFragmentUniforms(state_,0,uniforms.data(),sizeof(uniforms)));check(SDL_SetGPURenderState(renderer_,state_));
    }
#endif
    const SDL_FRect rect{float(destination.left),float(destination.top),float(width),float(height)};
    const bool drawn=SDL_RenderTexture(renderer_,texture_.get(),nullptr,&rect);
#if SDL_VERSION_ATLEAST(3,4,0)
    if(filter)check(SDL_SetGPURenderState(renderer_,nullptr));
#endif
    check(drawn);
}
core::ImageRgba read_renderer(SDL_Renderer* renderer){
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> shot(SDL_RenderReadPixels(renderer,nullptr),SDL_DestroySurface);check(bool(shot));
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> converted(shot->format==SDL_PIXELFORMAT_RGBA32?nullptr:SDL_ConvertSurface(shot.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    if(shot->format!=SDL_PIXELFORMAT_RGBA32)check(bool(converted));const auto& pixels=converted?*converted:*shot;
    core::ImageRgba out{unsigned(pixels.w),unsigned(pixels.h),{}};out.pixels.resize(std::size_t(out.width)*out.height*4);
    for(unsigned y=0;y<out.height;++y)std::memcpy(out.pixels.data()+std::size_t(y)*out.width*4,static_cast<const std::uint8_t*>(pixels.pixels)+std::size_t(y)*pixels.pitch,std::size_t(out.width)*4);
    return out;
}
}
