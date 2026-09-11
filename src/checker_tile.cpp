#include "fsb_core/raster.hpp"
#include <algorithm>

namespace fsb::core {
void draw_checker_tile(Image8& target,const Image8& source,Rect rect,int x,int y,bool transparent,std::uint8_t fill,std::optional<Rect> clip){
    const auto dx=std::int64_t(x)-rect.left,dy=std::int64_t(y)-rect.top;
    auto left=std::max({std::int64_t(rect.left),std::int64_t(0),-dx});
    auto top=std::max({std::int64_t(rect.top),std::int64_t(0),-dy});
    auto right=std::min({std::int64_t(rect.right),std::int64_t(source.width),std::int64_t(target.width)-dx});
    auto bottom=std::min({std::int64_t(rect.bottom),std::int64_t(source.height),std::int64_t(target.height)-dy});
    if(clip){left=std::max(left,std::int64_t(clip->left)-dx);top=std::max(top,std::int64_t(clip->top)-dy);right=std::min(right,std::int64_t(clip->right)-dx);bottom=std::min(bottom,std::int64_t(clip->bottom)-dy);}
    const auto width=right-left,height=bottom-top;if(width<=1||height<=1)return; // Actual helpers' small-region guard.
    const auto copy=&target==&source?std::optional<Image8>(source):std::nullopt;const auto& input=copy?*copy:source;
    const auto put=[&](std::size_t at,std::size_t from){
        const auto pixel=input.pixels[from];if(transparent&&!pixel)return;
        if(const auto tile=input.detail_cell(from))target.set_detail_cell(at,*tile);else target.clear_detail_cell(at);
        target.pixels[at]=pixel;
    };
    unsigned parity=unsigned(left+dx+top+dy)&1;
    for(auto row=0;row<height;++row){
        parity^=1;const auto from=std::size_t(top+row)*input.width+std::size_t(left),at=std::size_t(top+dy+row)*target.width+std::size_t(left+dx);
        if(transparent){
            // Original439237 copies width/2 cells even for an odd width.
            for(auto i=0;i<width/2;++i){const auto col=std::size_t(i*2+parity);put(at+col,from+col);}
        }else for(auto col=0;col<width;++col){if((unsigned(col)&1)==parity)put(at+col,from+col);else if(fill)target.set_pixel(at+col,fill);}
    }
}
} // namespace fsb::core
