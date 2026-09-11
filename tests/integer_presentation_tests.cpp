#include "fsb_core/presentation.hpp"
#include "fixtures/presentation_reference.cpp"
#include <iostream>
#include <cstring>
using namespace fsb::core;
int main(){try{
 unsigned seed=9876,checks=0;auto random=[&](){seed=1664525u*seed+1013904223u;return std::uint8_t(seed>>24);};
 for(auto size:{std::pair{1u,1u},std::pair{7u,9u},std::pair{800u,600u}}){
  Image8 source;source.width=size.first;source.height=size.second;source.pixels.resize(size.first*size.second);
  for(auto&v:source.pixels)v=random();for(auto&c:source.palette)c={random(),random(),random()};
  for(unsigned pattern=0;pattern<3;++pattern){
   if(pattern){SubpixelImage::Tile tile;for(auto&v:tile)v=random();source.set_detail_cell(source.pixels.size()/2,tile);if(pattern==2)source.clear_detail_cell(source.pixels.size()/2);}
   const auto before=source;
   for(auto mode:{PresentationMode::original,PresentationMode::enhanced})for(bool vga:{false,true})
    for(auto factor:{std::pair{1u,2u},std::pair{2u,1u},std::pair{2u,2u},std::pair{3u,3u},std::pair{4u,4u},std::pair{3u,2u},std::pair{5u,3u}}){
     const auto w=size.first*factor.first,h=size.second*factor.second;
     const auto result=present_image(source,w,h,mode,vga),expected=reference_present_image(source,w,h,mode,vga);
     if(result.width!=expected.width||result.height!=expected.height||result.pixels!=expected.pixels)throw std::runtime_error("integer magnification differs from frozen area filter");++checks;
    }
   if(source.pixels!=before.pixels||std::memcmp(source.palette.data(),before.palette.data(),sizeof(source.palette))||bool(source.detail)!=bool(before.detail))throw std::runtime_error("source mutated");
   if(source.detail&&(source.detail->cells!=before.detail->cells||source.detail->tiles!=before.detail->tiles||source.detail->free_tiles!=before.detail->free_tiles))throw std::runtime_error("source detail mutated");
  }
 }
 // All channel values, including VGA6 expansion boundaries.
 Image8 ramp;ramp.width=256;ramp.height=1;ramp.pixels.resize(256);for(unsigned i=0;i<256;++i){ramp.pixels[i]=i;ramp.palette[i]={std::uint8_t(i),std::uint8_t(255-i),std::uint8_t(i^127)};}
 for(bool vga:{false,true})for(unsigned k=2;k<=7;++k){if(present_image(ramp,256*k,k,PresentationMode::enhanced,vga).pixels!=reference_present_image(ramp,256*k,k,PresentationMode::enhanced,vga).pixels)throw std::runtime_error("palette ramp mismatch");++checks;}
 std::cout<<"integer_presentation_checks="<<checks<<" mismatches=0\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
