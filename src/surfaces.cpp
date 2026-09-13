#include "fsb_core/surfaces.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
// A scaled row is a gather out of a 64-byte window, so the whole fast path is
// one table lookup per sixteen destination pixels. Every target below has a
// 16-byte shuffle; they differ only in what an out-of-range index yields, which
// is what the three fetch() bodies encode.
#if defined(__ARM_NEON)
#include <arm_neon.h>
#define FSB_STRETCH_SIMD 1
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define FSB_STRETCH_SIMD 1
#elif defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define FSB_STRETCH_SIMD 1
#define FSB_STRETCH_SIMD_RUNTIME_CHECK 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#define FSB_STRETCH_TARGET __attribute__((target("sse4.1")))
#endif
#endif
#ifndef FSB_STRETCH_TARGET
#define FSB_STRETCH_TARGET
#endif

namespace fsb::core {
namespace {
#ifdef FSB_STRETCH_SIMD
// Sixteen bytes picked out of a 64-byte window. Indices at or past 64 must read
// as zero, which the colour key then treats as "leave the destination alone".
struct Gather64 {
#if defined(__ARM_NEON)
    uint8x16x4_t window;
    explicit Gather64(const std::uint8_t* p):window{vld1q_u8(p),vld1q_u8(p+16),vld1q_u8(p+32),vld1q_u8(p+48)}{}
    // vqtbl4q_u8 already zeroes out-of-range indices across all four registers.
    void blend(std::uint8_t* out,const std::uint8_t* index)const{
        const auto fetched=vqtbl4q_u8(window,vld1q_u8(index));
        vst1q_u8(out,vbslq_u8(vceqzq_u8(fetched),vld1q_u8(out),fetched));
    }
#elif defined(__wasm_simd128__)
    v128_t q[4];
    explicit Gather64(const std::uint8_t* p){for(int i=0;i<4;++i)q[i]=wasm_v128_load(p+i*16);}
    // i8x16.swizzle zeroes any index outside 0..15, so shifting the index down
    // by one register per step leaves exactly one lane contributing.
    void blend(std::uint8_t* out,const std::uint8_t* index)const{
        v128_t i=wasm_v128_load(index),step=wasm_i8x16_splat(16),fetched=wasm_i8x16_swizzle(q[0],i);
        for(int k=1;k<4;++k){i=wasm_i8x16_sub(i,step);fetched=wasm_v128_or(fetched,wasm_i8x16_swizzle(q[k],i));}
        const auto dst=wasm_v128_load(out);
        wasm_v128_store(out,wasm_v128_bitselect(dst,fetched,wasm_i8x16_eq(fetched,wasm_i8x16_splat(0))));
    }
#else
    __m128i q[4];
    explicit Gather64(const std::uint8_t* p){for(int i=0;i<4;++i)q[i]=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+i*16));}
    // pshufb keeps only the low four index bits and zeroes a byte when bit 7 is
    // set, so each register is shuffled unconditionally and bits 4 and 5 pick
    // the winner. An out-of-range 0xff zeroes all four and survives the blends.
    FSB_STRETCH_TARGET void blend(std::uint8_t* out,const std::uint8_t* index)const{
        const auto i=_mm_loadu_si128(reinterpret_cast<const __m128i*>(index));
        const auto r01=_mm_blendv_epi8(_mm_shuffle_epi8(q[0],i),_mm_shuffle_epi8(q[1],i),_mm_slli_epi16(i,3));
        const auto r23=_mm_blendv_epi8(_mm_shuffle_epi8(q[2],i),_mm_shuffle_epi8(q[3],i),_mm_slli_epi16(i,3));
        const auto fetched=_mm_blendv_epi8(r01,r23,_mm_slli_epi16(i,2));
        const auto dst=_mm_loadu_si128(reinterpret_cast<const __m128i*>(out));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out),
            _mm_blendv_epi8(fetched,dst,_mm_cmpeq_epi8(fetched,_mm_setzero_si128())));
    }
