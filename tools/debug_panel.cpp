#include "debug_panel.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>

namespace fsb::host {
namespace {
std::vector<unsigned> codepoints(const std::string& s){
    std::vector<unsigned> out;
    for(std::size_t i=0;i<s.size();){unsigned c=std::uint8_t(s[i++]);if(c<128){out.push_back(c);continue;}const auto n=c<0xe0?1u:c<0xf0?2u:3u;c&=n==1?31:n==2?15:7;for(unsigned j=0;j<n;++j){if(i==s.size()||(std::uint8_t(s[i])&0xc0)!=0x80){c=0xfffd;break;}c=(c<<6)|(std::uint8_t(s[i++])&63);}out.push_back(c);}return out;
}
using Color=std::array<std::uint8_t,3>;
constexpr Color background{19,26,36},muted{147,165,184},foreground{236,242,248},accent{94,218,183};
}
struct DebugPanel::Impl {
    FT_Library library=nullptr;FT_Face face=nullptr;std::vector<std::uint8_t> font;
    struct Glyph{int w,h,left,top,advance;std::vector<std::uint8_t> alpha;};
    std::map<unsigned,Glyph> glyphs;
    bool visible=false,frozen=false,dirty=true,have_snapshot=false;unsigned page=0,captured_buttons=0,font_size=0;
    int scroll=0,max_scroll=0;std::uint64_t updated=0,revision=0;double dpi=1;core::ImageRgba pixels;core::DebugSnapshot state;
    std::optional<SDL_FingerID> captured_finger;
    bool reward_boost=false;std::optional<bool> reward_request;
    std::filesystem::path directory;std::string notice;unsigned export_count=0;
    Impl(std::vector<std::uint8_t> bytes,std::filesystem::path path):font(std::move(bytes)),directory(std::move(path)){
        if(FT_Init_FreeType(&library))throw std::runtime_error("debug font initialization failed");
        if(FT_New_Memory_Face(library,font.data(),FT_Long(font.size()),0,&face)||FT_Select_Charmap(face,FT_ENCODING_UNICODE)){if(face)FT_Done_Face(face);FT_Done_FreeType(library);throw std::runtime_error("debug font could not be loaded");}
    }
    ~Impl(){if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library);}
    const Glyph& glyph(unsigned cp){
        if(const auto i=glyphs.find(cp);i!=glyphs.end())return i->second;
        if(FT_Load_Char(face,cp,FT_LOAD_RENDER|FT_LOAD_NO_BITMAP))throw std::runtime_error("debug glyph could not be rendered");
        const auto& b=face->glyph->bitmap;Glyph g{int(b.width),int(b.rows),face->glyph->bitmap_left,face->glyph->bitmap_top,int(face->glyph->advance.x>>6),{}};
        g.alpha.resize(std::size_t(g.w)*g.h);for(int y=0;y<g.h;++y){const auto* row=b.buffer+(b.pitch>=0?y:g.h-1-y)*std::abs(b.pitch);for(int x=0;x<g.w;++x)g.alpha[std::size_t(y)*g.w+x]=b.pixel_mode==FT_PIXEL_MODE_MONO?((row[x/8]&(128>>(x%8)))?255:0):row[x];}
        return glyphs.emplace(cp,std::move(g)).first->second;
    }
    int px(double n)const{return int(std::lround(n*dpi));}
    void blend(int x,int y,Color c,unsigned a=255){if(x<0||y<0||x>=int(pixels.width)||y>=int(pixels.height))return;const auto i=(std::size_t(y)*pixels.width+x)*4;for(unsigned k=0;k<3;++k)pixels.pixels[i+k]=std::uint8_t((c[k]*a+pixels.pixels[i+k]*(255-a)+127)/255);pixels.pixels[i+3]=255;}
    void rect(int x,int y,int w,int h,Color c){for(int yy=std::max(0,y);yy<std::min(int(pixels.height),y+h);++yy)for(int xx=std::max(0,x);xx<std::min(int(pixels.width),x+w);++xx)blend(xx,yy,c);}
    int text(const std::string& s,int left,int baseline,Color color,int right,int top=0,int bottom=0,bool wrap=false){
        if(!bottom)bottom=int(pixels.height);int x=left,y=baseline;unsigned lines=1;
        for(auto cp:codepoints(s)){if(cp<32)cp=' ';const auto& g=glyph(cp);if(x+g.advance>right){if(!wrap||lines>=6)break;x=left;y+=px(20);++lines;}
            for(int yy=0;yy<g.h;++yy){const auto py=y-g.top+yy;if(py<top||py>=bottom)continue;for(int xx=0;xx<g.w;++xx)if(x+g.left+xx<right)blend(x+g.left+xx,py,color,g.alpha[std::size_t(yy)*g.w+xx]);}x+=g.advance;
        }return y;
    }
    void action(unsigned id){
        try{
            if(id==0){frozen=!frozen;notice=frozen?"표시만 고정 · 게임은 계속 진행":"실시간 표시로 복귀";}
            else if(id==1){if(!SDL_SetClipboardText(core::debug_json(state).c_str()))throw std::runtime_error(SDL_GetError());notice="전체 상태 JSON을 복사했습니다";}
            else{
                std::filesystem::create_directories(directory);const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const auto path=directory/("snapshot-"+std::to_string(ms)+"-"+std::to_string(export_count++)+".json");std::ofstream file(path,std::ios::binary);file<<core::debug_json(state);file.close();if(!file)throw std::runtime_error("snapshot write failed");notice="저장됨: "+path.filename().string();SDL_Log("Debug snapshot: %s",path.string().c_str());
            }
        }catch(const std::exception& e){notice="실패: "+std::string(e.what());}dirty=true;
    }
};
DebugPanel::DebugPanel(std::vector<std::uint8_t> font,std::filesystem::path directory):impl_(std::make_unique<Impl>(std::move(font),std::move(directory))){}
DebugPanel::~DebugPanel()=default;
bool DebugPanel::visible()const{return impl_->visible;}
void DebugPanel::show(bool visible){impl_->visible=visible;impl_->dirty=true;}
int DebugPanel::width_points(int total)const{return visible()?std::min(440,total/2):0;}
void DebugPanel::reward_boost(bool value){auto& p=*impl_;if(p.reward_boost!=value){p.reward_boost=value;p.dirty=true;}}
std::optional<bool> DebugPanel::take_reward_boost_request(){const auto request=impl_->reward_request;impl_->reward_request.reset();return request;}
bool DebugPanel::handle(const SDL_Event& e,int width,int height){
    auto& p=*impl_;
    if((e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_KEY_UP)&&e.key.key==SDLK_TAB){if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat)show(!visible());return true;}
    const auto panel=width_points(width),left=width-panel;
    if(e.type==SDL_EVENT_MOUSE_BUTTON_UP){const auto bit=SDL_BUTTON_MASK(e.button.button);if(p.captured_buttons&bit){p.captured_buttons&=~bit;return true;}return false;}
    if(e.type==SDL_EVENT_FINGER_UP&&p.captured_finger&&*p.captured_finger==e.tfinger.fingerID){p.captured_finger.reset();return true;}
    if(!visible())return false;
    // SDL3 reports wheel motion as a float; integer_y is the same whole-tick
    // count SDL2 delivered, so one notch keeps scrolling by one step.
    if(e.type==SDL_EVENT_MOUSE_WHEEL){float x=0;SDL_GetMouseState(&x,nullptr);if(x<float(left))return false;const auto delta=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e.wheel.integer_y:e.wheel.integer_y;p.scroll=std::clamp(p.scroll-delta*64,0,p.max_scroll);p.dirty=true;return true;}
    if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.x>=float(left)){
        p.captured_buttons|=SDL_BUTTON_MASK(e.button.button);if(e.button.button!=SDL_BUTTON_LEFT)return true;
        const auto x=int(e.button.x)-left,y=int(e.button.y);
        if(y>=48&&y<82&&x>=12&&x<panel-12){p.page=std::min(3u,unsigned((x-12)*4/(panel-24)));p.scroll=0;p.notice.clear();p.dirty=true;}
        if(y>=90&&y<122&&x>=12&&x<panel-12)p.action(std::min(2u,unsigned((x-12)*3/(panel-24))));
        if(y>=128&&y<164&&x>=12&&x<panel-12){p.reward_request=!p.reward_boost;p.notice="이후 보상에 적용";p.dirty=true;}
        return true;
    }
    // Debug touches never become game confirms, including development toggles.
    if(e.type==SDL_EVENT_FINGER_DOWN&&e.tfinger.x*width>=left){p.captured_finger=e.tfinger.fingerID;const auto x=e.tfinger.x*width-left,y=e.tfinger.y*height;if(y>=128&&y<164&&x>=12&&x<panel-12){p.reward_request=!p.reward_boost;p.notice="이후 보상에 적용";p.dirty=true;}return true;}
    if(e.type==SDL_EVENT_FINGER_MOTION)return p.captured_finger&&*p.captured_finger==e.tfinger.fingerID;
    return false;
}
bool DebugPanel::wants_snapshot(std::uint64_t now)const{const auto& p=*impl_;return p.visible&&(!p.have_snapshot||(!p.frozen&&now-p.updated>=100));}
void DebugPanel::update(core::DebugSnapshot state,std::uint64_t now){auto& p=*impl_;if(p.frozen&&p.have_snapshot)return;p.state=std::move(state);p.have_snapshot=true;p.updated=now;p.dirty=true;}
const core::DebugSnapshot& DebugPanel::snapshot()const{return impl_->state;}
std::uint64_t DebugPanel::revision()const{return impl_->revision;}
const core::ImageRgba& DebugPanel::image(unsigned width,unsigned height,double dpi){
    auto& p=*impl_;dpi=std::max(0.5,dpi);if(width!=p.pixels.width||height!=p.pixels.height||dpi!=p.dpi){p.dirty=true;p.dpi=dpi;p.pixels={width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};}
    if(!p.dirty)return p.pixels;p.dirty=false;++p.revision;const auto size=unsigned(p.px(14));if(p.font_size!=size){p.font_size=size;p.glyphs.clear();if(FT_Set_Pixel_Sizes(p.face,0,size))throw std::runtime_error("debug font size rejected");}
    p.rect(0,0,int(width),int(height),background);const auto right=int(width)-p.px(16);p.text("FSB DEBUG",p.px(16),p.px(29),accent,right);p.text(p.frozen?"고정됨":"실시간 · 10 Hz",p.px(160),p.px(29),muted,right);
    const char* tabs[]={"상태","파티","이벤트","진단"};const auto tab_width=(int(width)-p.px(24))/4;
    for(unsigned i=0;i<4;++i){const auto x=p.px(12)+int(i)*tab_width;p.rect(x,p.px(48),tab_width-p.px(3),p.px(34),i==p.page?Color{43,76,83}:Color{31,42,57});p.text(tabs[i],x+p.px(12),p.px(70),i==p.page?accent:foreground,x+tab_width-p.px(4));}
    const char* actions[]={p.frozen?"고정 해제":"표시 고정","JSON 복사","파일 저장"};const auto action_width=(int(width)-p.px(24))/3;
    for(unsigned i=0;i<3;++i){const auto x=p.px(12)+int(i)*action_width;p.rect(x,p.px(90),action_width-p.px(3),p.px(32),Color{37,49,66});p.text(actions[i],x+p.px(10),p.px(111),foreground,x+action_width-p.px(4));}
    p.rect(p.px(12),p.px(128),int(width)-p.px(24),p.px(36),p.reward_boost?Color{30,68,64}:Color{37,49,66});
    p.text("경험치 · 골드 10배",p.px(22),p.px(152),foreground,right-p.px(60));p.text(p.reward_boost?"ON":"OFF",int(width)-p.px(66),p.px(152),p.reward_boost?accent:muted,right);
    const auto top=p.px(180),bottom=int(height)-p.px(68);int y=top-p.px(p.scroll);
    if(p.page<p.state.sections.size())for(const auto& field:p.state.sections[p.page].fields){p.text(field.label,p.px(16),y+p.px(14),muted,right,top,bottom);y=p.text(field.value,p.px(16),y+p.px(36),foreground,right,top,bottom,true)+p.px(18);}
    p.max_scroll=std::max(0,int(std::ceil((y-top)/dpi))+p.scroll-int(std::floor((bottom-top)/dpi)));p.scroll=std::min(p.scroll,p.max_scroll);
    if(p.max_scroll){const auto track=bottom-top;const auto thumb=std::max(p.px(20),int(double(track)*track/(track+p.px(p.max_scroll))));const auto offset=int(double(track-thumb)*p.scroll/p.max_scroll);p.rect(int(width)-p.px(5),top+offset,p.px(3),thumb,muted);}
    p.rect(0,bottom,int(width),int(height)-bottom,background);p.text("표본 "+std::to_string(p.state.frame_ms)+" ms · Tab 닫기 / 휠 스크롤",p.px(16),bottom+p.px(24),muted,right);
    p.text(p.notice.empty()?"상태 보기 · 개발용 보상 옵션":p.notice,p.px(16),bottom+p.px(47),accent,right);return p.pixels;
}
} // namespace fsb::host
