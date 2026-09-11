// Frozen pre-optimization area-filter oracle; do not regenerate from optimized code.
#include "fsb_core/presentation.hpp"
#include <algorithm>
namespace fsb::core {
namespace {
struct Contribution {unsigned cell;std::uint64_t weight;std::array<std::uint64_t,SubpixelImage::scale> samples{};};
using Axis=std::vector<std::vector<Contribution>>;
Axis area_axis(unsigned input,unsigned output){
    constexpr unsigned s=SubpixelImage::scale;Axis axis(output);
    for(unsigned p=0;p<output;++p){
        const auto low=std::uint64_t(p)*input*s,high=std::uint64_t(p+1)*input*s,cell_width=std::uint64_t(output)*s;
        for(unsigned cell=unsigned(low/cell_width);cell<input&&std::uint64_t(cell)*cell_width<high;++cell){
            Contribution c{};c.cell=cell;
            for(unsigned i=0;i<s;++i){const auto a=std::max(low,(std::uint64_t(cell)*s+i)*output),b=std::min(high,(std::uint64_t(cell)*s+i+1)*output);if(b>a)c.weight+=(c.samples[i]=b-a);}
            axis[p].push_back(c);
        }
    }return axis;
}
}
ImageRgba reference_present_image(const Image8& image,unsigned width,unsigned height,PresentationMode mode,bool vga6){
    if(!width||!height||width>8192||height>8192||std::uint64_t(width)*height>33554432||!image.width||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height)throw Fault(0,"invalid presentation image size");
    ImageRgba result{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
    auto palette=image.palette;if(vga6)for(auto& c:palette){const auto expand=[](unsigned x){return std::uint8_t(((x>>2)<<2)|(x>>6));};c={expand(c.r),expand(c.g),expand(c.b)};}
    if(mode==PresentationMode::original){
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto sx=unsigned((std::uint64_t(x)*2+1)*image.width/(width*2)),sy=unsigned((std::uint64_t(y)*2+1)*image.height/(height*2));const auto c=palette[image.pixels[std::size_t(sy)*image.width+sx]];const auto at=(std::size_t(y)*width+x)*4;result.pixels[at]=c.r;result.pixels[at+1]=c.g;result.pixels[at+2]=c.b;result.pixels[at+3]=255;}return result;
    }
    const auto xs=area_axis(image.width,width),ys=area_axis(image.height,height);
    const auto denominator=std::uint64_t(image.width)*image.height*SubpixelImage::scale*SubpixelImage::scale;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto at=(std::size_t(y)*width+x)*4;
        // Most sprite pixels are uniform cells. Integer output scales need
        // no filtering work there; reserve subpixel integration for text.
        if(xs[x].size()==1&&ys[y].size()==1){
            const auto index=std::size_t(ys[y][0].cell)*image.width+xs[x][0].cell;
            if(!image.detail_cell(index)){const auto c=palette[image.pixels[index]];result.pixels[at]=c.r;result.pixels[at+1]=c.g;result.pixels[at+2]=c.b;result.pixels[at+3]=255;continue;}
        }
        std::uint64_t red=0,green=0,blue=0;
        const auto add=[&](std::uint8_t index,std::uint64_t weight){const auto c=palette[index];red+=c.r*weight;green+=c.g*weight;blue+=c.b*weight;};
        for(const auto& cy:ys[y])for(const auto& cx:xs[x]){
            const auto index=std::size_t(cy.cell)*image.width+cx.cell;
            if(const auto tile=image.detail_cell(index)){
                constexpr auto s=SubpixelImage::scale;
                for(unsigned iy=0;iy<s;++iy)if(cy.samples[iy])for(unsigned ix=0;ix<s;++ix)if(cx.samples[ix])add((*tile)[iy*s+ix],cy.samples[iy]*cx.samples[ix]);
            }else add(image.pixels[index],cy.weight*cx.weight);
        }
        result.pixels[at]=std::uint8_t((red+denominator/2)/denominator);result.pixels[at+1]=std::uint8_t((green+denominator/2)/denominator);result.pixels[at+2]=std::uint8_t((blue+denominator/2)/denominator);result.pixels[at+3]=255;
    }
    return result;
}
} // namespace fsb::core
