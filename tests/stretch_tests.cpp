#include "fsb_core/surfaces.hpp"
#include <iostream>

using namespace fsb::core;

int main(){
    try{
        Memory memory;Surfaces surfaces(memory);unsigned checks=0;
        // Cover SIMD lanes, scalar tails, all four gather registers and the
        // scalar fallback with an independent nearest-neighbour pixel oracle.
        for(unsigned source_width:{1u,7u,32u,64u,65u})
        for(unsigned width:{16u,17u,63u,64u,65u,127u,2049u})
        for(int origin:{-3,0,4})for(bool key:{false,true}){
            Image8 source;source.width=source_width;source.height=3;
            source.pixels.resize(source_width*3);
            for(unsigned i=0;i<source.pixels.size();++i)source.pixels[i]=i%5?std::uint8_t(i%250+1):0;
            Image8 destination;destination.width=width;destination.height=5;
            destination.pixels.assign(width*5,251);auto expected=destination.pixels;
            const Rect rect{-2,0,int(width)+2,5},sample{origin,-1,int(source_width)+2,4};
            for(unsigned y=0;y<5;++y)for(unsigned x=0;x<width;++x){
                const auto sx=sample.left+(int(x)-rect.left)*(sample.right-sample.left)/(rect.right-rect.left);
                const auto sy=sample.top+int(y)*(sample.bottom-sample.top)/(rect.bottom-rect.top);
                if(sx<0||sx>=int(source_width)||sy<0||sy>=3)continue;
                const auto pixel=source.pixels[unsigned(sy)*source_width+unsigned(sx)];
                if(pixel||!key)expected[y*width+x]=pixel;
            }
            const auto src=surfaces.insert(source),dst=surfaces.insert(destination);
            surfaces.stretch(dst,rect,src,sample,key);
            if(surfaces.get(dst).pixels!=expected)throw std::runtime_error("stretch differs from scalar pixel oracle");
            if(surfaces.get(src).pixels!=source.pixels)throw std::runtime_error("stretch modified source pixels");
            surfaces.release(src);surfaces.release(dst);++checks;
        }
        std::cout<<"stretch_checks="<<checks<<" mismatches=0\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