#endif
};
FSB_STRETCH_TARGET void gather_row(std::uint8_t* out,const std::uint8_t* window,const std::uint8_t* offsets,int count){
    const Gather64 table(window);
    for(int i=0;i+16<=count;i+=16)table.blend(out+i,offsets+i);
}
inline bool gather_available(){
#ifdef FSB_STRETCH_SIMD_RUNTIME_CHECK
#if defined(_MSC_VER)
    int registers[4];
    __cpuid(registers,1);
    return (registers[2]&(1<<19))!=0;
#else
    return __builtin_cpu_supports("sse4.1");
#endif
#else
    return true;
#endif
}
#endif
void clear_detail(Image8& target,std::size_t at){
    if(target.detail){auto& detail=*target.detail;auto& id=detail.cells[at];if(id){detail.free_tiles.push_back(id);id=0;}}
}
void copy_detail(Image8& target,std::size_t at,const Image8& source,std::size_t from,bool key){
    const auto id=source.detail?source.detail->cells[from]:0;
    if(id){
        auto result=source.detail->tiles[id-1];
        if(key){const auto previous=target.detail_cell(at);for(unsigned i=0;i<result.size();++i)if(!result[i])result[i]=previous?(*previous)[i]:target.pixels[at];}
        target.set_detail_cell(at,result);
    }else if(source.pixels[from]||!key)clear_detail(target,at);
}
// Destination-to-source mapping for one stretch axis, computed once per call
// instead of once per pixel. coarse drives the indexed pixel and sub drives the
// high-resolution samples; first/last bound the source cells those samples
// actually land in, so a destination pixel can tell whether its footprint holds
// any high-resolution ink. last is -1 when no sample falls inside the source.
struct StretchAxis {
    std::vector<int> coarse,first,last;std::vector<std::array<int,SubpixelImage::scale>> sub;
};
StretchAxis stretch_axis(int begin,int end,int origin,int source_begin,std::int64_t source_extent,std::int64_t extent,int limit,bool detail){
    constexpr auto s=int(SubpixelImage::scale);StretchAxis axis;const auto count=std::size_t(end-begin);
    axis.coarse.resize(count);if(detail){axis.first.assign(count,-1);axis.last.assign(count,-1);axis.sub.resize(count);}
    for(std::size_t i=0;i<count;++i){
        const auto k=std::int64_t(begin)+std::int64_t(i)-origin,coarse=source_begin+k*source_extent/extent;
        axis.coarse[i]=(coarse<0||coarse>=limit)?-1:int(coarse);
        if(!detail)continue;
        auto& sub=axis.sub[i];
        for(int j=0;j<s;++j){
            const auto p=std::int64_t(source_begin)*s+(k*s+j)*source_extent/extent;
            if(p<0||p>=std::int64_t(limit)*s){sub[j]=-1;continue;}
            sub[j]=int(p);if(axis.first[i]<0)axis.first[i]=int(p)/s;axis.last[i]=int(p)/s;
        }
    }
    return axis;
}
}
Address Surfaces::insert(Image8 image) {
    if (!image.width||!image.height||image.width>8192||image.height>8192||image.pixels.size()!=std::size_t(image.width)*image.height) throw Fault(0,"invalid indexed surface");
    const auto id=memory_.allocate_zeroed(16);memory_.write(id,image.width);memory_.write(id+4,image.height);memory_.write(id+8,image.width);
    images_.emplace(id,std::move(image));return id;
}
Address Surfaces::create(unsigned width,unsigned height) {
    if (!width||!height||width>8192||height>8192) throw Fault(0,"invalid surface dimensions");
    Image8 image;image.width=width;image.height=height;image.pixels.resize(std::size_t(width)*height);return insert(std::move(image));
}
Image8& Surfaces::get(Address id) {const auto it=images_.find(id);if(it==images_.end())throw Fault(id,"unknown surface ID");return it->second;}
const Image8& Surfaces::get(Address id) const {const auto it=images_.find(id);if(it==images_.end())throw Fault(id,"unknown surface ID");return it->second;}
void Surfaces::release(Address id) {if(!id)return;if(!images_.erase(id))throw Fault(id,"invalid surface release");memory_.release_allocation(id);}
void Surfaces::blit(Address dst,int x,int y,Address src,Rect rect,bool key,std::optional<Rect> clip) {
    auto& target=get(dst);const auto& source=get(src);
    // Intersect once in source coordinates, using wide offsets even for
    // extreme signed rectangles. The inner loops now touch only valid spans.
    const auto dx=std::int64_t(x)-rect.left,dy=std::int64_t(y)-rect.top;
    auto left=std::max({std::int64_t(rect.left),std::int64_t(0),-dx});
    auto top=std::max({std::int64_t(rect.top),std::int64_t(0),-dy});
    auto right=std::min({std::int64_t(rect.right),std::int64_t(source.width),std::int64_t(target.width)-dx});
    auto bottom=std::min({std::int64_t(rect.bottom),std::int64_t(source.height),std::int64_t(target.height)-dy});
    if(clip){left=std::max(left,std::int64_t(clip->left)-dx);top=std::max(top,std::int64_t(clip->top)-dy);right=std::min(right,std::int64_t(clip->right)-dx);bottom=std::min(bottom,std::int64_t(clip->bottom)-dy);}
    if(left>=right||top>=bottom)return;
    // Retain the old snapshot semantics for overlapping self-copies.
    const auto copy=dst==src?std::optional<Image8>(source):std::nullopt;const auto& input=copy?*copy:source;const auto& pixels=input.pixels;
    const bool detail=input.has_detail()||target.has_detail();const auto count=std::size_t(right-left);
    for(auto sy=top;sy<bottom;++sy){
        const auto from=std::size_t(sy)*source.width+std::size_t(left),at=std::size_t(sy+dy)*target.width+std::size_t(left+dx);
        if(detail)for(std::size_t i=0;i<count;++i)copy_detail(target,at+i,input,from+i,key);
        if(!key)std::copy_n(pixels.data()+from,count,target.pixels.data()+at);
        else for(std::size_t i=0;i<count;++i)if(pixels[from+i])target.pixels[at+i]=pixels[from+i];
    }
}
void Surfaces::clear(Address dst,Rect rect,std::uint8_t color) {
    auto& target=get(dst);
    const int left=std::max(0,rect.left),right=std::min(int(target.width),rect.right);if(left>=right)return;
    const bool detail=target.has_detail();
    for(int y=std::max(0,rect.top),end_y=std::min(int(target.height),rect.bottom);y<end_y;++y)
    {
        const auto at=std::size_t(y)*target.width+left,count=std::size_t(right-left);
        if(detail)for(std::size_t i=0;i<count;++i)clear_detail(target,at+i);
        std::fill_n(target.pixels.data()+at,count,color);
    }
}
void Surfaces::stretch(Address dst,Rect rect,Address src,Rect sr,bool key,std::optional<Rect> clip) {
    constexpr auto s=SubpixelImage::scale;
    const auto width=std::int64_t(rect.right)-rect.left,height=std::int64_t(rect.bottom)-rect.top;
    if(width<=0||height<=0||sr.right<=sr.left||sr.bottom<=sr.top)return;
    auto& target=get(dst);const auto& source=get(src);
    const auto copy=dst==src?std::optional<Image8>(source):std::nullopt;const auto& input=copy?*copy:source;const auto& pixels=input.pixels;
    // The clip rectangle only ever removes whole rows and columns, so fold it
    // into the bounds once and keep the inner loops free of the test.
    int left=std::max(0,rect.left),right=std::min(int(target.width),rect.right);
    int top=std::max(0,rect.top),bottom=std::min(int(target.height),rect.bottom);
    if(clip){left=std::max(left,clip->left);right=std::min(right,clip->right);top=std::max(top,clip->top);bottom=std::min(bottom,clip->bottom);}
    if(left>=right||top>=bottom)return;
    const bool detail=input.has_detail();
    const auto horizontal=stretch_axis(left,right,rect.left,sr.left,std::int64_t(sr.right)-sr.left,width,int(input.width),detail);
    const auto vertical=stretch_axis(top,bottom,rect.top,sr.top,std::int64_t(sr.bottom)-sr.top,height,int(input.height),detail);
    const auto* cells=detail?input.detail->cells.data():nullptr;const auto* tiles=detail?input.detail->tiles.data():nullptr;
#if defined(FSB_STRETCH_SIMD) && !defined(FSB_NO_SIMD_STRETCH)
    // Scaled sprite rows are a gather from one source row: dest[i] = src[coarse[i]].
    // Measured on the field replay, 95% of stretches carry the colour key and 80%
    // of destination pixels read a source span of 64 bytes or less, which is one
    // Gather64 window. The byte offsets do not depend on y, so they are built once
    // and reused for every row.
    const auto count=right-left;
    if(!detail&&!target.detail&&key&&count>=16&&count<=2048&&gather_available()){
        int lo=INT_MAX,hi=INT_MIN;
        for(int i=0;i<count;++i){const auto c=horizontal.coarse[i];if(c>=0){lo=std::min(lo,c);hi=std::max(hi,c);}}
        if(lo<=hi&&hi-lo<64){
            std::uint8_t offsets[2048];
            for(int i=0;i<count;++i){const auto c=horizontal.coarse[i];offsets[i]=c<0?255:std::uint8_t(c-lo);}
            std::uint8_t window[64];
            for(int y=top;y<bottom;++y){
                const auto sy=vertical.coarse[y-top];if(sy<0)continue;
                const auto row=std::size_t(sy)*input.width+std::size_t(lo),at_row=std::size_t(y)*target.width;
                const std::uint8_t* base;
                if(row+64<=pixels.size())base=pixels.data()+row;
                else{const auto have=pixels.size()-row;std::memcpy(window,pixels.data()+row,have);std::memset(window+have,0,64-have);base=window;}
                auto* out=target.pixels.data()+at_row+left;
                gather_row(out,base,offsets,count);
                for(int i=count-count%16;i<count;++i){const auto c=horizontal.coarse[i];if(c<0)continue;const auto v=pixels[std::size_t(sy)*input.width+std::size_t(c)];if(v)out[i]=v;}
            }
            return;
        }
    }
#endif
    for(int y=top;y<bottom;++y){
        const auto sy=vertical.coarse[y-top];if(sy<0)continue;
        const auto row=std::size_t(sy)*input.width,at_row=std::size_t(y)*target.width;
        for(int x=left;x<right;++x) {
            const auto sx=horizontal.coarse[x-left];if(sx<0)continue;
            const auto at=at_row+x;const auto pixel=pixels[row+sx];
            if(detail){
                // Only footprints that actually hold high-resolution ink are
                // worth resampling at subpixel density. Flat art takes the plain
                // indexed path, so its cost does not grow with the tile size.
                bool ink=false;
                const auto x_first=horizontal.first[x-left],x_last=horizontal.last[x-left];
                const auto y_first=vertical.first[y-top],y_last=vertical.last[y-top];
                if(x_last>=0&&y_last>=0)
                    for(int cy=y_first;cy<=y_last&&!ink;++cy)for(int cx=x_first;cx<=x_last&&!ink;++cx)
                        if(cells[std::size_t(cy)*input.width+cx])ink=true;
                if(ink){
                    SubpixelImage::Tile tile;if(const auto previous=target.detail_cell(at))tile=*previous;else tile.fill(target.pixels[at]);
                    const auto& rows=vertical.sub[y-top];const auto& columns=horizontal.sub[x-left];
                    for(unsigned iy=0;iy<s;++iy){
                        const auto py=rows[iy];if(py<0)continue;
                        const auto sample_row=std::size_t(py/s)*input.width;const auto sample_top=unsigned(py%s)*s;
                        int cached=-1;const SubpixelImage::Tile* cell=nullptr;std::uint8_t plain=0;
                        for(unsigned ix=0;ix<s;++ix){
                            const auto px=columns[ix];if(px<0)continue;
                            if(px/int(s)!=cached){cached=px/int(s);const auto index=sample_row+unsigned(cached);const auto id=cells[index];cell=id?tiles+(id-1):nullptr;plain=pixels[index];}
                            const auto color=cell?(*cell)[sample_top+unsigned(px%int(s))]:plain;if(color||!key)tile[iy*s+ix]=color;
                        }
                    }
                    const auto legacy=(pixel||!key)?pixel:target.pixels[at];
                    if(std::all_of(tile.begin(),tile.end(),[&](auto c){return c==legacy;}))target.clear_detail_cell(at);else target.set_detail_cell(at,tile);
                }else if(pixel||!key)target.clear_detail_cell(at);
            }else if(pixel||!key)target.clear_detail_cell(at);
            if(pixel||!key)target.pixels[at]=pixel;
        }
    }
}
} // namespace fsb::core
