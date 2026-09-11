#include "fsb_core/battle.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/combat/handler_lifecycle.hpp"

namespace fsb::core {
void Battle::show_banner(unsigned action){
    auto& m=runtime_.memory;if(!m.read(combat::handler_flow::action_banner_enabled)){m.write(combat::handler_flow::action_banner,0);return;}
    const auto h=runtime_.arena.allocate_after(m.read(0x6d56e8),0x464158,0x30000,0),object=*resolve_compact(m,h);m.write(combat::handler_flow::action_banner,object);m.write(object+0xe8,action);
}
void Battle::tick_banner(Address object){
    auto& m=runtime_.memory;auto& surfaces=runtime_.surfaces;const auto state=m.read(object+0x20);bool draw=false;
    const auto text=[&](Address at){std::string s;while(m.read(at,1)){s.push_back(char(m.read(at++,1)));if(s.size()>255)throw Fault(at,"banner text exceeds original buffer");}return s;};
    if(state==1){
        const auto surface=surfaces.create(640,64);m.write(object+0x188,surface);
        const auto actor=Actors::slot(m.read(0x7757e0)),enemy=m.read(BattleRules::enemy_record(m.read(actor+0x118))+4),action=m.read(object+0xe8);
        const auto name=text(m.read(0x609cac+enemy*124)),label=text(m.read(0x610c0c+action*32));
        runtime_.graphics.menu_text(surface,8,16,14,m.read(0x4a27d8),m.read(0x4a27dc),name);
        const auto style=(m.read(0x610c14+action*32)&0x800000)?0u:4u;
        runtime_.graphics.menu_text(surface,8,48,14,m.read(0x4a27d8+style*8),m.read(0x4a27dc+style*8),label);
        m.write(0x805a70,unsigned(name.size()*17));m.write(0x805a74,unsigned(label.size()*17));
        m.write(object+0x140,(m.read(0x6e12b0)/2+m.read(0x6e1468))*65536);m.write(object+0x144,(m.read(0x6e1440)/2+m.read(0x74b470))*65536);
        m.write(object+0x11c,0xfe3e0000u);m.write(object+0x134,0x3c0000);m.write(object+0x128,0x40000);m.write(object+0x20,10);
    }else if(state==10){
        const auto velocity=m.read(object+0x134)-m.read(object+0x128);m.write(object+0x134,velocity);m.write(object+0x11c,m.read(object+0x11c)+velocity);
        if(!velocity){m.write(object+0x20,20);m.write(object+0x158,0);}draw=true;
    }else if(state==20){const auto timer=m.read(object+0x158)+1;m.write(object+0x158,timer);if(timer==8)m.write(object+0x20,30);draw=true;}
    else if(state==30){
        const auto velocity=m.read(object+0x134)+m.read(object+0x128);m.write(object+0x134,velocity);m.write(object+0x11c,m.read(object+0x11c)+velocity);
        if(signed32(velocity)>0x3bffff){surfaces.release(m.read(object+0x188));m.write(object+0x188,0);m.write(combat::handler_flow::action_banner,0);m.write(object+0x20,0);return;}draw=true;
    }
    if(draw)for(unsigned row=0;row<2;++row){
        const int width=signed32(m.read(0x805a70+row*4)),x=signed32(m.read(object+0x140)+(row?0u-m.read(object+0x11c):m.read(object+0x11c)))/65536-width/2,y=signed32(m.read(object+0x144))/65536+int(row*32)-32;
        surfaces.blit(m.read(globals::render_target_surface),x,y,m.read(object+0x188),{0,int(row*32),width,int(row*32+32)},true);
    }
}
} // namespace fsb::core
