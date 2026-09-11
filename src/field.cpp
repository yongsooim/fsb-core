#include "fsb_core/field.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
namespace {
constexpr unsigned blocked=0x200,height_connector=0x1000,hop_pair=0x20000,
    hop_aligned=0x400,special_block=0x40000,occupied=0x10000;
constexpr unsigned overlay_registered=0x1000000,overlay_running=0x2000000,
    backtrack_trigger=0x400,autostep_sound=0x168;
constexpr int dx[]={0,0,-1,1,1,-1,-1,1},dy[]={-1,1,0,0,-1,1,-1,1};
}
bool Field::mark_triggers(int x,int y,unsigned kind,unsigned direction){
    return mark_field_triggers(memory_,x,y,kind,direction);
}
bool mark_field_triggers(Memory& memory_,int x,int y,unsigned kind,unsigned direction){
    // 457ba4: marking and callback execution happen in different frame phases.
    bool marked=false;const auto bit=memory_.read(0x5d0048+direction%4,1);
    for(Address entry=globals::overlay_effects;entry<globals::overlay_effects_end;entry+=48){
        const auto flags=memory_.read(entry);
        if((flags&0x3000000)!=0x1000000||!(flags&kind&0xf00)||!(flags&bit&15))continue;
        if(signed32(memory_.read(entry+4))<=x&&x<=signed32(memory_.read(entry+12))&&
           signed32(memory_.read(entry+8))<=y&&y<=signed32(memory_.read(entry+16))){memory_.write(entry,flags|0x2000000);marked=true;}
    }
    return marked;
}
void Field::probe_action(Address actor){
    if(interact){interact(actor);return;}
    // 45d395 probes an adjacent actor, then one tile farther through clear
    // directional edges. Pickup objects are adjacent-only.
    const auto direction=memory_.read(actor+actor_offset::facing);if(direction>3)return;
    const auto width=memory_.read(globals::grid_row_stride),layer=memory_.read(actor+actor_offset::layer_q16)/units::q16_one;
    const auto x=memory_.read(actor+actor_offset::tile_x),y=memory_.read(actor+actor_offset::tile_y);
    const auto delta=dy[direction]*int(width)+dx[direction];const auto cell=y*width+x,edge=0x20u<<direction;
    const auto plane=memory_.read(globals::path_grid_id)*4096;
    if(memory_.read(globals::tile_attributes+(plane+cell)*4)&edge)return;
    for(unsigned distance=1;distance<=2;++distance){
        const auto target=cell+std::uint32_t(delta)*distance;
        const auto entry=memory_.read(globals::tile_occupancy+(layer*4096+target)*4);
        if(entry&occupied){
            if(distance==2&&(entry&65535)>=90)return;
            throw Fault(actor,"field NPC/pickup interaction is not connected");
        }
        if(memory_.read(globals::tile_attributes+(plane+target)*4)&edge)return;
    }
}
void Field::tick_actor(Address actor){
    const auto read=[&](unsigned offset){return memory_.read(actor+offset);};
    const auto write=[&](unsigned offset,std::uint32_t value){memory_.write(actor+offset,value);};
    const int x=signed32(read(actor_offset::tile_x_q16))/units::q16_one,y=signed32(read(actor_offset::tile_y_q16))/units::q16_one,z=signed32(read(actor_offset::layer_q16))/units::q16_one;
    if(z<0||z>1)throw Fault(actor,"field actor layer outside map");
    const int width=signed32(memory_.read(globals::map_layer_width+z*layout::map_layer_stride)),height=signed32(memory_.read(globals::map_layer_height+z*layout::map_layer_stride));
    const auto cell=[&](int cx,int cy){return std::uint32_t(z)*4096+std::uint32_t(cy*width+cx);};
    const auto attr=[&](int cx,int cy){return memory_.read(globals::tile_attributes+cell(cx,cy)*4);};
    const auto raw=[&](int cx,int cy){return memory_.read(globals::map_raw_attributes+z*layout::map_layer_stride+std::uint32_t(cy*width+cx)*4);};
    const auto center=attr(x,y),current_raw=raw(x,y);
    const auto motion=actors_.step_motion(actor);if(motion.sound)audio_.play_cue(*motion.sound);
    const auto state=read(actor_offset::motion_state);
    memory_.write(globals::field_transition_phase,state==0||state==5||state==12?memory_.read(globals::field_transition_phase)+1:0xffffffffu);
    if(!memory_.read(globals::field_transition_phase))mark_triggers(signed32(read(actor_offset::tile_x)),signed32(read(actor_offset::tile_y)),0x200,memory_.read(tables::opposite_directions+read(actor_offset::facing)*4));
    const auto finish=[&](){
        if(read(actor_offset::motion_state)==3&&read(actor_offset::motion_frame)==1){
            for(Address entry=globals::overlay_effects;entry<globals::overlay_effects_end;entry+=48){
                const auto flags=memory_.read(entry);
                if((flags&(overlay_registered|overlay_running))==overlay_registered&&(flags&backtrack_trigger)&&signed32(read(actor_offset::tile_x_q16))/units::q16_one==signed32(memory_.read(entry+4))&&signed32(read(actor_offset::tile_y_q16))/units::q16_one==signed32(memory_.read(entry+8)))memory_.write(entry,flags|overlay_running);
            }
        }
        if(memory_.read(globals::game_mode)==9)actors_.resolve_battle_frame(actor);else actors_.resolve_field_frame(actor);
    };
    if(signed32(memory_.read(0x5d0768))>=0)throw Fault(actor,"field gatewarp timeline is not connected");
    if(motion.active||!(read(actor_offset::flags)&0x80)||memory_.read(0x802ca0)||state){finish();return;}
    if((center&0x180000)==0x80000&&memory_.read(globals::field_transition_phase)==1)throw Fault(actor,"field MFO map transition is not connected");
    write(actor_offset::tile_x,std::uint32_t(x));write(actor_offset::tile_y,std::uint32_t(y));
    if((center&0x18000)==0x8000)throw Fault(actor,"field forced tile movement is not connected");
    if(memory_.read(0x8021d8)==1){memory_.write(0x8021d8,0);audio_.stop_cue(autostep_sound);}
    const auto facing=read(actor_offset::facing);if(facing>=8)throw Fault(actor,"field facing outside original direction table");
    const auto cardinal=memory_.read(tables::direction_to_cardinal+facing*4);
    unsigned direction=4,input_facing=cardinal;
    if(!(memory_.read(globals::control_key_state)&&memory_.read(0x804a60,1))){
        if(memory_.read(globals::up_held)||memory_.read(0x6da688))direction=0;
        else if(memory_.read(globals::down_held)||memory_.read(0x6da6a8))direction=1;
        else if(memory_.read(globals::left_held)||memory_.read(0x6da694))direction=2;
        else if(memory_.read(globals::right_held)||memory_.read(0x6da69c))direction=3;
        if(direction<4)input_facing=direction;
    }
    if(input_facing!=cardinal&&!memory_.read(0x77ecdc)){write(actor_offset::motion_state,5);write(actor_offset::target_facing,input_facing);}
    else if(memory_.read(globals::input_message)==input_message::key_down&&!(memory_.read(globals::input_flags)&0x40000000)){
        const auto key=memory_.read(globals::input_key);
        if(key==13||key==88||key==97){
            const auto opposite=memory_.read(tables::opposite_directions+facing*4);
            if(!mark_triggers(x+dx[facing],y+dy[facing],0x100,opposite)&&!memory_.read(0x802c9c,1)&&memory_.read(globals::current_event_id)==0xffffffffu)probe_action(actor);
        }
    }else if(direction<4&&((direction==0&&y>1)||(direction==1&&y<height-1)||(direction==2&&x>0)||(direction==3&&x<width-1))){
        const int nx=x+dx[direction],ny=y+dy[direction];const auto neighbor=attr(nx,ny);
        if(signed32(memory_.read(0x5f858c))>=0)throw Fault(actor,"active field door crossing is not connected");
        const auto commit=[&](unsigned facing,unsigned state,unsigned delta,int tx,int ty){
            write(actor_offset::tile_x_q16,read(actor_offset::tile_x_q16)+std::uint32_t(dx[facing])*delta);
            write(actor_offset::tile_y_q16,read(actor_offset::tile_y_q16)+std::uint32_t(dy[facing])*delta);
            write(actor_offset::tile_x,std::uint32_t(tx));write(actor_offset::tile_y,std::uint32_t(ty));
            write(actor_offset::facing,facing);write(actor_offset::motion_state,state);write(actor_offset::motion_frame,1);
        };
        if((center&hop_pair)&&(!(center&hop_aligned)||(center&0x1c000000)==(direction<<26))&&(neighbor&blocked)&&!(attr(x+dx[direction]*2,y+dy[direction]*2)&blocked))commit(direction,3,0x2000,x+dx[direction]*2,y+dy[direction]*2);
        else if(!(center&(0x20u<<direction))){
            int diagonal_y=y;
            if(direction==2){if(current_raw>=0x33&&current_raw<=0x36)diagonal_y=y+1;else if(current_raw>=0x38&&current_raw<=0x3b)diagonal_y=y-1;}
            if(direction==3){if(current_raw>=0x32&&current_raw<=0x35)diagonal_y=y-1;else if(current_raw>=0x37&&current_raw<=0x3a)diagonal_y=y+1;}
            if(direction>=2&&diagonal_y!=y){
                if(attr(nx,diagonal_y)&blocked)write(actor_offset::layer_q16,read(actor_offset::layer_q16)+(diagonal_y<y?0x10000u:0xffff0000u));
                commit(direction==2?(diagonal_y<y?6:5):(diagonal_y<y?4:7),1,0x2000,nx,diagonal_y);
            }else{
                bool allowed=!(neighbor&blocked)&&!(memory_.read(globals::tile_occupancy+cell(nx,ny)*4)&occupied)&&!(direction==0&&(center&special_block));
                if((neighbor&blocked)&&(center&height_connector)){
                    const auto link=(center>>26)&7,opposite=memory_.read(tables::opposite_directions+link*4);
                    if(link==direction||opposite==direction){write(actor_offset::layer_q16,read(actor_offset::layer_q16)+(link==direction?0x10000u:0xffff0000u));allowed=true;}
                }
                if(allowed){
                    const bool fast=memory_.read(globals::shift_key_state)||memory_.read(globals::numpad5_held)||memory_.read(0x773000);
                    commit(direction,fast?2:1,fast?0x4000:0x2000,nx,ny);
                    if(direction<2&&!(neighbor&blocked)&&(neighbor&height_connector)){
                        const auto link=(neighbor>>26)&7,opposite=memory_.read(tables::opposite_directions+link*4);
                        if(link==direction||opposite==direction)write(actor_offset::layer_q16,read(actor_offset::layer_q16)+(link==direction?0x10000u:0xffff0000u));
                    }
                }
            }
            if(direction>=2){
                const auto target_raw=raw(nx,diagonal_y);
                if(target_raw==(direction==2?0x39u:0x34u))write(actor_offset::layer_q16,read(actor_offset::layer_q16)+0x10000);
                else if(target_raw==(direction==2?0x34u:0x39u))write(actor_offset::layer_q16,read(actor_offset::layer_q16)-0x10000);
            }
        }
    }
    if(direction<4){
        const auto state=read(actor_offset::motion_state);
        if(!state)mark_triggers(x+dx[input_facing],y+dy[input_facing],0x800,memory_.read(tables::opposite_directions+input_facing*4));
        else if(state<=2)mark_triggers(signed32(read(actor_offset::tile_x))-dx[input_facing],signed32(read(actor_offset::tile_y))-dy[input_facing],0x400,read(actor_offset::facing));
    }
    finish();
}
} // namespace fsb::core
