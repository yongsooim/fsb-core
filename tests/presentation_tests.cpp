#include "fsb_core/presentation.hpp"
#include "fsb_core/presentation_update.hpp"
#include "fsb_core/surfaces.hpp"
#include "../tools/lab_io.hpp"
#ifdef FSB_TEST_TEXT
#include "fsb_core/font_raster.hpp"
#endif
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
using namespace fsb::core;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
Image8 indexed(unsigned w,unsigned h){Image8 im;im.width=w;im.height=h;im.pixels.resize(std::size_t(w)*h);for(unsigned i=0;i<256;++i)im.palette[i]={std::uint8_t(i),std::uint8_t(i),std::uint8_t(i)};return im;}
constexpr auto scale=SubpixelImage::scale;
Image8 expanded(const Image8& im){auto out=indexed(im.width*scale,im.height*scale);for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x)out.pixels[std::size_t(y)*out.width+x]=im.detail_pixel(x,y);return out;}
Rect scaled(Rect r){return {r.left*int(scale),r.top*int(scale),r.right*int(scale),r.bottom*int(scale)};}
bool same_detail(const Image8& sparse,const Image8& dense){const auto rgba=present_image(sparse,dense.width,dense.height,PresentationMode::enhanced,false);for(unsigned i=0;i<dense.pixels.size();++i)if(rgba.pixels[i*4]!=dense.pixels[i])return false;return true;}
// Scalar pre-optimization contract, intentionally kept independent of the
// production span intersection/bulk-copy implementation.
void scalar_blit(Image8& target,const Image8& source,int x,int y,Rect rect,bool key,std::optional<Rect> clip){
    const auto input=source;
    for(int sy=std::max(0,rect.top);sy<std::min(int(input.height),rect.bottom);++sy)
        for(int sx=std::max(0,rect.left);sx<std::min(int(input.width),rect.right);++sx){
            const auto dx=std::int64_t(x)+sx-rect.left,dy=std::int64_t(y)+sy-rect.top;
            if(dx<0||dy<0||dx>=target.width||dy>=target.height||(clip&&(dx<clip->left||dy<clip->top||dx>=clip->right||dy>=clip->bottom)))continue;
            const auto at=std::size_t(dy)*target.width+std::size_t(dx),from=std::size_t(sy)*input.width+std::size_t(sx);
            if(const auto detail=input.detail_cell(from)){
                auto tile=*detail;for(unsigned i=0;i<tile.size();++i)if(key&&!tile[i]){const auto old=target.detail_cell(at);tile[i]=old?(*old)[i]:target.pixels[at];}
                target.set_detail_cell(at,tile);
            }else if(!key||input.pixels[from])target.clear_detail_cell(at);
            if(!key||input.pixels[from])target.pixels[at]=input.pixels[from];
        }
}
// Scalar pre-optimization contract for the scaled path: one coordinate
// transform and one subpixel tile per destination pixel, no axis tables or
// footprint shortcuts.
void scalar_stretch(Image8& target,const Image8& source,Rect rect,Rect sr,bool key,std::optional<Rect> clip){
    const auto width=std::int64_t(rect.right)-rect.left,height=std::int64_t(rect.bottom)-rect.top;
    if(width<=0||height<=0||sr.right<=sr.left||sr.bottom<=sr.top)return;
    const auto input=source;const auto& pixels=input.pixels;
    for(int y=std::max(0,rect.top),end_y=std::min(int(target.height),rect.bottom);y<end_y;++y)
        for(int x=std::max(0,rect.left),end_x=std::min(int(target.width),rect.right);x<end_x;++x){
            if(clip&&(x<clip->left||y<clip->top||x>=clip->right||y>=clip->bottom))continue;
            const auto sx=sr.left+(std::int64_t(x)-rect.left)*(sr.right-sr.left)/width;
            const auto sy=sr.top+(std::int64_t(y)-rect.top)*(sr.bottom-sr.top)/height;
            if(sx<0||sy<0||sx>=input.width||sy>=input.height)continue;
            const auto at=std::size_t(y)*target.width+x;const auto pixel=pixels[std::size_t(sy)*input.width+sx];
            // Subpixel sampling applies only where the footprint holds ink;
            // flat art takes the plain indexed path at any tile size.
            int cx0=-1,cx1=-1,cy0=-1,cy1=-1;
            for(unsigned j=0;j<scale;++j){
                const auto px=std::int64_t(sr.left)*scale+((std::int64_t(x)-rect.left)*scale+j)*(sr.right-sr.left)/width;
                if(px>=0&&px<std::int64_t(input.width)*scale){const int c=int(px/scale);if(cx0<0)cx0=c;cx1=c;}
                const auto py=std::int64_t(sr.top)*scale+((std::int64_t(y)-rect.top)*scale+j)*(sr.bottom-sr.top)/height;
                if(py>=0&&py<std::int64_t(input.height)*scale){const int c=int(py/scale);if(cy0<0)cy0=c;cy1=c;}
            }
            bool ink=false;
            if(cx1>=0&&cy1>=0)for(int c=cy0;c<=cy1&&!ink;++c)for(int e=cx0;e<=cx1&&!ink;++e)
                if(input.detail_cell(std::size_t(c)*input.width+e))ink=true;
            if(ink){
                SubpixelImage::Tile tile;if(const auto previous=target.detail_cell(at))tile=*previous;else tile.fill(target.pixels[at]);
                for(unsigned iy=0;iy<scale;++iy)for(unsigned ix=0;ix<scale;++ix){
                    const auto px=std::int64_t(sr.left)*scale+((std::int64_t(x)-rect.left)*scale+ix)*(sr.right-sr.left)/width;
                    const auto py=std::int64_t(sr.top)*scale+((std::int64_t(y)-rect.top)*scale+iy)*(sr.bottom-sr.top)/height;
                    if(px<0||py<0||px>=input.width*scale||py>=input.height*scale)continue;
                    const auto color=input.detail_pixel(unsigned(px),unsigned(py));if(color||!key)tile[iy*scale+ix]=color;
                }
                const auto legacy=(pixel||!key)?pixel:target.pixels[at];
                if(std::all_of(tile.begin(),tile.end(),[&](auto c){return c==legacy;}))target.clear_detail_cell(at);else target.set_detail_cell(at,tile);
            }else if(pixel||!key)target.clear_detail_cell(at);
            if(pixel||!key)target.pixels[at]=pixel;
        }
}
}
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;const std::filesystem::path assets=argv[1],out=argv[2];std::filesystem::create_directories(out);
        auto two=indexed(2,1);two.pixels={0,255};const auto area=present_image(two,3,1,PresentationMode::enhanced,false);
        check(area.pixels[0]==0&&area.pixels[4]==128&&area.pixels[8]==255,"fractional area filter preserves coverage instead of uneven nearest widths");
        auto checker=indexed(8,8);for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)checker.pixels[y*8+x]=((x+y)&1)?255:0;
        const auto reduced=present_image(checker,3,3,PresentationMode::enhanced,false);check(std::all_of(reduced.pixels.begin(),reduced.pixels.end(),[](auto c){return c==120||c==128||c==135||c==255;}),"downsampling integrates repeated texture without point-sample aliasing");
        check(present_image(checker,16,16,PresentationMode::enhanced,false).pixels==present_image(checker,16,16,PresentationMode::original,false).pixels,"integer magnification preserves original sprite pixels");
        auto cell=indexed(1,1);cell.pixels[0]=255;SubpixelImage::Tile half{};for(unsigned y=0;y<scale;++y)for(unsigned x=0;x<scale/2;++x)half[y*scale+x]=255;cell.set_detail_cell(0,half);
        // A partly inked cell needs at least two samples per axis to exist, so
        // these two only apply while the detail plane carries subpixels.
        if(scale>1){
            const auto single=present_image(cell,1,1,PresentationMode::enhanced,false),double_size=present_image(cell,2,2,PresentationMode::enhanced,false);
            check(single.pixels[0]==128&&double_size.pixels[0]==255&&double_size.pixels[4]==0,"high-resolution ink is integrated at actual output pixel density");
        }
        check(present_image(cell,1,1,PresentationMode::original,false).pixels[0]==255&&cell.pixels[0]==255,"original output ignores presentation detail without changing it");
        cell.palette[255]={100,20,0};
        if(scale>1){const auto recolored=present_image(cell,1,1,PresentationMode::enhanced,false);check(recolored.pixels[0]==50&&recolored.pixels[1]==10,"palette changes recolor existing high-resolution ink");}
        cell.clear_detail_cell(0);const auto allocated=cell.detail->tiles.size();for(unsigned i=0;i<100;++i){cell.set_detail_cell(0,half);cell.clear_detail_cell(0);}check(cell.detail->tiles.size()==allocated&&!cell.has_detail(),"detail cells are recycled during repeated drawing and clearing");
        for(auto dimensions:{std::pair{960u,720u},std::pair{1920u,1440u},std::pair{3840u,2880u}}){const auto r=fit_presentation(640,480,dimensions.first,dimensions.second);check(r.left==0&&r.top==0&&r.right==int(dimensions.first)&&r.bottom==int(dimensions.second),"physical HiDPI output fills a matching aspect ratio");}
        const auto wide=fit_presentation(640,480,1920,1080),integer=fit_presentation(640,480,960,720,true);
        check(wide.left==240&&wide.right==1680&&wide.top==0&&wide.bottom==1080,"widescreen output uses centered letterboxing");
        check(integer.left==160&&integer.top==120&&integer.right==800&&integer.bottom==600,"optional integer scaling preserves complete pixels");
        auto m=Memory::from_pe32(fsb::lab::read(assets/"FLYINGSB.EXE"));Surfaces surfaces(m);std::mt19937 random(1784);unsigned mismatches=0;
        for(unsigned test=0;test<96;++test){
            auto source=indexed(7,6),target=indexed(9,8);for(auto& c:source.pixels)c=std::uint8_t(random()%4);for(auto& c:target.pixels)c=std::uint8_t(random()%4);
            for(unsigned i=0;i<8;++i){SubpixelImage::Tile tile;for(auto& c:tile)c=std::uint8_t(random()%4);source.set_detail_cell(random()%source.pixels.size(),tile);target.set_detail_cell(random()%target.pixels.size(),tile);}
            // The dense reference resamples every pixel, so the stretch cases
            // need a source whose footprints all hold ink.
            if(test%3==1)for(std::size_t c=0;c<source.pixels.size();++c)if(!source.detail_cell(c)){SubpixelImage::Tile tile;tile.fill(source.pixels[c]);source.set_detail_cell(c,tile);}
            const auto src=surfaces.insert(source),dst=surfaces.insert(target),dense_src=surfaces.insert(expanded(source)),dense_dst=surfaces.insert(expanded(target));
            const Rect sr{-1+int(test%3),int(test%2),6,6},dr{-2+int(test%4),-1+int(test%3),8,7},clip{1,1,8,7};const bool key=test&1;
            if(test%3==0){surfaces.blit(dst,dr.left,dr.top,src,sr,key,clip);surfaces.blit(dense_dst,dr.left*int(scale),dr.top*int(scale),dense_src,scaled(sr),key,scaled(clip));}
            else if(test%3==1){surfaces.stretch(dst,dr,src,sr,key,clip);surfaces.stretch(dense_dst,scaled(dr),dense_src,scaled(sr),key,scaled(clip));}
            else{const Rect from{1,1,7,6};surfaces.blit(dst,2,2,dst,from,key,clip);surfaces.blit(dense_dst,2*int(scale),2*int(scale),dense_dst,scaled(from),key,scaled(clip));}
            if(!same_detail(surfaces.get(dst),surfaces.get(dense_dst)))++mismatches;
            surfaces.clear(dst,{2,1,8,4},3);surfaces.clear(dense_dst,scaled({2,1,8,4}),3);if(!same_detail(surfaces.get(dst),surfaces.get(dense_dst)))++mismatches;
            for(auto id:{src,dst,dense_src,dense_dst})surfaces.release(id);
        }
        check(mismatches==0,"96sparse high-resolution blit/stretch/clip/colour-key/self-copy cases match independent dense pixel rendering");
        unsigned span_mismatches=0;
        for(unsigned test=0;test<384;++test){
            auto source=indexed(7,6),target=indexed(9,8);for(auto& c:source.pixels)c=std::uint8_t(random()%4);for(auto& c:target.pixels)c=std::uint8_t(random()%4);
            for(unsigned i=0;i<8;++i){SubpixelImage::Tile tile;for(auto& c:tile)c=std::uint8_t(random()%4);if(test&1)source.set_detail_cell(random()%source.pixels.size(),tile);if(test&2)target.set_detail_cell(random()%target.pixels.size(),tile);}
            const auto src=surfaces.insert(source),dst=surfaces.insert(target);auto expected=target;
            const int x=int(random()%15)-7,y=int(random()%13)-6;Rect rect{int(random()%9)-4,int(random()%7)-3,int(random()%10),int(random()%9)};
            if(test%13==0)rect={std::numeric_limits<int>::min(),-1,std::numeric_limits<int>::max(),7};
            const auto clip=test&4?std::optional<Rect>{{1,0,8,7}}:std::nullopt;const bool key=test&8,self=test&16;
            scalar_blit(expected,self?expected:source,x,y,rect,key,clip);surfaces.blit(dst,x,y,self?dst:src,rect,key,clip);
            if(expected.pixels!=surfaces.get(dst).pixels||expanded(expected).pixels!=expanded(surfaces.get(dst)).pixels)++span_mismatches;
            // Also exercise surfaces whose detail allocation exists but has
            // become empty, then refill it after optimized clear/copy paths.
            const Rect clear{int(test%4)-2,-1,8,7};for(int cy=0;cy<7;++cy)for(int cx=std::max(0,clear.left);cx<8;++cx)expected.set_pixel(cy*expected.width+cx,2);
            surfaces.clear(dst,clear,2);
            if(expected.pixels!=surfaces.get(dst).pixels||expanded(expected).pixels!=expanded(surfaces.get(dst)).pixels)++span_mismatches;
            surfaces.release(src);surfaces.release(dst);
        }
        check(span_mismatches==0,"384optimized span copies/clears match scalar pixels and detail with clipping, empty bounds, extreme coordinates and self-copy");
        unsigned scale_mismatches=0;
        for(unsigned test=0;test<512;++test){
            auto source=indexed(11,9),target=indexed(13,11);for(auto& c:source.pixels)c=std::uint8_t(random()%4);for(auto& c:target.pixels)c=std::uint8_t(random()%4);
            for(unsigned i=0;i<8;++i){SubpixelImage::Tile tile;for(auto& c:tile)c=std::uint8_t(random()%4);if(test&1)source.set_detail_cell(random()%source.pixels.size(),tile);if(test&2)target.set_detail_cell(random()%target.pixels.size(),tile);}
            const auto src=surfaces.insert(source),dst=surfaces.insert(target);auto expected=target;
            // Magnification, minification and the fractional scales in between,
            // where a destination pixel straddles two source cells per axis.
            Rect rect{int(random()%17)-4,int(random()%15)-4,0,0};rect.right=rect.left+1+int(random()%18);rect.bottom=rect.top+1+int(random()%16);
            Rect sr{int(random()%13)-2,int(random()%11)-2,0,0};sr.right=sr.left+1+int(random()%13);sr.bottom=sr.top+1+int(random()%11);
            const auto clip=test&4?std::optional<Rect>{{1,0,11,9}}:std::nullopt;const bool key=test&8,self=test&16;
            scalar_stretch(expected,self?expected:source,rect,sr,key,clip);surfaces.stretch(dst,rect,self?dst:src,sr,key,clip);
            if(expected.pixels!=surfaces.get(dst).pixels||expanded(expected).pixels!=expanded(surfaces.get(dst)).pixels)++scale_mismatches;
            surfaces.release(src);surfaces.release(dst);
        }
        check(scale_mismatches==0,"512optimized stretches match the scalar sampling contract across scales, clipping, colour key and self-copy");
