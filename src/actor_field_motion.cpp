#include "fsb_core/actors.hpp"
#include "fsb_core/field.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
namespace {
// 45c55c motion states are actor states, not event-VM opcodes.
constexpr unsigned idle=0,normal_step=1,fast_step=2,hop=3,static_step=4,turn=5,
    override_step=7,jump=8,restore=12,slow16=13,slow32=14,slow64=15,slow128=16;
unsigned variant3_frame(unsigned phase,bool override_initialized=false){
    //45cc1b initializes16entries but indexes modulo20. State7 initializes
    //the following direction table too. Other states read indeterminate stack
    //bytes for16..19; the portable visual policy zero-initializes that tail.
    constexpr unsigned cycle[]={0,0,0,0,1,1,1,1,2,2,2,2,1,1,1,1,0,0,0,0};
    constexpr unsigned override_tail[]={0,4,3,7};
    return override_initialized&&phase>=16?override_tail[phase-16]:cycle[phase];
}
}
ActorMotionStep Actors::step_motion(Address object){
    const auto read=[&](unsigned offset){return memory_.read(object+offset);};
    const auto write=[&](unsigned offset,std::uint32_t value){memory_.write(object+offset,value);};
    const auto state=read(actor_offset::motion_state);ActorMotionStep result;
    if(state==turn||state==restore){
        const auto facing=read(actor_offset::facing),target=read(actor_offset::target_facing);
        if(facing>=8||target>=8)throw Fault(object,"motion turn facing outside original table");
        if(facing==target){
            if(state==restore){
                const unsigned destination[]={actor_offset::motion_state,actor_offset::facing,actor_offset::motion_frame,actor_offset::target_facing};
                for(unsigned i=0;i<4;++i){const auto value=read(0x2c+i*4);write(0x2c+i*4,0);write(destination[i],value);}
            }else write(actor_offset::motion_state,idle);
        }else{
            const auto current=memory_.read(0x5d05c8+facing*4),desired=memory_.read(0x5d05c8+target*4);
            const auto next=current+(desired<current?(current-desired>3?1:7):(desired-current>4?7:1));
            write(actor_offset::facing,memory_.read(0x5d05a8+(next%8)*4));
        }
        result.active=true;return result;
    }
    unsigned frames,delta;
    switch(state){
    case normal_step:case override_step:frames=8;delta=0x2000;break;
    case fast_step:frames=4;delta=0x4000;break;
    case hop:frames=16;delta=0x2000;break;
    case slow16:frames=16;delta=0x1000;break;
    case slow32:frames=32;delta=0x800;break;
    case slow64:frames=64;delta=0x400;break;
    case slow128:frames=128;delta=0x200;break;
    default:return result;
    }
    const auto frame=(read(actor_offset::motion_frame)+1)%frames;write(actor_offset::motion_frame,frame);
    if(state==hop){if(frame==2)result.sound=0x128;else if(!frame)result.sound=0x129;}
    if(!frame){
        write(actor_offset::motion_state,idle);write(actor_offset::frame_group,(read(actor_offset::frame_group)-1)&1);
        if(state==hop)write(actor_offset::elevation,read(actor_offset::elevation)-memory_.read(0x5d0708));
        if(state==normal_step&&memory_.read(object)<10){
            const auto member=memory_.read(globals::party_actor_ids+memory_.read(object)*4);
            if(memory_.read(0x607a12+member*0xbc,1)&0x20)memory_.write(0x77ece0,1,1);
        }
        if(state!=override_step&&memory_.read(object)==memory_.read(globals::active_party_index))memory_.write(0x804aac,memory_.read(0x804aac)+1);
    }
    const auto facing=state==override_step?memory_.read(object+0x3c,2):read(actor_offset::facing);
    if(facing<8&&(state!=hop||facing<4)){
        const int dx[]={0,0,-1,1,1,-1,-1,1},dy[]={-1,1,0,0,-1,1,-1,1};
        if(dx[facing])write(actor_offset::tile_x_q16,read(actor_offset::tile_x_q16)+std::uint32_t(dx[facing])*delta);
        if(dy[facing])write(actor_offset::tile_y_q16,read(actor_offset::tile_y_q16)+std::uint32_t(dy[facing])*delta);
    }
    result.active=true;return result;
}
void Actors::apply_entry_direction(Address object,std::uint32_t direction){
    if(direction==0xffffffffu){memory_.write(object+actor_offset::motion_state,0);memory_.write(object+actor_offset::motion_frame,0);return;}
    memory_.write(object+actor_offset::motion_state,1);memory_.write(object+actor_offset::motion_frame,1);memory_.write(object+actor_offset::facing,direction);
    mark_field_triggers(memory_,signed32(memory_.read(object+actor_offset::tile_x)),signed32(memory_.read(object+actor_offset::tile_y)),0x400,direction);
    // Only0..7 index the step table; the original leaves the position alone
    // for anything else, having already published the state and facing above.
    if(direction>=8)return;
    constexpr int dx[]={0,0,-1,1,1,-1,-1,1},dy[]={-1,1,0,0,-1,1,-1,1};
    if(dx[direction]){memory_.write(object+actor_offset::tile_x_q16,memory_.read(object+actor_offset::tile_x_q16)+std::uint32_t(dx[direction])*0x2000);memory_.write(object+actor_offset::tile_x,memory_.read(object+actor_offset::tile_x)+std::uint32_t(dx[direction]));}
    if(dy[direction]){memory_.write(object+actor_offset::tile_y_q16,memory_.read(object+actor_offset::tile_y_q16)+std::uint32_t(dy[direction])*0x2000);memory_.write(object+actor_offset::tile_y,memory_.read(object+actor_offset::tile_y)+std::uint32_t(dy[direction]));}
}
void Actors::resolve_field_frame(Address object){
    const auto read=[&](unsigned offset){return memory_.read(object+offset);};
    const auto write=[&](unsigned offset,std::uint32_t value){memory_.write(object+offset,value);};
    write(actor_offset::world_x,read(actor_offset::tile_x_q16)<<6);write(actor_offset::world_y,read(actor_offset::tile_y_q16)*48);
    const auto state=read(actor_offset::motion_state),counter=read(actor_offset::motion_frame),group=read(actor_offset::frame_group),facing=read(actor_offset::facing),variant=(read(actor_offset::flags)>>4)&3;
    if(facing>=8)throw Fault(object,"animation facing outside original table");
    const auto cardinal=memory_.read(tables::direction_to_cardinal+facing*4);
    const auto global_cycle=[&](bool initialized=false){
        const auto phase=memory_.read(globals::presented_frame_counter)%20;
        return variant3_frame(phase,initialized);
    };
    const auto table=[&](unsigned index){return memory_.read(0x5d05e8+index*4);};
    unsigned mode=0x20000,frame;
    switch(state){
    case idle:frame=cardinal*6;break;
    case turn:case restore:frame=facing<4?cardinal*6:memory_.read(0x5d0578+facing*4)*6+5;break;
    case normal_step:case static_step:frame=variant==3?global_cycle():table(counter+(group+variant*2)*8)+cardinal*6;break;
    case fast_step:
        mode+=memory_.read(0x5d06a8+(counter+group*4)*4)>>4;
        frame=variant==3?global_cycle():(memory_.read(0x5d06a8+(counter+(group+variant*2)*4)*4)&15)+cardinal*(3-(mode&0xffff))*2;break;
    case hop:case jump:
        frame=state==hop&&variant==3?global_cycle():table(counter+(group+(state==hop?variant*2:0))*8)+cardinal*6;
        write(actor_offset::elevation,read(actor_offset::elevation)-memory_.read(0x5d0708+counter*4));break;
    case slow16:case slow32:frame=table(counter/(state==slow16?2:4)+group*8)+cardinal*6;break;
    case slow64:case slow128:frame=table((counter/4)%16)+cardinal*6;break;
    case override_step:{
        if(facing>3)throw Fault(object,"original override frame requires cardinal facing");
        constexpr unsigned base[]={0,4,6,2},directions[]={0,4,3,7,1,5,2,6};
        const auto direction=directions[(base[facing]+counter)%8];
        frame=variant==3?global_cycle(true):direction<4?memory_.read(tables::direction_to_cardinal+direction*4)*6:memory_.read(0x5d0578+direction*4)*6+5;break;
    }
    default:return;
    }
    write(actor_offset::sprite_selector,mode);write(actor_offset::sprite_frame,frame);
}
void Actors::resolve_battle_frame(Address object){
    const auto read=[&](unsigned offset){return memory_.read(object+offset);};
    const auto write=[&](unsigned offset,std::uint32_t value){memory_.write(object+offset,value);};
    const auto flags=read(actor_offset::flags);
    if(read(actor_offset::motion_state)||!(flags&0x500)){resolve_field_frame(object);return;}
    write(actor_offset::world_x,read(actor_offset::tile_x_q16)<<6);write(actor_offset::world_y,read(actor_offset::tile_y_q16)*48);
    const auto counter=read(actor_offset::motion_frame)+1;write(actor_offset::motion_frame,counter);
    bool low=false,status=false;
    if(flags&0x100){
        const auto id=memory_.read(globals::party_actor_ids+memory_.read(object)*4),record=0x607a08+id*188;
        low=signed32(sequence_alu(alu::signed_divide,memory_.read(record+0x1c)*100,memory_.read(record+0x18)))<40;
        status=(memory_.read(record+9,1)&10)!=0;
    }
    if(flags&0x400){
        const auto record=0x806b60+read(0x118)*36,definition=0x609ca0+memory_.read(record+4)*124;
        low|=signed32(sequence_alu(alu::signed_divide,memory_.read(record+20)*100,memory_.read(definition+24)))<40;
        status|=(memory_.read(record+9,1)&10)!=0;
    }
    const auto variant=(flags>>4)&3,facing=read(actor_offset::facing);if(facing>=8)throw Fault(object,"battle idle facing outside original table");
    const auto cardinal=memory_.read(tables::direction_to_cardinal+facing*4);
    const auto toggle=[&](){return sequence_alu(alu::signed_divide,counter,memory_.read(0x5d0268+(read(actor_offset::sprite_base)/8)*4))&1;};
    unsigned mode=0x20000,frame=0;
    if(low&&!read(0x2c)){
        mode=0x20004;
        frame=status?cardinal*3+(variant<2?1:variant==2?2:0):cardinal*3+1+toggle();
    }else if(!variant){
        if(status||counter==1)frame=cardinal*6;else{mode=0x20006;frame=cardinal+toggle()*4;}
    }else if(variant==1)frame=cardinal*6+memory_.read(0x5d0628+(status?0:counter&15)*4);
    else if(variant==2){
        if(status){mode=0x20004;frame=cardinal*3+2;}else frame=cardinal*6+memory_.read(0x5d0668+(counter&15)*4);
    }else if(!status){
        frame=variant3_frame(memory_.read(globals::presented_frame_counter)%20);
    }
    write(actor_offset::sprite_selector,mode);write(actor_offset::sprite_frame,frame);
}
ActorMotionStep Actors::tick_default_visual(Address object){
    const auto result=step_motion(object);
    memory_.write(object+actor_offset::tile_x,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_x_q16))/units::q16_one));
    memory_.write(object+actor_offset::tile_y,std::uint32_t(signed32(memory_.read(object+actor_offset::tile_y_q16))/units::q16_one));
    if(memory_.read(globals::game_mode)==9)resolve_battle_frame(object);else resolve_field_frame(object);return result;
}
} // namespace fsb::core
