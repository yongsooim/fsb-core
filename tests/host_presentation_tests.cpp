#include "host_presentation.hpp"
#include <SDL3/SDL_main.h>
#include <iostream>
#include <cstring>
using namespace fsb::core;
void check(bool ok){if(!ok)throw std::runtime_error(SDL_GetError());}
int main(int,char**){try{check(SDL_Init(SDL_INIT_VIDEO));
struct Lifetime{~Lifetime(){SDL_Quit();}} lifetime;
std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> w(SDL_CreateWindow("FSB GPU contract",800,600,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);check(bool(w));fsb::host::RendererPtr r(nullptr,SDL_DestroyRenderer);
try{r=fsb::host::create_renderer(w.get(),false,true);}catch(const std::exception& e){std::cerr<<"GPU device unavailable: "<<e.what()<<'\n';return 77;}SDL_SetRenderVSync(r.get(),0);fsb::host::HostPresentation presenter(r.get());
unsigned tests=0,gpu_tests=0,fallback_tests=0;unsigned seed=987;auto random=[&](){seed=seed*1664525u+1013904223u;return std::uint8_t(seed>>24);};
for(auto size:{std::pair{1u,1u},std::pair{7u,9u},std::pair{256u,1u},std::pair{800u,600u}}){Image8 source;source.width=size.first;source.height=size.second;source.pixels.resize(source.width*source.height);for(auto&p:source.pixels)p=random();for(auto&c:source.palette)c={random(),random(),random()};
if(source.width==256){for(unsigned i=0;i<256;++i){source.pixels[i]=i;source.palette[i]={std::uint8_t(i),std::uint8_t(255-i),std::uint8_t(i^127)};}}
for(unsigned pattern=0;pattern<3;++pattern){if(pattern){SubpixelImage::Tile tile;for(auto&v:tile)v=random();source.set_detail_cell(source.pixels.size()/2,tile);if(pattern==2)source.clear_detail_cell(source.pixels.size()/2);}
for(auto mode:{PresentationMode::original,PresentationMode::enhanced})for(bool vga:{false,true})for(auto output:{std::pair{1u,1u},std::pair{source.width,source.height},std::pair{source.width*2,source.height*2},std::pair{std::max(1u,source.width*3/2),std::max(1u,source.height*3/2)},std::pair{std::max(1u,source.width/2),std::max(1u,source.height/2)},std::pair{source.width+13,source.height+17}}){
std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> target(SDL_CreateTexture(r.get(),SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,output.first+6,output.second+10),SDL_DestroyTexture);check(bool(target));check(SDL_SetRenderTarget(r.get(),target.get()));SDL_SetRenderDrawColor(r.get(),0,0,0,255);check(SDL_RenderClear(r.get()));presenter.draw(source,{3,5,3+int(output.first),5+int(output.second)},mode,vga);const unsigned density=mode==PresentationMode::enhanced&&source.has_detail()?4:1;
const bool bounded=mode==PresentationMode::original||((size_t(source.width)*density+output.first-1)/output.first+1)*((size_t(source.height)*density+output.second-1)/output.second+1)<=4096;
if(presenter.gpu_scaling()!=bounded)throw std::runtime_error("unexpected GPU/fallback selection");if(bounded)++gpu_tests;else ++fallback_tests;auto actual=fsb::host::read_renderer(r.get());const auto expected=present_image(source,output.first,output.second,mode,vga);
for(unsigned y=0;y<output.second;++y)for(unsigned x=0;x<output.first*4;++x)if(actual.pixels[(size_t(y+5)*actual.width+3)*4+x]!=expected.pixels[size_t(y)*output.first*4+x]){std::cerr<<"size="<<source.width<<" pattern="<<pattern<<" mode="<<int(mode)<<" vga="<<vga<<" output="<<output.first<<"x"<<output.second<<" at="<<x<<","<<y<<" actual="<<int(actual.pixels[(size_t(y+5)*actual.width+3)*4+x])<<" expected="<<int(expected.pixels[size_t(y)*output.first*4+x])<<"\n";throw std::runtime_error("GPU mismatch");}
check(SDL_SetRenderTarget(r.get(),nullptr));++tests;if(tests%17==0)presenter.reset();
}
}
}
// Exercise drawable resize/letterbox and reset on the actual window surface.
Image8 resize_source;resize_source.width=80;resize_source.height=60;resize_source.pixels.resize(4800);
for(auto&v:resize_source.pixels)v=random();for(auto&c:resize_source.palette)c={random(),random(),random()};
for(auto size:{std::pair{800,600},std::pair{733,511},std::pair{400,700},std::pair{1000,300},std::pair{800,600}}){
 check(SDL_SetWindowSize(w.get(),size.first,size.second));check(SDL_SyncWindow(w.get()));SDL_Event event;while(SDL_PollEvent(&event)){}
 presenter.prepare_window();
 int width,height;check(SDL_GetRenderOutputSize(r.get(),&width,&height));
 const auto rect=fit_presentation(80,60,width,height,false);SDL_SetRenderDrawColor(r.get(),0,0,0,255);check(SDL_RenderClear(r.get()));
 presenter.draw(resize_source,rect,PresentationMode::enhanced,true);const auto actual=fsb::host::read_renderer(r.get());
 const auto expected=present_image(resize_source,rect.right-rect.left,rect.bottom-rect.top);
 for(unsigned y=0;y<actual.height;++y)for(unsigned x=0;x<actual.width;++x){
  const bool inside=int(x)>=rect.left&&int(x)<rect.right&&int(y)>=rect.top&&int(y)<rect.bottom;
  for(unsigned c=0;c<4;++c){const auto value=inside?expected.pixels[(size_t(y-rect.top)*expected.width+x-rect.left)*4+c]:(c==3?255:0);
   if(actual.pixels[(size_t(y)*actual.width+x)*4+c]!=value){std::cerr<<"resize="<<size.first<<","<<size.second<<" drawable="<<width<<","<<height<<" readback="<<actual.width<<","<<actual.height<<" at="<<x<<","<<y<<","<<c<<" actual="<<int(actual.pixels[(size_t(y)*actual.width+x)*4+c])<<" expected="<<int(value)<<" rect="<<rect.left<<","<<rect.top<<","<<rect.right<<","<<rect.bottom<<"\n";throw std::runtime_error("resized window/letterbox differs");}}
 }
 check(SDL_RenderPresent(r.get()));++tests;++gpu_tests;presenter.reset();
}
std::cout<<"GPU checks="<<tests<<" gpu="<<gpu_tests<<" bounded_cpu_fallback="<<fallback_tests<<" mismatches=0 renderer="<<SDL_GetRendererName(r.get())<<"\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