#ifdef FSB_TEST_TEXT
        FontRaster fonts(m);fonts.load(fsb::lab::read(assets/"fonts/gulim.ttc"),fsb::lab::read(assets/"fonts/batang.ttc"),fsb::lab::read(assets/"fonts/cp949.bin"));
        auto original=indexed(160,64),enhanced=original;std::vector<DialogGlyph> requests;
        for(unsigned i=0;i<7;++i)requests.push_back({std::uint16_t(0xb0a1+i),9,{int(i)*21-2,12,int(i)*21+18,30},i==4?0x200u:i==5?0x400u:0x20u,0,255,30,1});
        for(const auto& glyph:requests)fonts.draw(original,glyph);const auto advance=fonts.advance(9,0xb0a1);fonts.set_enhanced(true);for(const auto& glyph:requests)fonts.draw(enhanced,glyph);
        check(original.pixels==enhanced.pixels&&fonts.advance(9,0xb0a1)==advance,"enhanced font leaves original monochrome pixels and layout advances byte-identical");
        const auto text=present_image(enhanced,480,192,PresentationMode::enhanced,false);check(enhanced.has_detail()&&text.pixels!=present_image(original,480,192,PresentationMode::original,false).pixels,"font is redrawn from scalable outlines, not a filtered low-resolution bitmap");
        fsb::lab::bmp(text,out/"enhanced-font.bmp");fsb::lab::bmp(present_image(original,480,192,PresentationMode::original,false),out/"original-font.bmp");

