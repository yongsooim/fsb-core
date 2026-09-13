#pragma once
#include <algorithm>
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include "field_step.hpp"
#include <limits>

namespace fsb::lab {
class WorldInputFixture {
public:
    unsigned destination=0xffffffffu;
    std::optional<core::InputMessage> next(core::Runtime& runtime,unsigned now){
        using namespace core;const auto& m=runtime.memory;
        if(key_&&now>=release_){const auto key=key_;key_=0;ready_=now+400;return keyboard_message(key,scan(key),false);}
        if(destination==0xffffffffu||key_||now<ready_||m.read(globals::game_mode)!=10)return std::nullopt;
        unsigned key=13;
        if(m.read(0x7714a4)==2&&!m.read(globals::live_dialogue_count)){
            const auto current=m.read(0x770b68);if(current>=44||destination>=44)throw Fault(destination,"world fixture city outside table");
            if(current!=destination){
                std::vector<int> first(44,-1);std::vector<unsigned> queue{current};first[current]=4;
                for(std::size_t cursor=0;cursor<queue.size()&&first[destination]<0;++cursor)for(unsigned direction=0;direction<4;++direction){
                    const auto neighbor=m.read(0x5b10a0+queue[cursor]*4+direction,1);if(neighbor>=44||first[neighbor]>=0||(m.read(0x5aff98+neighbor*84)&3)!=3)continue;
                    first[neighbor]=cursor?first[queue[cursor]]:int(direction);queue.push_back(neighbor);
                }
                if(first[destination]<0)throw Fault(destination,"world fixture destination is not reachable/selectable");
                //435740 consumes the same four input globals as field motion.
                constexpr unsigned keys[]={38,40,37,39};key=keys[first[destination]];
            }
        }else if(!m.read(globals::live_dialogue_count))return std::nullopt;
        key_=key;release_=now+48;return keyboard_message(key,scan(key),true);
    }
private:
    unsigned key_=0,release_=0,ready_=0;
    static unsigned scan(unsigned key){switch(key){case 38:return 0xc8;case 40:return 0xd0;case 37:return 0xcb;case 39:return 0xcd;default:return 0x1c;}}
};
class FieldInputFixture {
public:
    unsigned template_id=0xffffffffu;
    std::vector<unsigned> actor_sequence;
    bool completed()const{return completed_;}
    std::optional<core::InputMessage> next(core::Runtime& runtime,unsigned now){
        using namespace core;auto& m=runtime.memory;
        if(key_&&now>=release_){const auto key=key_;key_=0;ready_=now+64;return keyboard_message(key,scan(key),false);}
        if(template_id==0xffffffffu||completed_||key_||now<ready_)return std::nullopt;
        if(m.read(globals::game_mode)!=3)return std::nullopt;
        if(m.read(0x768940)||m.read(globals::live_dialogue_count)){
            if(m.read(0x768940)&&m.read(0x768958)!=template_id)throw Fault(template_id,"approach fixture spoke to a different actor");
            saw_dialog_=true;if(now>=confirm_){confirm_=now+600;return press(13,now);}return std::nullopt;
        }
        if(saw_dialog_){if(sequence_index_+1<actor_sequence.size()){template_id=actor_sequence[++sequence_index_];saw_dialog_=false;ready_=now+150;}else completed_=true;return std::nullopt;}
        if(m.read(globals::current_event_id)!=0xffffffffu)return std::nullopt;
        const auto actor=Actors::slot(m.read(globals::active_party_index)),target=lookup_actor(m,template_id);if(!target)throw Fault(template_id,"field fixture actor absent from current map");
        if(m.read(actor+0x104))return std::nullopt;
        const int x=signed32(m.read(actor+0x128)),y=signed32(m.read(actor+0x12c)),tx=signed32(m.read(target+0x128)),ty=signed32(m.read(target+0x12c));
        constexpr unsigned keys[]={38,40,37,39};constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};
        const auto grid=m.read(actor+0x1c)/65536;const int width=signed32(m.read(globals::grid_row_stride)),height=signed32(m.read(globals::grid_height));
        const auto clear_ray=[&](int cx,int cy,unsigned facing,unsigned distance){for(unsigned i=0;i<distance;++i){const auto cell=grid*4096+unsigned((cy+dy[facing]*int(i))*width+cx+dx[facing]*int(i));if(m.read(globals::tile_attributes+cell*4)&(0x20u<<facing))return false;if(i&&(m.read(globals::tile_occupancy+cell*4)&0x10000))return false;}return true;};
        const int distance=std::abs(tx-x)+std::abs(ty-y);
        if(distance>0&&distance<=2&&(x==tx||y==ty)){const unsigned facing=tx<x?2:tx>x?3:ty<y?0:1;if(clear_ray(x,y,facing,unsigned(distance)))return press(m.read(actor+0x110)==facing?13:keys[facing],now);}
        // Reuse the already-verified path routine on an isolated memory copy.
        // Only the resulting ordinary key is applied to the live engine.
        Memory scratch=m;Actors paths(scratch);
        paths.flood_costs(grid,x,y,0x2f3,{1,1,width-1,height-1});int cost=std::numeric_limits<int>::max(),gx=0,gy=0;unsigned face=0;
        for(unsigned distance=1;distance<=2;++distance)for(unsigned i=0;i<4;++i){const int cx=tx+dx[i]*int(distance),cy=ty+dy[i]*int(distance);const auto facing=m.read(tables::opposite_directions+i*4);if(cx<1||cy<1||cx>=width||cy>=height||!clear_ray(cx,cy,facing,distance))continue;const auto occupied=m.read(globals::tile_occupancy+(grid*4096+unsigned(cy*width+cx))*4);if((occupied&0x10000)&&(occupied&65535)!=m.read(actor))continue;
            const int value=signed32(scratch.read(globals::move_cost_grid+unsigned(cy*width+cx)*4));if(value>=0&&value<cost){cost=value;gx=cx;gy=cy;face=facing;}}
        if(cost==std::numeric_limits<int>::max())throw Fault(template_id,"field fixture has no route to actor");
        if(!paths.trace_path(actor,gx,gy,face,0x2f2))throw Fault(template_id,"field fixture path disappeared");
        for(unsigned i=0;i<scratch.read(actor+actor_offset::path_count);++i){const auto step=scratch.read(actor+actor_offset::path_commands+i*2,2);if(step&0x8000){const auto direction=step&15;if(direction>=4)throw Fault(template_id,"non-cardinal fixture path");return press(keys[direction],now);}}
        return std::nullopt;
    }
private:
    unsigned key_=0,release_=0,ready_=100,confirm_=0,sequence_index_=0;bool saw_dialog_=false,completed_=false;
    static unsigned scan(unsigned key){switch(key){case 38:return 0xc8;case 40:return 0xd0;case 37:return 0xcb;case 39:return 0xcd;default:return 0x1c;}}
    core::InputMessage press(unsigned key,unsigned now){key_=key;release_=now+48;return core::keyboard_message(key,scan(key),true);}
};
class FieldWalkFixture {
public:
    unsigned map=0xffffffffu,start_ms=0;int x=0,y=0;bool done=false;
    bool settled()const{return done&&!key_;}
    std::optional<core::InputMessage> next(core::Runtime& runtime,unsigned now){
        using namespace core;auto& m=runtime.memory;
        if(key_&&now>=release_){const auto key=key_;key_=0;ready_=now+64;return keyboard_message(key,scan(key),false);}
        if(done||key_||now<ready_||now<start_ms||map==0xffffffffu)return std::nullopt;
        if(started_&&(m.read(globals::game_mode)==10||(m.read(globals::game_mode)==3&&m.read(globals::current_map_id)!=map))){done=true;return std::nullopt;}
        if(m.read(globals::game_mode)!=3||m.read(globals::current_map_id)!=map)return std::nullopt;started_=true;
        const auto actor=Actors::slot(m.read(globals::active_party_index));
        if(signed32(m.read(actor+0x128))==x&&signed32(m.read(actor+0x12c))==y){done=true;return std::nullopt;}
        if(m.read(globals::current_event_id)!=0xffffffffu||m.read(globals::live_dialogue_count)||m.read(0x803a4c)||runtime.palette.busy()||m.read(actor+0x104)||!(m.read(actor+4)&0x80))return std::nullopt;
        Memory scratch=m;const int width=signed32(m.read(globals::grid_row_stride)),height=signed32(m.read(globals::grid_height));if(x<0||y<0||x>=width||y>=height)throw Fault(map,"walk fixture goal outside map");
        // The cutscene flood intentionally ignores some NPC callback kinds.
        // A player cannot walk through them. Use the original player step-query
        // graph on a private copy, including occupancy, stairs and jump tiles.
        const int sx=signed32(m.read(actor+0x128)),sy=signed32(m.read(actor+0x12c)),sz=int(m.read(actor+0x1c)/65536);
        const auto layers=m.read(globals::active_map_layer_count);const auto index=[&](int px,int py,int pz){return pz*4096+py*width+px;};
        const auto goal_attributes=m.read(globals::tile_attributes+unsigned(index(x,y,sz))*4);
        const auto is_goal=[&](int px,int py,int pz){
            if(px==x&&py==y)return true;
            // 458ed7 resolves (attr>>21)&31. Multiple adjacent tiles may be
            // the same exit; don't demand the authored pcpos center when a
            // wandering NPC temporarily blocks it (TBS0_00 south exit).
            const auto attr=m.read(globals::tile_attributes+unsigned(index(px,py,pz))*4);
            return (goal_attributes&0x180000)==0x80000&&(attr&0x180000)==0x80000&&((attr^goal_attributes)&0x3e00000)==0;
        };
        for(unsigned layer=0;layer<layers;++layer){const auto at=globals::tile_attributes+(layer*4096+unsigned(y*width+x))*4;scratch.write(at,scratch.read(at)&~0x80200u);}
        std::vector<Rect> falling_floors;
        for(Address overlay=globals::overlay_effects;overlay<globals::overlay_effects_end;overlay+=48)
            if((m.read(overlay)&0x1000000)&&m.read(overlay+20)==0x493a26)
                falling_floors.push_back({signed32(m.read(overlay+4)),signed32(m.read(overlay+8)),signed32(m.read(overlay+12)),signed32(m.read(overlay+16))});
        scratch.write(globals::tile_occupancy+unsigned(index(sx,sy,sz))*4,0);
        std::vector<int> first(layers*4096,-1),queue{index(sx,sy,sz)};first[queue.front()]=4;Actors steps(scratch);int selected=-1;
        constexpr int dx[]={0,0,-1,1,1,-1,-1,1},dy[]={-1,1,0,0,-1,1,-1,1};
        for(std::size_t cursor=0;cursor<queue.size()&&selected<0;++cursor){
            const int node=queue[cursor],pz=node/4096,px=(node%4096)%width,py=(node%4096)/width;
            set_actor_tile_position(scratch,actor,px,py,pz);
            const auto terrain=scratch.read(globals::tile_attributes+unsigned(node)*4);
            for(unsigned direction=0;direction<4;++direction){
                //458ed7 processes forced auto-step terrain before keyboard
                //input. A path cannot walk against the arrow (ice valley386).
                if((terrain&0x18000)==0x8000&&direction!=((terrain>>26)&7))continue;
                if(px+dx[direction]<0||px+dx[direction]>=width||py+dy[direction]<0||py+dy[direction]>=height)continue;
                const auto result=player_step_for_planning(scratch,steps,actor,direction);if(!(result&3))continue;const unsigned facing=(result>>8)&7,distance=(result&2)?2:1;
                const int nx=px+dx[facing]*int(distance),ny=py+dy[facing]*int(distance),nz=pz+int(bool(result&4))-int(bool(result&8));
                if(nx<0||ny<0||nx>=width||ny>=height||nz<0||nz>=int(layers))continue;
                // A known collapsing floor returns the actor to the previous
                // storey. Avoid it en route; an explicit trap goal may still
                // exercise it. The live tile/overlay state is never changed.
                if(!is_goal(nx,ny,nz)&&std::any_of(falling_floors.begin(),falling_floors.end(),[&](const Rect& r){return nx>=r.left&&nx<=r.right&&ny>=r.top&&ny<=r.bottom;}))continue;
                const auto target=index(nx,ny,nz);if(first[target]>=0)continue;
                if((scratch.read(globals::tile_attributes+unsigned(target)*4)&0x80000)&&!is_goal(nx,ny,nz))continue;
                first[target]=cursor?first[node]:int(direction);
                if(is_goal(nx,ny,nz)){selected=first[target];break;}queue.push_back(target);
            }
        }
        if(selected<0){
            // Roaming NPCs can briefly occupy a one-tile doorway. Let normal
            // actor ticks clear it and replan; never clear live occupancy.
            if(!blocked_since_)blocked_since_=now;
            if(now-blocked_since_>=10000)throw Fault(map,"walk fixture cannot reach goal on either map layer");
            ready_=now+250;return std::nullopt;
        }
        blocked_since_=0;
        if(sx+dx[selected]==x&&sy+dy[selected]==y&&!(runtime.actors.step_attribute(actor,unsigned(selected))&3)){
            for(Address overlay=globals::overlay_effects;overlay<globals::overlay_effects_end;overlay+=48){
                const auto flags=m.read(overlay);
                if((flags&0x3000000)!=0x1000000||!(flags&0x100)||m.read(overlay+20)!=0x486dfb)continue;
                const auto opposite=m.read(tables::opposite_directions+unsigned(selected)*4);if(!(flags&m.read(0x5d0048+opposite,1)&15))continue;
                if(x<signed32(m.read(overlay+4))||x>signed32(m.read(overlay+12))||y<signed32(m.read(overlay+8))||y>signed32(m.read(overlay+16)))continue;
                if(m.read(actor+actor_offset::facing)==unsigned(selected)){key_=13;release_=now+48;return keyboard_message(13,0x1c,true);}
            }
        }
        constexpr unsigned keys[]={38,40,37,39};key_=keys[selected];
        // 486c6f opens a door after >7 consecutive blocked approach frames.
        // Short, separated taps reset that original dwell counter.
        const auto next_cell=unsigned(sz)*4096+unsigned((sy+dy[selected])*width+sx+dx[selected]);
        release_=now+((m.read(globals::tile_attributes+next_cell*4)&0x200)?240:48);
        return keyboard_message(key_,scan(key_),true);
        return std::nullopt;
    }
private:
    unsigned key_=0,release_=0,ready_=0,blocked_since_=0;bool started_=false;
    static unsigned scan(unsigned key){switch(key){case 13:return 0x1c;case 38:return 0xc8;case 40:return 0xd0;case 37:return 0xcb;default:return 0xcd;}}
};
} // namespace fsb::lab
