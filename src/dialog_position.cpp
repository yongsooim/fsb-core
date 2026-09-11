#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <vector>

namespace fsb::core {
namespace {
int edge(int value,int low,int high){if(value<=low)return low;if(value>=high)return high;return value;}
bool intersects(Rect a,Rect b){return std::max(a.left,b.left)<std::min(a.right,b.right)&&std::max(a.top,b.top)<std::min(a.bottom,b.bottom);}
bool tail_valid(int pos,Rect box,int x,int y){
    if(pos==4){if(x!=box.right)return false;}
    else if(pos==6){if(x!=box.left-16)return false;}
    else if((pos>=1&&pos<=3)||(pos>=7&&pos<=9)){if(x<box.left+16||x>box.right-32)return false;}
    if(pos>=1&&pos<=3)return y==box.top-16;
    if(pos==4||pos==6)return y>=box.top+16&&y<=box.bottom-32;
    if(pos>=7&&pos<=9)return y==box.bottom;
    return true;
}
}
void DialogGraphics::position(Address ctrl,Address d){
    const auto r=[&](unsigned word){return signed32(memory_.read(d+word*4));};
    const auto w=[&](unsigned word,int value){memory_.write(d+word*4,std::uint32_t(value));};
    const auto rect=[&](unsigned word){return Rect{r(word),r(word+1),r(word+2),r(word+3)};};
    const auto store=[&](unsigned word,Rect q){w(word,q.left);w(word+1,q.top);w(word+2,q.right);w(word+3,q.bottom);};
    restore_tail(d);
    const Rect viewport{signed32(memory_.read(globals::viewport_left))+4,signed32(memory_.read(globals::viewport_top))+4,signed32(memory_.read(globals::viewport_right))-4,signed32(memory_.read(globals::viewport_bottom))-4};
    const int view_width=signed32(memory_.read(globals::viewport_width)),view_height=signed32(memory_.read(globals::viewport_height));
    const int cx=signed32(memory_.read(globals::viewport_center_x)),cy=signed32(memory_.read(globals::viewport_center_y));
    const int width=r(dialog_word::width),height=r(dialog_word::height);
    memory_.write(globals::dialogue_view_left,std::uint32_t(viewport.left));memory_.write(globals::dialogue_view_top,std::uint32_t(viewport.top));memory_.write(globals::dialogue_view_right,std::uint32_t(viewport.right));memory_.write(globals::dialogue_view_bottom,std::uint32_t(viewport.bottom));
    int ax=r(dialog_word::anchor_x),ay=r(dialog_word::anchor_y),pos=r(dialog_word::position);
    if(r(dialog_word::anchor_x_pointer)||r(dialog_word::anchor_y_pointer)){ax=signed32(memory_.read(std::uint32_t(r(dialog_word::anchor_x_pointer))));ay=signed32(memory_.read(std::uint32_t(r(dialog_word::anchor_y_pointer))));}
    else{
        const int divisor=(r(dialog_word::flags_b)&4)?4:6;
        if(pos==0||pos==2||pos==5||pos==8)ax=cx;
        else if(pos==1||pos==4||pos==7)ax=view_width/divisor+signed32(memory_.read(globals::viewport_origin_x));
        else if(pos==3||pos==6||pos==9)ax=signed32(memory_.read(globals::viewport_far_x))-view_width/divisor;
        if(pos>=0&&pos<=9){if(pos==0||(pos>=4&&pos<=6))ay=cy;else if(pos<4)ay=signed32(memory_.read(globals::viewport_far_y))-view_height/divisor;else ay=view_height/divisor+signed32(memory_.read(globals::viewport_origin_y));}
    }
    ax+=signed32(memory_.read(ctrl+0x180));ay+=signed32(memory_.read(ctrl+0x184));w(dialog_word::anchor_x,ax);w(dialog_word::anchor_y,ay);
    const int offsets[]={ax-32,ay-44,ax-32,ay-44};
    for(unsigned i=0;i<4;++i)w(0x27+i,signed32(memory_.read(tables::dialogue_actor_exclusion_rect+i*4))+offsets[i]);
    w(dialog_word::tail_frame,0);w(0x2b,pos);
    const bool unclamped=(r(dialog_word::flags_b)&256)!=0;
    const auto clamp=[&](Rect q){if(!unclamped){q.left=edge(q.left,viewport.left,viewport.right-width);q.top=edge(q.top,viewport.top,viewport.bottom-height);}q.right=q.left+width;q.bottom=q.top+height;return q;};
    Rect candidate=rect(dialog_word::target_rect);
    if(!r(dialog_word::anchor_x_pointer)){
        if(r(dialog_word::anchor_y_pointer))throw Fault(d,"dialog anchor pointers must be paired");
        candidate=clamp({ax-width/2,ay-height/2,0,0});
    }else{
        if(!r(dialog_word::anchor_y_pointer))throw Fault(d,"dialog anchor pointers must be paired");
        const auto above_left=[&](){const int vw=viewport.right-viewport.left;if(vw<=0)throw Fault(d,"empty dialog viewport");return ((width*2/10+vw)*(ax-viewport.left))/vw-width*6/10+viewport.left;};
        const auto try_pos=[&](int key){
            int x=key,y=key;
            if(key==4)x=r(0x27)-r(0x42)-width;else if(key==6)x=r(0x29)+r(0x44);else if((key>=1&&key<=3)||(key>=7&&key<=9))x=above_left();
            if(key>=1&&key<=3)y=r(0x35+key)+r(0x2a);else if(key==4||key==6)y=ay-height/2;else if(key>=7&&key<=9)y=r(0x28)-r(0x35+key)-height;
            x=edge(x,viewport.left,viewport.right-width);y=edge(y,viewport.top,viewport.bottom-height);const Rect box{x,y,x+width,y+height};
            for(unsigned i=0;i<memory_.read(globals::occupied_dialogue_count);++i){const auto a=globals::occupied_dialogue_rects+i*16;const Rect occupied{signed32(memory_.read(a)),signed32(memory_.read(a+4)),signed32(memory_.read(a+8)),signed32(memory_.read(a+12))};if(intersects(box,occupied))return false;}
            return true;
        };
        const bool vertical_safe=ay>=viewport.top+20&&ay<=viewport.bottom-24;
        const bool can_above=r(0x3d)+height<=r(0x28)-viewport.top;
        const bool can_below=r(0x37)+height<=viewport.bottom-r(0x2a);
        const bool can_left=vertical_safe&&r(0x27)-viewport.left>=width+r(0x42);
        const bool can_right=vertical_safe&&width+r(0x44)<=viewport.right-r(0x29);
        const int ht=ax>=cx?6:4,vt=ay>=cy?2:8;
        const auto probe=[&](int key){return(key==8?can_above:key==2?can_below:key==4?can_left:can_right)&&try_pos(key);};
        if(!pos){
            std::vector<int> order;int fallback=0;
            switch(r(dialog_word::position_order)){
            case 0x21c2:case 0x360:order={8,6,4};fallback=8;break;
            case 0x09a4:order={2,4,6};fallback=2;break;
            case 0x12da:order={4,8,2};fallback=4;break;
            case 0x188c:case 0x274:order={6,2,8};fallback=6;break;
            case 0x1ce:order={4,6,2};fallback=4;break;case 0x34a:order={8,4,2};fallback=8;break;
            case 0x339:order={8,2,5};fallback=8;break;case 0x102:order={2,5,8};fallback=2;break;
            case 0x246:order={5,8,2};fallback=ht;break;case 0x11d:order={2,8,5};fallback=2;break;
            case 0x354:order={8,5,2};fallback=8;break;case 0x210:order={5,2,8};fallback=ht;break;
            case 0x32:order={5,0};fallback=ht;break;case 5:order={0,5};fallback=vt;break;
            default:throw Fault(d,"unknown original PosSub order");
            }
            for(auto group:order){if(group==5||group==0){const int a=group==5?4:8,b=group==5?6:2;const bool va=probe(a),vb=probe(b);if(va||vb)pos=va&&vb?(group==5?ht:vt):va?a:b;}
                else if(probe(group))pos=group;if(pos)break;}
            if(!pos)pos=fallback;
            if(pos==2||pos==8){if(ax<=cx-32)--pos;else if(ax>=cx+32||(r(dialog_word::flags_b)&8))pos+=(ax<cx||(pos==8&&ax==cx))?-1:1;}
        }
        const int marker_left=ax-40,marker_center=ax-8,marker_right=ax+24;
        if((pos>=1&&pos<=3)||(pos>=7&&pos<=9)){
            candidate.left=above_left();if(!unclamped)candidate.left=edge(candidate.left,viewport.left,viewport.right-width);
            if(width<80){if(pos==1||pos==3)pos=2;else if(pos==7||pos==9)pos=8;
                candidate.left=ax<cx?std::min(candidate.left,ax-24):std::max(candidate.left,ax-width+24);
            }else{
                bool center_check=pos==2||pos==8;
                if((pos==1||pos==7)&&marker_left<candidate.left+16){++pos;center_check=true;}
                else if((pos==3||pos==9)&&candidate.left+width-32<marker_right){--pos;center_check=true;}
                if(center_check){if(marker_center<candidate.left+16)++pos;else if(candidate.left+width-32<marker_center)--pos;}
            }
        }
        int mx=r(dialog_word::tail_x),my=r(dialog_word::tail_y);
        if(pos==1||pos==7)mx=marker_left;else if(pos==2||pos==8)mx=marker_center;else if(pos==3||pos==9)mx=marker_right;
        else if(pos==4){mx=r(0x27)-r(0x42);candidate.left=mx-width;}
        else if(pos==6){mx=r(0x29)-16+r(0x44);candidate.left=mx+16;}
        else if(pos==5)candidate.left=ax-width/2;
        if(pos>=1&&pos<=3){my=r(0x35+pos)-16+r(0x2a);candidate.top=my+16;}
        else if(pos>=4&&pos<=6){if(pos!=5)my=ay-8;candidate.top=ay-height/2;}
        else if(pos>=7&&pos<=9){my=r(0x28)-r(0x35+pos);candidate.top=my-height;}
        else throw Fault(d,"invalid dialog keypad position");
        candidate=clamp(candidate);w(dialog_word::tail_x,mx);w(dialog_word::tail_y,my);w(0x2b,pos);
        if(pos!=5&&(r(dialog_word::flags_b)&2)){
            if(tail_valid(pos,candidate,mx,my))w(dialog_word::tail_frame,signed32(memory_.read(tables::dialogue_tail_frames+pos*4)));
            else if(!(ax>=viewport.left&&ax<viewport.right&&ay>=viewport.top&&ay<viewport.bottom)){
                candidate.top=edge(ay-height/2,viewport.top,viewport.bottom-height);candidate.bottom=candidate.top+height;
                int fallback;
                if(ay-8>=viewport.top+16&&ay-8<=viewport.bottom-32)fallback=ax>=cx?4:6;
                else if(ax-8>=viewport.left+16&&ax-8<=viewport.right-32)fallback=ay>=cy?8:2;
                else fallback=(ay>=cy?0:-6)+(ax>=cx?0:2)+7;
                w(dialog_word::tail_frame,signed32(memory_.read(tables::dialogue_tail_frames+fallback*4)));
                mx=(fallback==1||fallback==7)?marker_left:(fallback==2||fallback==8)?marker_center:(fallback==3||fallback==9)?marker_right:fallback==4?candidate.right:candidate.left-16;
                my=fallback<4?candidate.top-16:(fallback==4||fallback==6)?ay-8:candidate.bottom;w(dialog_word::tail_x,mx);w(dialog_word::tail_y,my);
            }
        }
    }
    store(dialog_word::target_rect,candidate);
    Rect applied=rect(dialog_word::current_rect);
    if((r(dialog_word::flags_a)&1)||(r(dialog_word::flags_c)&3)){
        int sx=int(memory_.read(globals::frame_delta_ms)>>1),sy=sx;const auto marker=r(dialog_word::tail_frame);
        if((marker>=26&&marker<=28)||(marker>=31&&marker<=33)){if(candidate.top!=applied.top)sx/=3;}
        else if(marker==29||marker==30){if(candidate.left!=applied.left)sy/=3;}
        const auto step=[](int target,int current,int n){return target<current?std::max(target,current-n):std::min(target,current+n);};
        applied.left=step(candidate.left,applied.left,sx);applied.top=step(candidate.top,applied.top,sy);applied.right=applied.left+width;applied.bottom=applied.top+height;store(dialog_word::current_rect,applied);
    }
    const int frame=r(dialog_word::tail_frame);if(frame>=26&&frame<=33){constexpr int positions[]={7,8,9,4,6,1,2,3};if(!tail_valid(positions[frame-26],applied,r(dialog_word::tail_x),r(dialog_word::tail_y)))w(dialog_word::tail_frame,0);}
}
} // namespace fsb::core
