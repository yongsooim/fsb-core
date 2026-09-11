#include "fsb_core/battle.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
void Battle::tick_game_over(){
    auto& m=runtime_.memory;auto state=m.read(0x804a70);
    if(!state){runtime_.audio.fade_volume(30,0);m.write(0x804a70,1);state=1;}
    if(state==1){
        if(!recovered.invoke(0x4321ab,{1,1,500,0x804660})||runtime_.audio.global_fade_busy())return;
        const auto surface=runtime_.sprites.decode_surface(0x5d24d0,0x804258);const auto& image=runtime_.surfaces.get(surface);m.write(0x804a74,surface);m.write(0x804250,image.width);m.write(0x804254,image.height);
        runtime_.palette.begin(0x804258,0,0,30);runtime_.audio.load_bgm(46);m.write(0x76e91c,0);runtime_.audio.play_bgm();runtime_.audio.fade_volume(30,100);m.write(0x804a70,2);state=2;
    }
    if(state==2){
        //460a6c: an idle palette fade plus an unblocked key-down hands the
        // screen to the post-defeat title flow. The splash still draws this frame.
        if(!runtime_.palette.busy()&&m.read(globals::input_message)==0x100&&!(m.read(globals::input_flags)&0x40000000))m.write(0x804a64,9);
        const auto target=m.read(globals::render_target_surface);runtime_.surfaces.clear(target,{0,0,int(m.read(globals::framebuffer_width)),int(m.read(globals::framebuffer_height))});
        const int width=int(m.read(0x804250)),height=int(m.read(0x804254));runtime_.surfaces.blit(target,(int(m.read(globals::framebuffer_width))-width)/2,(int(m.read(globals::framebuffer_height))-height)/2,m.read(0x804a74),{0,0,width,height});
    }
}
void Battle::initialize_ui(){
    auto& m=runtime_.memory;
    for(unsigned i=0;i<21;++i)m.write(0x6db998+i*4,0x24000000+i);
    if(!m.read(0x7735d4))m.write(0x7735d4,runtime_.sprites.decode_surface(0x5b3170,0x771f90));
    if(!m.read(0x7735d8))m.write(0x7735d8,runtime_.sprites.decode_surface(0x5b3164));
    recovered.invoke(0x464102);
}
void Battle::open_menu_assets(bool field){
    auto& m=runtime_.memory;auto& surfaces=runtime_.surfaces;
    if(field){
        if(m.read(0x7735d0))surfaces.release(m.read(0x7735d0));const auto width=m.read(globals::framebuffer_width),height=m.read(globals::framebuffer_height),surface=surfaces.create(width,height);m.write(0x7735d0,surface);
        surfaces.blit(surface,0,0,m.read(globals::render_target_surface),{0,0,int(width),int(height)});auto& image=surfaces.get(surface);
        for(unsigned y=0;y<height;++y)for(unsigned x=y&1;x<width;x+=2)image.set_pixel(std::size_t(y)*width+x,0);
    }
    // The shared icon sheets are loaded independently of the capture path.
    if(!m.read(0x7735d4))m.write(0x7735d4,runtime_.sprites.decode_surface(0x5b3170,0x771f90));
    if(!m.read(0x7735d8))m.write(0x7735d8,runtime_.sprites.decode_surface(0x5b3164));
    const auto object=[&](Address owner,Address callback){const auto h=runtime_.arena.allocate_after(m.read(owner),callback,0x30000,0);return *resolve_compact(m,h);};
    if(field&&!m.read(0x7735dc))m.write(0x7735dc,object(0x6d5398,0x4471c3));
    if(!m.read(0x7735e0))m.write(0x7735e0,object(0x6d5a38,0x4473b4));
    runtime_.palette.capture(0x772a18,1,2);runtime_.palette.capture(0x772a20,224,24);runtime_.palette.upload(0x5b15f0,1,2);runtime_.palette.upload(0x772310,224,24);
    m.write(0x5b15e8,0xffffffffu);m.write(0x772fb0,0);m.write(0x772a10,m.read(globals::active_party_index));m.write(0x7723ec,1);
}
void Battle::close_menu_assets(){
    auto& m=runtime_.memory;if(m.read(0x7735d0)){runtime_.surfaces.release(m.read(0x7735d0));m.write(0x7735d0,0);}
    for(auto slot:{0x7735dcu,0x7735e0u})if(const auto object=m.read(slot)){runtime_.arena.release(compact_handle(m,object));m.write(slot,0);}
    m.write(0x5b15e8,0xffffffffu);m.write(0x773634,0);runtime_.palette.upload(0x772a18,1,2);runtime_.palette.upload(0x772a20,224,24);
}
void Battle::tick_shop(){
    auto& m=runtime_.memory;
    if(m.read(globals::game_mode)==5){
        if(!m.read(0x804cc8)){
            const auto width=m.read(globals::framebuffer_width),height=m.read(globals::framebuffer_height);m.write(0x804cc8,runtime_.surfaces.create(width,height));
            m.write(0x804cbc,width);m.write(0x804cc0,height);m.write(0x804ca8,1,2);m.write(0x804caa,1,2);m.write(0x804cb4,width,2);m.write(0x804cb6,height,2);m.write(0x804cb8,1,2);
        }
        if(!m.read(0x80384c))initialize_ui();
        m.write(globals::game_mode,recovered.invoke(0x45dcc8));
        if(m.read(0x803850)==1){
            runtime_.sprites.decode_palette(m.read(0x5cd41c),0x803a50);runtime_.palette.copy(0x804660+224*4,0x803a50+224*4,24);runtime_.palette.upload(0x8049e0,224,24);
            runtime_.sprites.decode_palette(m.read(0x5cd40c),0x803a50);m.write(0x803958,1);
        }
        runtime_.palette.copy(0x804660,0x803a50,16);runtime_.palette.upload(0x804660,0,16);m.write(0x80380c,0);return;
    }
    const auto next=recovered.invoke(0x45ef2d);if(next==3||next==6||next==0xffffffffu)m.write(globals::game_mode,next);if(next!=6)m.write(0x766f74,0xffffffffu);
}
bool Battle::ui_service(Address entry,RecoveredBattle& code){
    auto& m=runtime_.memory;auto& surfaces=runtime_.surfaces;const auto arg=[&](unsigned i){return code.argument(i);};
    const auto rect=[&](Address at){return Rect{signed32(code.read(at)),signed32(code.read(at+4)),signed32(code.read(at+8)),signed32(code.read(at+12))};};
    const auto whole=[&](Address surface){const auto& image=surfaces.get(surface);return Rect{0,0,int(image.width),int(image.height)};};
    const auto text=[&](Address at){std::string out;while(code.read(at,1)){out.push_back(char(code.read(at++,1)));if(out.size()>4096)throw Fault(at,"menu text exceeds source buffer");}return out;};
    switch(entry){
    case 0x406576:case 0x40664a:
        runtime_.graphics.queue_text_overlay(signed32(arg(0)),signed32(arg(1)),arg(2),arg(3),code.format_text(arg(4),code.r[4]+24),entry==0x40664a);code.result(0);return true;
    case 0x407e14:runtime_.graphics.text_overlays(runtime_.palette);code.result(0);return true;
    case 0x406ce2:runtime_.viewport.center(signed32(arg(0)),signed32(arg(1)),arg(2)!=0,arg(3)!=0);code.result(0,16);return true;
    case 0x40bba1:{const bool cached=m.read(0x514b94+arg(0)*64)!=0;runtime_.sprites.character(arg(0));code.result(cached,4);return true;}
    case 0x40bf56:code.result(runtime_.sprites.flush_deferred());return true;
    case 0x411de0:runtime_.graphics.show_direct_text(arg(0));code.result(0,4);return true;
    case 0x411feb:runtime_.graphics.clear_direct_text();code.result(0);return true;
    case 0x401fab:case 0x402070:case 0x40225e:case 0x4023f7:{
        const unsigned kind=entry==0x401fab?1:entry==0x402070?2:entry==0x40225e?3:4;
        runtime_.transition.rectangle(kind,arg(0),rect(arg(1)),arg(2),kind==4?arg(3):0);
        code.result(0,kind==4?16:12);return true;
    }
    case 0x4050fa:{
        const auto temporary=m.allocate_zeroed(1024);
        runtime_.sprites.decode_palette(arg(0),temporary,arg(2));
        if(arg(1)){for(unsigned i=0;i<256;++i)code.write(arg(1)+i*4,m.read(temporary+i*4));}
        else runtime_.palette.upload(temporary);
        m.release_allocation(temporary);code.result(0,12);return true;
    }
    case 0x40bb5b:code.result(runtime_.sprites.prepare_indexed(arg(0)),4);return true;
    case 0x4057ba:{const auto sprite=runtime_.sprites.sheet(arg(1),arg(2));const unsigned values[]={unsigned(sprite.rect.left),unsigned(sprite.rect.top),unsigned(sprite.rect.right),unsigned(sprite.rect.bottom)};for(unsigned i=0;i<4;++i)code.write(arg(0)+i*4,values[i]);code.result(arg(0),12);return true;}
    case 0x406044:{
        const auto surface=runtime_.sprites.decode_surface(arg(0),0,arg(4));const auto& image=surfaces.get(surface);
        if(arg(1))code.write(arg(1),image.width);if(arg(2))code.write(arg(2),image.height);
        if(arg(3))for(unsigned i=0;i<256;++i){const auto color=image.palette[i];code.write(arg(3)+i*4,color.r|(std::uint32_t(color.g)<<8)|(std::uint32_t(color.b)<<16));}code.result(surface,20);return true;
    }
    case 0x406721:{
        const auto out=code.format_text(arg(6),code.r[4]+32);const auto background=arg(2);
        code.result(runtime_.graphics.plain_text(arg(0),rect(arg(4)),arg(3),runtime_.palette.index(arg(1)),out,arg(5),std::nullopt,signed32(background)<0?std::nullopt:std::optional<std::uint8_t>(runtime_.palette.index(background))));return true;
    }
    case 0x405d43:{const auto& image=surfaces.get(arg(0));if(arg(1))code.write(arg(1),image.width);if(arg(2))code.write(arg(2),image.height);code.result(0,12);return true;}
    case 0x406996:runtime_.viewport.blit_borders(arg(0));code.result(0,4);return true;
    case 0x40587f:{const auto sprite=runtime_.sprites.sheet(arg(2),arg(3));if(arg(4)&~(blit_flags::fast_wait|blit_flags::fast_source_key))throw Fault(entry,"unsupported sheet blit flags");surfaces.blit(m.read(globals::render_target_surface),signed32(arg(0)),signed32(arg(1)),sprite.surface,sprite.rect,bool(arg(4)&1),rect(globals::clip_left));code.result(0,20);return true;}
    case 0x4067ec:{
        const auto color=arg(2),background=arg(3);const auto out=code.format_text(arg(4),code.r[4]+24);
        const auto result=runtime_.graphics.plain_text(m.read(globals::render_target_surface),{signed32(arg(0)),signed32(arg(1)),int(m.read(globals::framebuffer_width)),int(m.read(globals::framebuffer_height))},m.read(0x6e0e08),signed32(color)<0?255:runtime_.palette.index(color),out,0,std::nullopt,signed32(background)<0?std::nullopt:std::optional<std::uint8_t>(runtime_.palette.index(background)));code.result(result);return true;
    }
    case 0x43934c:open_menu_assets(arg(0)!=0);code.result(0,4);return true;
    case 0x4394ea:close_menu_assets();code.result(0);return true;
    case 0x405f84:code.result(surfaces.create(arg(0),arg(1)),8);return true;
    case 0x406012:if(const auto handle=code.read(arg(0))){text_contexts_.erase(handle);surfaces.release(handle);code.write(arg(0),0);}code.result(0,8);return true;
    case 0x405c01:surfaces.get(arg(0));code.result(arg(0),4);return true; // CPU drawing context = its surface.
    case 0x405c20:code.result(0,8);return true;
    case 0x405c8c:surfaces.clear(arg(0),arg(2)?rect(arg(2)):whole(arg(0)),std::uint8_t(arg(1)));code.result(0,12);return true;
    case 0x405cbc:
        surfaces.clear(m.read(globals::primary_surface),whole(m.read(globals::primary_surface)),std::uint8_t(arg(0)));
        surfaces.clear(m.read(globals::back_surface),whole(m.read(globals::back_surface)),std::uint8_t(arg(0)));code.result(0,4);return true;
    case 0x405db3:case 0x405dd6:
        if(arg(5)&~(blit_flags::fast_wait|blit_flags::fast_source_key))throw Fault(entry,"unsupported menu BltFast flags");
        surfaces.blit(arg(0),signed32(arg(1)),signed32(arg(2)),arg(3),arg(4)?rect(arg(4)):whole(arg(3)),bool(arg(5)&1),entry==0x405dd6?std::optional<Rect>(rect(globals::clip_left)):std::nullopt);code.result(0,24);return true;
    case 0x405eab:case 0x405ed1:
        if(arg(4)&~(blit_flags::rectangle_wait|blit_flags::rectangle_source_key))throw Fault(entry,"unsupported original rectangle blit effect");surfaces.stretch(arg(0),arg(1)?rect(arg(1)):whole(arg(0)),arg(2),arg(3)?rect(arg(3)):whole(arg(2)),bool(arg(4)&blit_flags::rectangle_source_key),entry==0x405ed1?std::optional<Rect>(rect(globals::clip_left)):std::nullopt);code.result(0,24);return true;
    case 0x404c56:
        if(arg(1)>256||arg(2)>256-arg(1))throw Fault(entry,"palette capture outside256entries");
        for(unsigned i=0;i<arg(2);++i){const auto index=arg(1)+i;auto value=runtime_.palette.raw_entries()[index];if(m.read(globals::video_mode_flags)&8){if(!index)value=0;if(index==255)value=0xffffff;}code.write(arg(0)+i*4,value);}code.result(0,12);return true;
    case 0x404dbb:
        for(unsigned i=0;i<arg(3);++i){const auto dst=arg(0)+i*4,src=arg(1)+i*4;code.write(dst+3,code.read(src+3,1),1);for(unsigned c=0;c<3;++c){const auto value=signed32(code.read(src+c,1)*arg(2))/100;code.write(dst+c,value<1?0:value>254?255:unsigned(value),1);}}code.result(0,16);return true;
    case 0x40536b:{
        std::vector<Address> temporary;const auto palette=[&](Address source){if(source<0x1000000||source>=0x1010000)return source;const auto copy=m.allocate_zeroed(1024);temporary.push_back(copy);for(unsigned i=0;i<256;++i)m.write(copy+i*4,code.read(source+i*4));return copy;};
        const auto final=palette(arg(0)),initial=palette(arg(1));runtime_.palette.begin(final,initial,arg(2),arg(3));for(auto allocation:temporary)m.release_allocation(allocation);code.result(0,16);return true;
    }
    case 0x405572:code.result(runtime_.palette.busy());return true;
    case 0x4397db:{
        const auto out=code.format_text(arg(6),code.r[4]+32);
        runtime_.graphics.menu_text(arg(0),signed32(arg(1)),signed32(arg(2)),arg(3),0x1000000u|runtime_.palette.index(arg(4)),0x1000000u|runtime_.palette.index(arg(5)),out);code.result(0);return true;
    }
    case 0x859550:for(unsigned i=0;i<4;++i)code.write(arg(0)+i*4,arg(i+1));code.result(1,20);return true; // SetRect.
    case 0x8594c4:{const auto value=rect(arg(1));const unsigned words[]={unsigned(value.left),unsigned(value.top),unsigned(value.right),unsigned(value.bottom)};for(unsigned i=0;i<4;++i)code.write(arg(0)+i*4,words[i]);code.result(1,8);return true;}
    case 0x8594a8:for(unsigned i=0;i<4;++i)code.write(arg(0)+i*4,code.read(arg(0)+i*4)+arg((i&1)+1));code.result(1,12);return true;
    case 0x8594ec:for(unsigned i=0;i<4;++i)code.write(arg(0)+i*4,code.read(arg(0)+i*4)+(i<2?0u-arg((i&1)+1):arg((i&1)+1)));code.result(1,12);return true;
    case 0x859500:{const auto bounds=rect(arg(0));const int x=signed32(arg(1)),y=signed32(arg(2));code.result(x>=bounds.left&&x<bounds.right&&y>=bounds.top&&y<bounds.bottom,12);return true;}
    case 0x859364:{auto& context=text_contexts_[arg(0)];const auto previous=0x24000000+context.font,value=arg(1);if(value>=0x24000000&&value<0x24000016)context.font=value-0x24000000;code.result(previous,8);return true;}
    case 0x859374:if(arg(0)!=tables::worldmap_logfont)throw Fault(arg(0),"unregistered custom LOGFONT");code.result(0x24000015,4);return true;
    case 0x85936c:{const auto handle=m.allocate_zeroed(16);text_regions_.emplace(handle,rect(arg(0)));code.result(handle,4);return true;}
    case 0x859370:
        if(text_regions_.erase(arg(0))){m.release_allocation(arg(0));code.result(1,4);return true;}
        if(arg(0)<0x24000000||arg(0)>=0x24000016){code.result(0,4);return true;}code.result(1,4);return true;
    case 0x859368:
        //436300 passes its stack RECT as an HRGN; this invalid region request
        //leaves the surface's normal full bounds in effect (returnERROR).
        if(arg(1)>=0x1000000&&arg(1)<0x1010000){code.result(0,8);return true;}
        if(!arg(1)){text_contexts_[arg(0)].clip.reset();code.result(1,8);return true;}
        if(const auto region=text_regions_.find(arg(1));region!=text_regions_.end()){text_contexts_[arg(0)].clip=region->second;code.result(2,8);return true;}code.result(0,8);return true;
    case 0x859384:{auto& context=text_contexts_[arg(0)];const auto old=context.background_mode;context.background_mode=arg(1);code.result(old,8);return true;}
    case 0x859388:{auto& context=text_contexts_[arg(0)];const auto old=context.color;context.color=arg(1);code.result(old,8);return true;}
    case 0x859378:{auto& context=text_contexts_[arg(0)];const auto old=context.background_color;context.background_color=arg(1);code.result(old,8);return true;}
    case 0x859390:code.result(arg(1),12);return true;
    case 0x8594d0:{
        const auto& context=text_contexts_[arg(0)];
        const auto pointer=arg(1),flags=arg(4);const auto string=signed32(arg(2))<0?text(pointer):[&](){std::string s;for(unsigned i=0;i<arg(2);++i)s.push_back(char(code.read(pointer+i,1)));return s;}();
        code.result(runtime_.graphics.plain_text(arg(0),rect(arg(3)),context.font,runtime_.palette.index(context.color),string,flags,context.clip,context.background_mode==2?std::optional<std::uint8_t>(runtime_.palette.index(context.background_color)):std::nullopt),20);return true;
    }
    default:return false;
    }
}
} // namespace fsb::core
