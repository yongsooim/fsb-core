#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
unsigned DialogGraphics::plain_text(Address surface,Rect box,unsigned font,std::uint8_t color,const std::string& text,unsigned flags,std::optional<Rect> clipping,std::optional<std::uint8_t> background){
    if(!glyph_renderer||!glyph_advance)throw Fault(surface,"text requires font adapter");
    if(flags&~0x927u)throw Fault(flags,"unsupported menu DrawText flags");
    const unsigned height=memory_.read(font==21?Address(tables::worldmap_logfont):tables::logfonts+font*60);
    if(text.empty())return 0;
    struct Character {std::uint16_t code;bool underline;};
    std::vector<std::vector<Character>> lines(1);std::vector<int> widths(1,0);bool underline=false;
    for(std::size_t i=0;i<text.size();){unsigned cp=std::uint8_t(text[i++]);if(cp&128){if(i==text.size())throw Fault(surface,"truncated menu CP949 string");cp=(cp<<8)|std::uint8_t(text[i++]);}
        if(cp=='\r'||cp=='\n'){if(cp=='\r'&&i<text.size()&&text[i]=='\n')++i;if(flags&32)cp=' ';else{lines.emplace_back();widths.push_back(0);continue;}}
        if(cp=='&'&&!(flags&0x800)){if(i<text.size()&&text[i]=='&')++i;else{underline=true;continue;}}
        lines.back().push_back({std::uint16_t(cp),underline});underline=false;widths.back()+=glyph_advance(font,std::uint16_t(cp));
    }
    const auto fill=[&](Rect rect,std::uint8_t value){if(clipping){rect.left=std::max(rect.left,clipping->left);rect.top=std::max(rect.top,clipping->top);rect.right=std::min(rect.right,clipping->right);rect.bottom=std::min(rect.bottom,clipping->bottom);}surfaces_.clear(surface,rect,value);};
    int y=box.top;if(flags&4)y+=(box.bottom-box.top-int(height*lines.size()))/2;
    for(unsigned row=0;row<lines.size();++row){int x=box.left;if(flags&1)x+=(box.right-box.left-widths[row])/2;else if(flags&2)x=box.right-widths[row];
        const int right=(flags&0x100)?int(surfaces_.get(surface).width):box.right; // DT_NOCLIP on the map-name OSD.
        if(background)fill({x,y,x+widths[row],y+int(height)},*background);
        for(auto character:lines[row]){const auto cp=character.code;const int advance=glyph_advance(font,cp);glyph_renderer(surfaces_.get(surface),{cp,font,{x,y,right,y+int(height)},0x10,0,color,0,0,true,clipping});if(character.underline)fill({x,y+int(height)-2,x+advance,y+int(height)-1},color);x+=advance;}y+=int(height);
    }
    return height*unsigned(lines.size());
}
void DialogGraphics::menu_text(Address surface,int x,int y,unsigned font,std::uint32_t foreground,std::uint32_t outline,const std::string& text){
    if(!glyph_renderer||!glyph_advance)throw Fault(surface,"menu text needs the portable font adapter");
    // 4397db: whole-string DrawTextA in a64px centered band; 5x5 outline
    // mask, followed by foreground. Colors here are original PALETTEINDEXs.
    if((foreground&0xffffff00u)!=0x01000000u||(outline&0xffffff00u)!=0x01000000u)throw Fault(font,"menu text RGB palette mapping is not connected");
    const int right=x+int(text.size())*(font>12?32:16);
    const auto pass=[&](int dx,int dy,std::uint8_t color){
        int pen=x+dx;
        for(std::size_t i=0;i<text.size();){unsigned cp=std::uint8_t(text[i++]);if(cp&128){if(i==text.size())throw Fault(surface,"truncated CP949 menu text");cp=(cp<<8)|std::uint8_t(text[i++]);}
            glyph_renderer(surfaces_.get(surface),{std::uint16_t(cp),font,{pen,y-32+dy,right+dx,y+32+dy},0x10,0,color,color,0});pen+=glyph_advance(font,std::uint16_t(cp));
        }
    };
    for(int dy=-2;dy<3;++dy)for(int dx=-2;dx<3;++dx)if(memory_.read(0x4a26e0+std::uint32_t((dx*5+dy+(font>12?25:0))*4)))pass(dx,dy,std::uint8_t(outline));
    pass(0,0,std::uint8_t(foreground));
}
} // namespace fsb::core