#endif
        {
            auto source=indexed(5,3);ImageRgba output;
            auto dirty=update_presentation(source,output,5,3,PresentationMode::enhanced,false);
            check(dirty.left==0&&dirty.top==0&&dirty.right==5&&dirty.bottom==3,"first presentation invalidates the full output");
            dirty=update_presentation(source,output,5,3,PresentationMode::enhanced,false);
            check(dirty.right==0&&dirty.bottom==0,"identical output has no dirty rectangle");
            source.pixels[13]=255;dirty=update_presentation(source,output,5,3,PresentationMode::enhanced,false);
            check(dirty.left<=3&&dirty.right>3&&dirty.top==2&&dirty.bottom==3&&output.pixels[13*4]==255,"odd-width tail and changed pixel are included");
            SubpixelImage::Tile tile;tile.fill(128);source.set_detail_cell(7,tile);
            dirty=update_presentation(source,output,5,3,PresentationMode::enhanced,false);
            check(dirty.left<=2&&dirty.right>2&&dirty.top==1&&dirty.bottom==2&&output.pixels[7*4]==128,"text changes are detected without changing indexed pixels");
            update_presentation(source,output,5,3,PresentationMode::original,false);source.clear_detail_cell(7);
            dirty=update_presentation(source,output,5,3,PresentationMode::original,false);
            check(dirty.right==0,"original mode ignores presentation detail changes");
            dirty=update_presentation(source,output,5,3,PresentationMode::original,false,false);
            check(dirty.right==5&&dirty.bottom==3,"forced repaint bypasses unchanged-frame detection");
        }
        std::cout<<"presentation_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
