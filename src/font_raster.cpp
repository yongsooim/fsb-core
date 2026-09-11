#include "fsb_core/font_raster.hpp"
#include "fsb_core/symbols.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_DRIVER_H
#include FT_TRUETYPE_TABLES_H
#include <map>
#include <algorithm>

namespace fsb::core {
struct FontRaster::Impl {
    const Memory& memory;
    FT_Library library=nullptr;
    std::array<FT_Face,4> faces{};
    std::vector<std::uint8_t> gulim,batang,cp949;
    struct Bitmap{int width,height,left,top,ascent,cell,advance;std::vector<std::uint8_t> pixels;};
    std::map<std::pair<unsigned,std::uint16_t>,Bitmap> cache;
    std::map<std::pair<unsigned,std::uint16_t>,Bitmap> detail_cache;
    bool enhanced=false;
    unsigned bitmap_count=0,outline_count=0;
    explicit Impl(const Memory& m):memory(m){
        if(FT_Init_FreeType(&library))throw Fault(0,"FreeType initialization failed");
        FT_UInt interpreter=TT_INTERPRETER_VERSION_35;
        if(FT_Property_Set(library,"truetype","interpreter-version",&interpreter))throw Fault(0,"FreeType interpreter35 unavailable");
    }
    ~Impl(){for(auto face:faces)if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library);}
    const Bitmap& glyph(unsigned index,std::uint16_t pair){
        if(index>21)throw Fault(index,"font id outside original tables");
        const auto key=std::make_pair(index,pair);if(auto it=cache.find(key);it!=cache.end())return it->second;
        unsigned slot;
        if(index==0)slot=2;else if(index<=4||index==21)slot=1;else slot=(index-5)%4;
        auto face=faces[slot];if(!face)throw Fault(slot,"reference font not loaded");
        const auto logfont=index==21?Address(tables::worldmap_logfont):tables::logfonts+index*60;
        const auto height=memory.read(logfont),weight=memory.read(logfont+16);
        if(!height||height>64)throw Fault(logfont,"invalid original LOGFONT height");
        unsigned unicode=pair;
        if(pair>=256){if(cp949.size()!=131072)throw Fault(pair,"CP949 mapping not loaded");unicode=cp949[pair*2]|unsigned(cp949[pair*2+1])<<8;if(!unicode)throw Fault(pair,"invalid CP949 character");}
        if(FT_Set_Pixel_Sizes(face,0,height))throw Fault(index,"cannot size reference font");
        const auto gi=FT_Get_Char_Index(face,unicode);if(!gi)throw Fault(pair,"reference font lacks character");
        const bool embedded=FT_Load_Glyph(face,gi,FT_LOAD_TARGET_MONO|FT_LOAD_SBITS_ONLY)==0&&face->glyph->format==FT_GLYPH_FORMAT_BITMAP;
        if(!embedded){if(FT_Load_Glyph(face,gi,FT_LOAD_TARGET_MONO|FT_LOAD_NO_BITMAP)||FT_Render_Glyph(face->glyph,FT_RENDER_MODE_MONO))throw Fault(pair,"reference glyph cannot be rasterized");++outline_count;}else ++bitmap_count;
        const auto& b=face->glyph->bitmap;if(b.width&&b.pixel_mode!=FT_PIXEL_MODE_MONO)throw Fault(pair,"expected monochrome glyph");
        const bool bold=weight>=600; // Explicit synthetic policy; original GDI weight matching still needs capture comparison.
        int ascent=int(height);
        if(const auto os2=static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face,ft_sfnt_os2))){const auto total=unsigned(os2->usWinAscent)+os2->usWinDescent;if(total)ascent=int((unsigned(os2->usWinAscent)*height+total/2)/total);}
        Bitmap output{int(b.width)+(bold&&b.width?1:0),int(b.rows),face->glyph->bitmap_left,face->glyph->bitmap_top,ascent,int(height),int(face->glyph->advance.x>>6),{}};
        output.pixels.resize(std::size_t(output.width)*output.height);
        for(int y=0;y<output.height;++y){const auto* row=b.buffer+(b.pitch>=0?y:output.height-1-y)*std::abs(b.pitch);
            for(unsigned x=0;x<b.width;++x)if(row[x/8]&(128>>(x%8))){output.pixels[std::size_t(y)*output.width+x]=1;if(bold)output.pixels[std::size_t(y)*output.width+x+1]=1;}}
        return cache.emplace(key,std::move(output)).first->second;
    }
    const Bitmap& detail_glyph(unsigned index,std::uint16_t pair){
        const auto key=std::make_pair(index,pair);if(auto it=detail_cache.find(key);it!=detail_cache.end())return it->second;
        const auto& legacy=glyph(index,pair);const unsigned slot=index==0?2:index<=4||index==21?1:(index-5)%4;auto face=faces[slot];
        const auto logfont=index==21?Address(tables::worldmap_logfont):tables::logfonts+index*60;
        unsigned unicode=pair;if(pair>=256)unicode=cp949[pair*2]|unsigned(cp949[pair*2+1])<<8;
        constexpr unsigned s=SubpixelImage::scale;
        if(FT_Set_Pixel_Sizes(face,0,unsigned(legacy.cell)*s)||FT_Load_Glyph(face,FT_Get_Char_Index(face,unicode),FT_LOAD_NO_BITMAP|FT_LOAD_TARGET_NORMAL)||FT_Render_Glyph(face->glyph,FT_RENDER_MODE_MONO))throw Fault(pair,"high-resolution outline glyph failed");
        const auto& b=face->glyph->bitmap;const unsigned bold=memory.read(logfont+16)>=600?s:0;
        Bitmap output{int(b.width)+(b.width?int(bold):0),int(b.rows),face->glyph->bitmap_left,face->glyph->bitmap_top,legacy.ascent*int(s),legacy.cell*int(s),legacy.advance*int(s),{}};
        output.pixels.resize(std::size_t(output.width)*output.height);
        for(int y=0;y<output.height;++y){const auto row=b.buffer+(b.pitch>=0?y:output.height-1-y)*std::abs(b.pitch);for(unsigned x=0;x<b.width;++x)if(row[x/8]&(128>>(x%8)))for(unsigned dx=0;dx<=bold;++dx)output.pixels[std::size_t(y)*output.width+x+dx]=1;}
        return detail_cache.emplace(key,std::move(output)).first->second;
    }
};
FontRaster::FontRaster(const Memory& memory):impl_(std::make_unique<Impl>(memory)){}
FontRaster::~FontRaster()=default;
void FontRaster::load(std::vector<std::uint8_t> gulim,std::vector<std::uint8_t> batang,std::vector<std::uint8_t> cp949){
    if(cp949.size()!=131072)throw Fault(0,"invalid CP949 lookup size");
    for(auto& face:impl_->faces){if(face)FT_Done_Face(face);face=nullptr;}
    impl_->gulim=std::move(gulim);impl_->batang=std::move(batang);impl_->cp949=std::move(cp949);impl_->cache.clear();impl_->detail_cache.clear();
    for(unsigned i=0;i<4;++i){const auto& bytes=i<2?impl_->gulim:impl_->batang;const auto face_index=i==0||i==3?3:1;
        if(FT_New_Memory_Face(impl_->library,bytes.data(),FT_Long(bytes.size()),face_index,&impl_->faces[i])||FT_Select_Charmap(impl_->faces[i],FT_ENCODING_UNICODE))throw Fault(i,"cannot load Windows95 Che font face");}
}
void FontRaster::draw(Image8& target,const DialogGlyph& request){
    const bool prefix=request.cp949=='&'&&!request.no_prefix;
    if(prefix&&!(request.style&0x600))return; // DrawText consumes a solitary '&'.
    const auto& glyph=impl_->glyph(request.font_index,request.cp949);
    const auto clipping=request.clip.value_or(Rect{0,0,int(target.width),int(target.height)});
    bool capture=false,detail=false;
    const auto paint_bitmap=[&](const Impl::Bitmap& bitmap,int advance,int dx,int dy,std::uint8_t color){
        if(detail){
            constexpr int s=SubpixelImage::scale;const int left=(request.rect.left+advance+dx)*s+bitmap.left;
            const int top=(request.rect.top+(request.rect.bottom-request.rect.top-bitmap.cell/s)/2+dy)*s+bitmap.ascent-bitmap.top;
            for(int y=0;y<bitmap.height;++y)for(int x=0;x<bitmap.width;++x){const int px=left+x,py=top+y;
                if(px>=(request.rect.left+dx)*s&&px<(request.rect.right+dx)*s&&py>=(request.rect.top+dy)*s&&py<(request.rect.bottom+dy)*s&&px>=0&&py>=0&&px<int(target.width)*s&&py<int(target.height)*s&&px>=clipping.left*s&&px<clipping.right*s&&py>=clipping.top*s&&py<clipping.bottom*s&&bitmap.pixels[std::size_t(y)*bitmap.width+x])target.set_detail_pixel(unsigned(px),unsigned(py),color);
            }return;
        }
        const int left=request.rect.left+advance+bitmap.left+dx;
        const int top=request.rect.top+(request.rect.bottom-request.rect.top-bitmap.cell)/2+bitmap.ascent-bitmap.top+dy;
        for(int y=0;y<bitmap.height;++y)for(int x=0;x<bitmap.width;++x){const int px=left+x,py=top+y;
            if(px>=request.rect.left+dx&&px<request.rect.right+dx&&py>=request.rect.top+dy&&py<request.rect.bottom+dy&&px>=0&&py>=0&&px<int(target.width)&&py<int(target.height)&&px>=clipping.left&&px<clipping.right&&py>=clipping.top&&py<clipping.bottom&&bitmap.pixels[std::size_t(y)*bitmap.width+x]){
                const auto at=std::size_t(py)*target.width+px;if(capture){if(!target.detail_cell(at)){SubpixelImage::Tile tile;tile.fill(target.pixels[at]);target.set_detail_cell(at,tile);}}else target.pixels[at]=color;
            }}
    };
    const auto selected=[&](std::uint16_t cp)->const Impl::Bitmap&{return detail?impl_->detail_glyph(request.font_index,cp):impl_->glyph(request.font_index,cp);};
    const auto paint=[&](int dx,int dy,std::uint8_t color){if(!prefix)paint_bitmap(detail?selected(request.cp949):glyph,0,dx,dy,color);};
    const auto underbar=[&](int dx,int dy,std::uint8_t color){if(request.style&0x200){const auto& marker=selected('_');for(unsigned i=0;i<(request.cp949>=256?2u:1u);++i)paint_bitmap(marker,int(i)*impl_->glyph(request.font_index,'_').advance,dx,dy+3,color);}};
    const auto accent=[&](int dx,int dy,std::uint8_t color){if((request.style&0x400)&&request.cp949!=' ')paint_bitmap(selected(0xa1a4),0,dx+(request.cp949<256?-4:0),dy-9,color);};
    auto mode=request.style&0xf0;
    if(((request.flags_b&32)&&(request.style&32))||(request.flags_b&64))mode=32;
    const bool large=(request.style&0x3000)!=0;
    const auto passes=[&](){
    if(mode==0||mode==32){for(int y=large?-2:-1;y<=int(request.radius);++y)for(int x=large?-2:-1;x<=int(request.radius);++x){
        const bool on=mode==0?impl_->memory.read(tables::glyph_outline_mask+std::uint32_t((y+x*5+(large?25:0))*4))!=0:(x||y);
        if(on){paint(x,y,request.outline);underbar(x,y,request.outline);accent(x,y,request.outline);}}}
    paint(0,0,request.foreground);
    //40dd27 draws the foreground underbar twice, atx-2 andx, and uses
    //the original CP949 middle-dot glyph at57fbd0 for accent marks.
    underbar(-2,0,request.foreground);underbar(0,0,request.foreground);accent(0,0,request.foreground);
    };
    if(impl_->enhanced){capture=true;passes();capture=false;detail=true;passes();detail=false;}
    passes();
}
void FontRaster::set_enhanced(bool enabled){impl_->enhanced=enabled;}
unsigned FontRaster::bitmap_glyphs()const{return impl_->bitmap_count;}
int FontRaster::advance(unsigned font,std::uint16_t cp949){return impl_->glyph(font,cp949).advance;}
unsigned FontRaster::outline_glyphs()const{return impl_->outline_count;}
} // namespace fsb::core
