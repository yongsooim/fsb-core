#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
Address Actors::spawn_sequence(unsigned id){
    if(id>=668)throw Fault(id,"sequence actor template outside original table");
    int canonical=int(id);const auto link=signed32(memory_.read(tables::actor_alias_ids+id*68));
    if(link!=int(id)){
        canonical=-1;
        if(int(id)<link){for(int i=int(id)-1;i>16;--i)if(memory_.read(tables::actor_alias_ids+unsigned(i)*68)==id){canonical=i;break;}}
        else for(unsigned i=id+1;i<668;++i)if(memory_.read(tables::actor_alias_ids+i*68)==id){canonical=int(i);break;}
    }
    if(canonical<0)throw Fault(id,"sequence actor has no canonical template");
    const auto object=spawn_effect(0,0,0,0,memory_.read(0x5b3598+unsigned(canonical)*68));
    if(!object)throw Fault(id,"sequence actor pool exhausted");
    memory_.write(object+actor_offset::template_link,unsigned(canonical));memory_.write(tables::actor_slot_ids+unsigned(canonical)*68,memory_.read(object));memory_.write(tables::actor_object_pointers+unsigned(canonical)*68,object);return object;
}
void Actors::spawn_collected(){
    const auto count=memory_.read(globals::collected_actor_count);if(count>50)throw Fault(count,"collected NPC table exceeds50 entries");
    for(unsigned i=0;i<count;++i){
        const auto record=0x8019d0+i*40,object=slot(i+10),id=memory_.read(record),direction=memory_.read(record+20);
        if(id>=668||direction>=8)throw Fault(id,"invalid collected NPC template/direction");
        set_actor_tile_position(memory_,object,signed32(memory_.read(record+8)),signed32(memory_.read(record+12)),signed32(memory_.read(record+16)));
        for(auto offset:{0x20,0x24,0x28,0x2c,0x30,0x34,0x38,0x104,0x108,0x120,0x124})memory_.write(object+offset,0);
        memory_.write(object+actor_offset::flags,0x24e);memory_.write(object+actor_offset::facing,direction);memory_.write(object+actor_offset::target_facing,direction);
        memory_.write(object+actor_offset::sprite_base,memory_.read(0x5b3598+id*68));memory_.write(object+actor_offset::sprite_selector,0x20000);
        memory_.write(object+0x118,i);memory_.write(object+actor_offset::template_link,id);
        memory_.write(object+actor_offset::sprite_frame,memory_.read(tables::direction_to_cardinal+direction*4)*6);
        const auto callback=memory_.read(0x5be718+memory_.read(tables::actor_callback_kinds+id*68)*4);
        if(callback){memory_.write(object+actor_offset::flags,0x1024e);memory_.write(object+actor_offset::callback_state,0);memory_.write(object+actor_offset::callback,callback);}
        memory_.write(object+actor_offset::dialogue_handle,0);memory_.write(tables::actor_slot_ids+id*68,memory_.read(object));memory_.write(tables::actor_object_pointers+id*68,object);
        memory_.write(globals::actor_active_count,memory_.read(globals::actor_active_count)+1);
    }
}
void Actors::tick_bound_son(Address object){
    // Actual447e31, a pose callback rather than a battle/effect VM.
    memory_.write(object+actor_offset::tile_x,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_x_q16))/units::q16_one));
    memory_.write(object+actor_offset::tile_y,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_y_q16))/units::q16_one));
    const auto flags=memory_.read(object+actor_offset::flags);memory_.write(object+actor_offset::flags,flags|64);
    memory_.write(object+actor_offset::sprite_selector,0x20007);memory_.write(object+actor_offset::sprite_frame,(flags&0x40000000)!=0);
    memory_.write(object+actor_offset::screen_anchor_x,0);memory_.write(object+actor_offset::screen_anchor_y,0);
}
std::optional<unsigned> Actors::tick_platform(Address object){
    if(signed32(memory_.read(object+actor_offset::callback_state))<0)return {};
    if(memory_.read(object+0x160)!=memory_.read(globals::current_map_id)){finalize(object);return {};}
    const auto state=memory_.read(object+0x108);
    if(signed32(state)<0)memory_.write(object+actor_offset::elevation,0xf0000000);
    else{
        const auto actor=slot(memory_.read(globals::active_party_index));
        memory_.write(object+0x110,memory_.read(actor+actor_offset::facing));memory_.write(object+actor_offset::sprite_frame,state);
        memory_.write(object+actor_offset::world_x,memory_.read(actor+actor_offset::world_x));memory_.write(object+actor_offset::world_y,memory_.read(actor+actor_offset::world_y)+0x300000);
        memory_.write(object+actor_offset::elevation,memory_.read(actor+actor_offset::elevation));
    }
    memory_.write(object+actor_offset::tile_x,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_x_q16))/units::q16_one));
    memory_.write(object+actor_offset::tile_y,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_y_q16))/units::q16_one));
    return state==3?std::optional<unsigned>(0x158):std::nullopt;
}
ActorMotionStep Actors::tick_npc(Address object){
    const auto read=[&](unsigned offset){return memory_.read(object+offset);};
    const auto write=[&](unsigned offset,std::uint32_t value){memory_.write(object+offset,value);};
    const int x=signed32(read(actor_offset::tile_x_q16))/units::q16_one,y=signed32(read(actor_offset::tile_y_q16))/units::q16_one,z=signed32(read(actor_offset::layer_q16))/units::q16_one;
    const int width=signed32(memory_.read(globals::map_layer_width+z*layout::map_layer_stride)),height=signed32(memory_.read(globals::map_layer_height+z*layout::map_layer_stride));
    const auto cell=[&](int cx,int cy){return std::uint32_t(z)*4096+std::uint32_t(cy*width+cx);};
    const auto attr=[&](int cx,int cy){return memory_.read(globals::tile_attributes+cell(cx,cy)*4);};
    const auto raw=[&](int cx,int cy){return memory_.read(globals::map_raw_attributes+z*layout::map_layer_stride+std::uint32_t(cy*width+cx)*4);};
    const auto center=attr(x,y),current_raw=raw(x,y);const auto motion=step_motion(object);
    const auto finish=[&](){resolve_field_frame(object);return motion;};
    if(motion.active||(read(actor_offset::flags)&0x40000000)||read(actor_offset::motion_state))return finish();
    write(actor_offset::tile_x,std::uint32_t(x));write(actor_offset::tile_y,std::uint32_t(y));if(read(0x2c))return finish();
    const auto facing=read(actor_offset::facing);if(facing>=8)throw Fault(object,"NPC facing outside direction table");
    const auto cardinal=memory_.read(tables::direction_to_cardinal+facing*4),roll=crt_rand(memory_)%200,profile=read(0x118);
    const auto bounds=0x8019e8+profile*40;
    const auto speed=memory_.read(tables::actor_dialogue_styles+read(actor_offset::template_link)*68)&0xf00;
    if(!read(0x34)){
        if(memory_.read(bounds)==memory_.read(bounds+8)&&memory_.read(bounds+4)==memory_.read(bounds+12))write(0x30,4);
        else{
            unsigned moves=4,timer=4;
            if(speed==0||speed==0x200){moves=crt_rand(memory_)%3+1;timer=crt_rand(memory_)%320+48;}
            else if(speed==0x100){moves=crt_rand(memory_)%4+1;timer=crt_rand(memory_)%180+24;}
            else if(speed==0x300){moves=crt_rand(memory_)%2+1;timer=crt_rand(memory_)%400+60;}
            if(roll>=40)write(0x30,timer);
            else{
                if(roll<10){if(signed32(memory_.read(bounds+4))<y)write(0x30,0);}
                else if(roll<20){if(y<signed32(memory_.read(bounds+12)))write(0x30,1);}
                else if(roll<30){if(signed32(memory_.read(bounds))<x)write(0x30,2);}
                else if(x<signed32(memory_.read(bounds+8)))write(0x30,3);
                write(0x34,moves);
            }
        }
    }
    const auto command=read(0x30),direction=command<=3?command:4,target=command<=3?command:cardinal;
    if(signed32(read(0x34))>0)write(0x34,read(0x34)-1);
    if(target!=cardinal&&!memory_.read(0x77ecdc)){write(actor_offset::motion_state,5);write(actor_offset::target_facing,target);return finish();}
    if(direction>3||(direction==0&&y<2)||(direction==1&&y>=height-1)||(direction==2&&x<1)||(direction==3&&x>=width-1))return finish();
    if((direction==2&&((current_raw>0x32&&current_raw<0x37)||(current_raw>0x37&&current_raw<0x3c)))||
       (direction==3&&((current_raw>0x31&&current_raw<0x36)||(current_raw>0x36&&current_raw<0x3b)))){write(0x34,0);return finish();}
    constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};const auto nx=x+dx[direction],ny=y+dy[direction];
    if(!(attr(nx,ny)&0x200)&&!(center&(0x20u<<direction))&&!(memory_.read(globals::tile_occupancy+cell(nx,ny)*4)&0x10000)&&
       (direction>=2||(!(center&0x40000)&&raw(nx,ny)<20))){
        unsigned delta=0,state=0;
        switch(speed){case 0:delta=0x1000;state=13;break;case 0x100:delta=0x4000;state=2;break;case 0x200:delta=0x800;state=14;break;case 0x300:delta=0x400;state=15;break;}
        if(delta){write(actor_offset::tile_x_q16,read(actor_offset::tile_x_q16)+std::uint32_t(dx[direction])*delta);write(actor_offset::tile_y_q16,read(actor_offset::tile_y_q16)+std::uint32_t(dy[direction])*delta);write(actor_offset::motion_state,state);}
        write(actor_offset::tile_x,std::uint32_t(nx));write(actor_offset::tile_y,std::uint32_t(ny));write(actor_offset::facing,direction);write(actor_offset::motion_frame,1);
    }else write(0x34,0);
    return finish();
}
} // namespace fsb::core
