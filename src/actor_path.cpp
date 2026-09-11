#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/arena.hpp"
#include "fsb_core/raster.hpp"
#include "fsb_core/recovered_battle.hpp"
#include <algorithm>

namespace fsb::core {
namespace {
constexpr unsigned height_connector_flag=0x1000; // Tile flag, separate from4096-cell capacity.
void flood(Memory& m,unsigned grid,int sx,int sy,unsigned flags,std::optional<Rect> bounds=std::nullopt){
    const int width=signed32(m.read(globals::grid_row_stride)),height=signed32(m.read(globals::grid_height));
    if(width<1||height<1||std::uint64_t(width)*height>capacity::map_cells)throw Fault(routines::flood_move_cost,"invalid path grid");
    const auto area=bounds.value_or(Rect{1,1,width,height});
    if(!(flags&2)){
        // Alternate original modes reuse/clear the guest scratch grid or read
        //passability bytes. Use the already recovered pure routine, preserving
        //those scratch side effects without duplicating its three algorithms.
        RecoveredBattle original(m);
        original.invoke(routines::flood_move_cost,{grid,unsigned(area.left),unsigned(area.top),unsigned(area.right),unsigned(area.bottom),unsigned(sx),unsigned(sy),flags});
        return;
    }
    // Zero padding defines scratch cells outside the loaded MAP rectangle.
    // Authored entrances can place an actor just beyond its visible boundary.
    std::array<std::uint32_t,capacity::map_cells> edges{},blocked{};
    const auto cell=[&](int x,int y){const auto index=std::int64_t(y)*width+x;if(index<0||index>=capacity::map_cells)throw Fault(routines::flood_move_cost,"path cell exceeds original fixed grid");return unsigned(index);};
    for(int i=0;i<width*height;++i){
        m.write(globals::move_cost_grid+i*4,0xffffffffu);const auto attr=m.read(globals::tile_attributes+(grid*capacity::map_cells+i)*4);
        edges[i]=(attr>>5)&15;blocked[i]=(attr>>9)&1;
        if(flags&0x80)blocked[i]|=unsigned((attr&0x80000)!=0);
        if(flags&0x100)blocked[i]|=(attr>>12)&1;
    }
    const auto dynamic=[&](Address begin,Address end,unsigned value,bool neutral){
        for(Address p=begin;p<end;p+=layout::actor_size){
            if(!(m.read(p-24,1)&64)||signed32(m.read(p))/units::q16_one!=signed32(m.read(globals::path_grid_id)))continue;
            if(neutral&&m.read(tables::actor_callback_kinds+m.read(p+0x100)*68))continue;
            const auto x=signed32(m.read(p+0x10c)),y=signed32(m.read(p+0x110));
            if((flags&0x200)&&x==sx&&y==sy)continue;
            blocked[cell(x,y)]=value;
        }
    };
    if(flags&0x10)dynamic(0x8073f4,0x8084ac,2,false);
    if(flags&0x20)dynamic(0x8084ac,0x80d844,3,true);
    if(flags&0x40)dynamic(0x80d844,0x810a6c,4,false);
    m.write(globals::path_frontier_x,std::uint32_t(sx));m.write(globals::path_frontier_y,std::uint32_t(sy));m.write(globals::path_frontier_counts,1);
    m.write(globals::move_cost_grid+cell(sx,sy)*4,0);
    unsigned cost=1;
    for(;;++cost){
        const auto current=cost&1,previous=(cost-1)&1;
        m.write(globals::path_frontier_counts+current*4,0);const auto previous_count=m.read(globals::path_frontier_counts+previous*4);
        if(previous_count>100)throw Fault(routines::flood_move_cost,"path frontier exceeds original capacity");
        for(unsigned i=0;i<previous_count;++i){
            const auto at=globals::path_frontier_x+(previous*100+i)*8;const int x=signed32(m.read(at)),y=signed32(m.read(at+4));
            const auto edge=edges[cell(x,y)];
            const auto visit=[&](int nx,int ny,unsigned mask,bool inside){
                if(!inside)return;const auto next=cell(nx,ny);
                if(m.read(globals::move_cost_grid+next*4)!=0xffffffffu||blocked[next]||(edge&mask))return;
                const auto count=m.read(globals::path_frontier_counts+current*4);if(count>=100)throw Fault(routines::flood_move_cost,"path frontier exceeds original capacity");
                m.write(globals::path_frontier_x+(current*100+count)*8,std::uint32_t(nx));m.write(globals::path_frontier_y+(current*100+count)*8,std::uint32_t(ny));
                m.write(globals::move_cost_grid+next*4,cost);m.write(globals::path_frontier_counts+current*4,count+1);
            };
            visit(x,y-1,1,area.top<y);visit(x,y+1,2,y<area.bottom);visit(x-1,y,4,area.left<x);visit(x+1,y,8,x<area.right);
        }
        if(!previous_count)break;
        if(cost>capacity::map_cells)throw Fault(routines::flood_move_cost,"path flood watchdog");
    }
    m.write(globals::move_cost_grid_valid,1);
}
}
void Actors::flood_costs(unsigned grid,int x,int y,unsigned flags,Rect bounds){
    memory_.write(globals::path_grid_id,grid);flood(memory_,grid,x,y,flags,bounds);
}
unsigned Actors::trace_path(Address actor,int tx,int ty,unsigned facing,unsigned flags,unsigned max_steps){
    const int width=signed32(memory_.read(globals::grid_row_stride)),sx=signed32(memory_.read(actor+actor_offset::world_x))/units::tile_width_q16,sy=signed32(memory_.read(actor+actor_offset::world_y))/units::tile_height_q16;
    const auto grid=std::uint32_t(signed32(memory_.read(actor+actor_offset::layer_q16))/units::q16_one),origin_facing=memory_.read(actor+actor_offset::facing);
    if(facing>=8||origin_facing>=8||max_steps>100)throw Fault(routines::trace_move_path,"invalid path facing/capacity");
    const auto index=[&](int x,int y){const auto n=std::int64_t(y)*width+x;if(n<0||n>=capacity::map_cells)throw Fault(routines::trace_move_path,"path cell exceeds original grid");return unsigned(n);};
    if((flags&1)||!memory_.read(globals::move_cost_grid_valid,1)||memory_.read(globals::move_cost_grid+index(sx,sy)*4)){
        memory_.write(globals::path_target_y,std::uint32_t(ty));memory_.write(globals::path_target_x,std::uint32_t(tx));memory_.write(globals::path_grid_id,grid);flood(memory_,grid,sx,sy,flags);
    }
    int cost=signed32(memory_.read(globals::move_cost_grid+index(tx,ty)*4));if(cost<0)return 0;
    const bool clipped=unsigned(cost)>max_steps;memory_.write(actor+actor_offset::path_cost,std::min(unsigned(cost),max_steps));
    const auto inverse=[&](unsigned dir){return memory_.read(tables::opposite_directions+dir*4);};
    auto direction=inverse(facing);const auto original_direction=inverse(origin_facing);int x=tx,y=ty;
    std::vector<std::uint16_t> reverse;
    while(x!=sx||y!=sy||direction!=original_direction){
        if(reverse.size()>=100)throw Fault(routines::trace_move_path,"path command list exceeds original capacity");
        unsigned next_direction=original_direction;
        if(cost>0){
            const auto available=[&](int nx,int ny,unsigned mask){const auto cell=index(nx,ny);return signed32(memory_.read(globals::move_cost_grid+cell*4))==cost-1&&!(memory_.read(globals::tile_attributes+(grid*capacity::map_cells+cell)*4)&mask);};
            if(available(x,y-1,0x40))next_direction=0;
            else if(available(x,y+1,0x20))next_direction=1;
            else if(available(x-1,y,0x100))next_direction=2;
            else if(available(x+1,y,0x80))next_direction=3;
            else throw Fault(routines::trace_move_path,"path backtrace has no predecessor");
        }
        if(next_direction==direction){
            reverse.push_back(std::uint16_t(inverse(next_direction)|((flags&0x400)?0x8100:0x8000)));
            if(next_direction==0)--y;else if(next_direction==1)++y;else if(next_direction==2)--x;else if(next_direction==3)++x;else throw Fault(routines::trace_move_path,"non-cardinal path move");
            --cost;
        }else{
            reverse.push_back(std::uint16_t(inverse(direction)|((inverse(next_direction)|0x10)<<4)));direction=next_direction;
        }
    }
    memory_.write(actor+actor_offset::path_count,0);unsigned moves=0,count=0;
    for(auto p=reverse.rbegin();p!=reverse.rend();++p){if(*p&0x8000)++moves;if(moves>max_steps)break;memory_.write(actor+actor_offset::path_commands+count*2,*p,2);memory_.write(actor+actor_offset::path_count,++count);}
    return clipped?2:1;
}
Handle Actors::follow_path(std::uint32_t id,int x,int y,unsigned facing,unsigned variant,bool block_party,bool block_objects,bool wait_mode){
    const auto actor=lookup_actor(memory_,id);if(!actor)throw Fault(routines::spawn_path_followup,"path needs a materialized actor");
    memory_.write(actor+actor_offset::path_cursor,0);memory_.write(actor+actor_offset::path_count,0);
    if(!trace_path(actor,x,y,facing,0x183|(block_party?0x10:0)|(block_objects?0x20:0))){
        set_actor_tile_position(memory_,actor,x,y,-1);memory_.write(actor+actor_offset::facing,facing);clear_frame(actor);return 0;
    }
    if(variant>1)throw Fault(routines::spawn_path_followup,"invalid path variant");
    const auto handle=Arena(memory_).clone_event(variant?scripts::alternate_path_followup:scripts::path_followup,1),child=*resolve_compact(memory_,handle);
    memory_.write(child+vm_offset::actor_id,id);memory_.write(child+vm_offset::actor_object,actor);memory_.write(child+0x19c,!wait_mode);return handle;
}
Handle Actors::start_walk(std::uint32_t id,std::uint32_t direction,std::uint32_t count,unsigned variant,std::uint32_t extra){
    if(variant>1)throw Fault(routines::start_walk_child,"invalid walk variant");
    const auto actor=lookup_actor(memory_,id);if(!actor)throw Fault(routines::start_walk_child,"walk needs a materialized actor");
    const auto handle=Arena(memory_).clone_event(variant?scripts::alternate_walk:scripts::walk,1),child=*resolve_compact(memory_,handle);
    memory_.write(child+vm_offset::actor_id,id);memory_.write(child+vm_offset::actor_object,actor);memory_.write(child+vm_offset::frame_bias,extra);memory_.write(child+0xe4,direction);memory_.write(child+0x2c,count);return handle;
}
std::uint32_t Actors::step_attribute(Address actor,unsigned direction)const{
    if(direction>3)return 0;
    const int x=signed32(memory_.read(actor+actor_offset::world_x))/units::tile_width_q16,y=signed32(memory_.read(actor+actor_offset::world_y))/units::tile_height_q16,z=signed32(memory_.read(actor+actor_offset::layer_q16))/units::q16_one;
    const int width=signed32(memory_.read(globals::grid_row_stride)),height=signed32(memory_.read(globals::grid_height));
    if((direction==0&&y<2)||(direction==1&&y>=height-1)||(direction==2&&x<1)||(direction==3&&x>=width-1))return 0;
    const auto cell=[&](int cx,int cy){return std::uint32_t(cy)*std::uint32_t(width)+std::uint32_t(cx);};
    const auto attr=[&](int cx,int cy){return memory_.read(globals::tile_attributes+(std::uint32_t(z)*capacity::map_cells+cell(cx,cy))*4);};
    const auto raw=[&](int cx,int cy){return memory_.read(globals::map_raw_attributes+std::uint32_t(z)*0x8028+cell(cx,cy)*4);};
    const auto occupied=[&](int cx,int cy){return (memory_.read(globals::tile_occupancy+(std::uint32_t(z)*capacity::map_cells+cell(cx,cy))*4+2,1)&1)!=0;};
    const auto opposite=[&](unsigned dir){return memory_.read(tables::opposite_directions+dir*4);};
    const int dx=direction==2?-1:direction==3?1:0,dy=direction==0?-1:direction==1?1:0;
    const auto center=attr(x,y),neighbor=attr(x+dx,y+dy);
    if((center&0x20000)&&(!(center&0x400)||(center&0x1c000000)==(direction<<26))&&(neighbor&0x200)&&!(attr(x+dx*2,y+dy*2)&0x200))return (direction<<8)|2;
    if(direction<2){
        std::uint32_t result=(direction<<8)|((neighbor&0x180000)==0x80000?16:0);
        const auto height_flags=[&](unsigned value){const auto dir=(value>>26)&7;return dir==direction?4u:opposite(dir)==direction?8u:0u;};
        if(neighbor&0x200){if(center&height_connector_flag)result|=height_flags(center);return result|((result&12)?1:0);}
        if((center&0x40000)||(center&(0x20u<<direction))||occupied(x+dx,y+dy))return result;
        result|=1;if(neighbor&height_connector_flag)result|=height_flags(neighbor);return result;
    }
    const auto current=raw(x,y);int target_y=y;std::uint32_t result=0;
    if(direction==2){
        if(current>=0x33&&current<=0x36){target_y=y+1;result=0x500;}
        else if(current>=0x38&&current<=0x3b){target_y=y-1;result=0x600;}
    }else{
        if(current>=0x32&&current<=0x35){target_y=y-1;result=0x400;}
        else if(current>=0x37&&current<=0x3a){target_y=y+1;result=0x700;}
    }
    const auto target=attr(x+dx,target_y),target_raw=raw(x+dx,target_y);
    if(target_y!=y){if(target&0x200)result|=target_y<y?4:8;result|=1;}
    else if(!(target&0x200)&&!(center&(0x20u<<direction))&&!occupied(x+dx,y))result=(direction<<8)|1;
    if((target&0x180000)==0x80000)result|=16;
    if(target_raw==(direction==2?0x39u:0x34u))result|=4;
    if(target_raw==(direction==2?0x34u:0x39u))result|=8;
    return result;
}
} // namespace fsb::core
