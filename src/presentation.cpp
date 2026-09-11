#include "fsb_core/presentation_update.hpp"
#include <bit>
#include <cstring>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif
#include <algorithm>

namespace fsb::core {
void Image8::set_pixel(std::size_t index,std::uint8_t value){clear_detail_cell(index);pixels[index]=value;}
void Image8::set_detail_pixel(unsigned x,unsigned y,std::uint8_t value){
    constexpr auto s=SubpixelImage::scale;const auto index=std::size_t(y/s)*width+x/s;
    if(!detail_cell(index)){SubpixelImage::Tile tile;tile.fill(pixels[index]);set_detail_cell(index,tile);}
    detail->tiles[detail->cells[index]-1][(y%s)*s+x%s]=value;
}
std::uint8_t Image8::detail_pixel(unsigned x,unsigned y)const{
    constexpr auto s=SubpixelImage::scale;const auto index=std::size_t(y/s)*width+x/s;
    if(const auto tile=detail_cell(index))return (*tile)[(y%s)*s+x%s];return pixels[index];
}
Rect fit_presentation(unsigned sw,unsigned sh,unsigned ow,unsigned oh,bool integer){
    if(!sw||!sh||!ow||!oh||ow>16384||oh>16384)throw Fault(0,"invalid presentation dimensions");
    unsigned w,h;const auto factor=std::min(ow/sw,oh/sh);
    if(integer&&factor){w=sw*factor;h=sh*factor;}
    else if(std::uint64_t(ow)*sh<=std::uint64_t(oh)*sw){w=ow;h=std::max(1u,unsigned(std::uint64_t(ow)*sh/sw));}
    else{h=oh;w=std::max(1u,unsigned(std::uint64_t(oh)*sw/sh));}
    const int x=int((ow-w)/2),y=int((oh-h)/2);return {x,y,x+int(w),y+int(h)};
}
namespace {
void validate_presentation(const Image8& image,unsigned width,unsigned height){
    if(!width||!height||width>8192||height>8192||std::uint64_t(width)*height>33554432||!image.width||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height)throw Fault(0,"invalid presentation image size");
}
using RgbaPalette=std::array<std::uint32_t,256>;
std::uint32_t packed_rgba(unsigned r,unsigned g,unsigned b){
    return std::bit_cast<std::uint32_t>(std::array<std::uint8_t,4>{std::uint8_t(r),std::uint8_t(g),std::uint8_t(b),255});
}
RgbaPalette rgba_palette(const Image8& image,bool vga6){
    RgbaPalette palette;
    for(unsigned i=0;i<256;++i){auto c=image.palette[i];if(vga6){const auto expand=[](unsigned x){return std::uint8_t(((x>>2)<<2)|(x>>6));};c={expand(c.r),expand(c.g),expand(c.b)};}palette[i]=packed_rgba(c.r,c.g,c.b);}
    return palette;
}
template<bool Detail> std::uint32_t rgba_pixel(const Image8& image,const RgbaPalette& palette,std::size_t at){
    if constexpr(Detail){
        if(const auto id=image.detail->cells[at]){
            unsigned r=0,g=0,b=0;
            for(auto index:image.detail->tiles[id-1]){const auto c=std::bit_cast<std::array<std::uint8_t,4>>(palette[index]);r+=c[0];g+=c[1];b+=c[2];}
            constexpr auto samples=SubpixelImage::scale*SubpixelImage::scale;
            return packed_rgba((r+samples/2)/samples,(g+samples/2)/samples,(b+samples/2)/samples);
        }
    }
    return palette[image.pixels[at]];
}
template<bool Detail,bool Compare> Rect fill_unscaled(const Image8& image,ImageRgba& output,const RgbaPalette& palette){
    Rect dirty{int(image.width),int(image.height),0,0};
    for(unsigned y=0;y<image.height;++y){
        unsigned left=image.width,right=0,x=0;const auto row=std::size_t(y)*image.width;
        for(;x+4<=image.width;x+=4){
            const auto at=row+x;auto* destination=output.pixels.data()+at*4;
            const std::array<std::uint32_t,4> values{rgba_pixel<Detail>(image,palette,at),rgba_pixel<Detail>(image,palette,at+1),rgba_pixel<Detail>(image,palette,at+2),rgba_pixel<Detail>(image,palette,at+3)};
#ifdef __wasm_simd128__
            const auto rgba=wasm_v128_load(values.data());
            if constexpr(Compare)if(!wasm_v128_any_true(wasm_v128_xor(rgba,wasm_v128_load(destination))))continue;
            wasm_v128_store(destination,rgba);
#else
            if constexpr(Compare)if(std::memcmp(destination,values.data(),16)==0)continue;
            std::memcpy(destination,values.data(),16);
#endif
            if constexpr(Compare){if(left==image.width)left=x;right=x+4;}
        }
        for(;x<image.width;++x){
            const auto value=rgba_pixel<Detail>(image,palette,row+x);auto* destination=output.pixels.data()+(row+x)*4;
            if constexpr(Compare){std::uint32_t old;std::memcpy(&old,destination,4);if(old==value)continue;}
            std::memcpy(destination,&value,4);
            if constexpr(Compare){if(left==image.width)left=x;right=x+1;}
        }
        if constexpr(Compare)if(right){dirty.left=std::min(dirty.left,int(left));dirty.right=std::max(dirty.right,int(right));dirty.top=std::min(dirty.top,int(y));dirty.bottom=int(y+1);}
    }
    if constexpr(!Compare)return {0,0,int(image.width),int(image.height)};
    return dirty.right?dirty:Rect{};
}
Rect update_unscaled(const Image8& image,ImageRgba& output,PresentationMode mode,bool vga6,bool compare){
    compare=compare&&output.width==image.width&&output.height==image.height&&output.pixels.size()==image.pixels.size()*4;
    output.width=image.width;output.height=image.height;output.pixels.resize(image.pixels.size()*4);
    const auto palette=rgba_palette(image,vga6);
    const bool detail=mode!=PresentationMode::original&&bool(image.detail);
    if(detail)return compare?fill_unscaled<true,true>(image,output,palette):fill_unscaled<true,false>(image,output,palette);
    return compare?fill_unscaled<false,true>(image,output,palette):fill_unscaled<false,false>(image,output,palette);
}
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
ImageRgba present_image(const Image8& image,unsigned width,unsigned height,PresentationMode mode,bool vga6){
    validate_presentation(image,width,height);
    if(width==image.width&&height==image.height){ImageRgba output;update_unscaled(image,output,mode,vga6,false);return output;}
    ImageRgba result{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
    // Integer magnification of flat indexed cells has no filter boundaries
    // inside a source pixel. Convert one row, then duplicate its exact bytes.
    // Enhanced detail still needs the area filter; original mode ignores it.
    if(width%image.width==0&&height%image.height==0&&(mode==PresentationMode::original||!image.has_detail())){
        const auto palette=rgba_palette(image,vga6);
        const auto sx=width/image.width,sy=height/image.height;
        const auto pitch=std::size_t(width)*4;
        for(unsigned y=0;y<image.height;++y){
            auto* row=result.pixels.data()+std::size_t(y)*sy*pitch;
            for(unsigned x=0;x<image.width;++x){
                const auto color=palette[image.pixels[std::size_t(y)*image.width+x]];
                for(unsigned k=0;k<sx;++k)std::memcpy(row+(std::size_t(x)*sx+k)*4,&color,4);
            }
            for(unsigned k=1;k<sy;++k)std::memcpy(row+std::size_t(k)*pitch,row,pitch);
        }
        return result;
    }
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
Rect update_presentation(const Image8& image,ImageRgba& output,unsigned width,unsigned height,PresentationMode mode,bool vga6,bool detect_changes){
    validate_presentation(image,width,height);
    if(width==image.width&&height==image.height)return update_unscaled(image,output,mode,vga6,detect_changes);
    auto next=present_image(image,width,height,mode,vga6);
    const bool changed=!detect_changes||output.width!=width||output.height!=height||output.pixels!=next.pixels;
    output=std::move(next);
    return changed?Rect{0,0,int(width),int(height)}:Rect{};
}
} // namespace fsb::core
