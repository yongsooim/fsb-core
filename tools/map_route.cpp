#include "lab_io.hpp"
#include <algorithm>
#include "fsb_core/actors.hpp"
#include "field_step.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
#include <set>
#include <sstream>

using namespace fsb::core;
namespace {
struct Position{int x,y,z;};
struct Edge{unsigned map,entry;Position exit;};
struct Node{unsigned map,entry;Position position;std::vector<std::pair<unsigned,Edge>> route;};
// Only a planning copy is touched. The resulting route must be executed by
// ordinary input; callbacks, NPC guards and puzzles can still reject a route.
bool reachable(Memory& m,Position start,Position goal){
    const int width=int(m.read(globals::grid_row_stride)),height=int(m.read(globals::grid_height)),layers=std::min(2u,m.read(globals::active_map_layer_count));
    const auto valid=[&](Position p){return p.x>=0&&p.y>=0&&p.x<width&&p.y<height&&p.z>=0&&p.z<layers;};
    if(!valid(start)||!valid(goal))return false;
    const auto index=[&](Position p){return p.z*4096+p.y*width+p.x;};
    const auto goal_cell=globals::tile_attributes+unsigned(index(goal))*4,saved=m.read(goal_cell);
    m.write(goal_cell,saved&~0x80200u);
    const auto actor=Actors::slot(0);Actors steps(m);std::vector<Position> queue{start};std::vector<bool> seen(layers*4096);seen[index(start)]=true;bool found=false;
    constexpr int dx[]={0,0,-1,1,1,-1,-1,1},dy[]={-1,1,0,0,-1,1,-1,1};
    for(std::size_t i=0;i<queue.size()&&!found;++i){const auto at=queue[i];if(at.x==goal.x&&at.y==goal.y&&at.z==goal.z){found=true;break;}
        set_actor_tile_position(m,actor,at.x,at.y,at.z);
        const auto terrain=m.read(globals::tile_attributes+unsigned(index(at))*4);
        for(unsigned direction=0;direction<4;++direction){
            if((terrain&0x18000)==0x8000&&direction!=((terrain>>26)&7))continue;
            if(!valid({at.x+dx[direction],at.y+dy[direction],at.z}))continue;
            const auto result=fsb::lab::player_step_for_planning(m,steps,actor,direction);if(!(result&3))continue;
            const unsigned facing=(result>>8)&7,distance=(result&2)?2:1;
            const Position next{at.x+dx[facing]*int(distance),at.y+dy[facing]*int(distance),at.z+int(bool(result&4))-int(bool(result&8))};
            if(!valid(next)||seen[index(next)])continue;
            if((m.read(globals::tile_attributes+unsigned(index(next))*4)&0x80000)&&(next.x!=goal.x||next.y!=goal.y))continue;
            seen[index(next)]=true;queue.push_back(next);
        }
    }
    m.write(goal_cell,saved);return found;
}
}
int main(int argc,char** argv){
    try{
        if(argc!=4&&argc!=5){std::cerr<<"Usage: fsb_map_route ASSETS SNAPSHOT TARGET_MAP[:X,Y,Z] [PLAN_FROM_MAP_ENTRY0|actor:SLOT]\n";return 2;}
        const std::filesystem::path assets=argv[1];const auto initial=fsb::lab::read_guest_snapshot(argv[2]);
        if(std::string(argv[3])=="audit"){
            unsigned checked=0,failed=0;
            for(unsigned id=0;id<500;++id){const auto names=Map::asset_names(initial,id);bool present=true;for(const auto& name:names)present&=std::filesystem::is_regular_file(assets/"MAPSET"/name);if(!present)continue;
                try{MapAssets data;for(unsigned i=0;i<6;++i)data.files[i]=fsb::lab::read(assets/"MAPSET"/names[i]);Memory scratch=initial;Map map(scratch);map.load_assets(id,data);map.rebuild_animation_lookup();map.tick_tile_animation();++checked;}
                catch(const std::exception& error){++failed;std::cerr<<"map="<<id<<" "<<error.what()<<'\n';}
            }
            std::cout<<"parsed_maps="<<checked<<" failures="<<failed<<"; import and first animation tick, not gameplay completion\n";return failed?1:0;
        }
        const std::string target_arg=argv[3];const auto colon=target_arg.find(':');
        const auto target=unsigned(std::stoul(target_arg.substr(0,colon)));std::optional<Position> target_tile;
        if(colon!=std::string::npos){
            auto coordinates=target_arg.substr(colon+1);std::replace(coordinates.begin(),coordinates.end(),',',' ');
            std::istringstream input(coordinates);Position position{};std::string extra;
            if(!(input>>position.x>>position.y>>position.z)||(input>>extra))throw std::runtime_error("target tile requires X,Y,Z");
            target_tile=position;
        }
        const bool from_actor=argc==5&&std::string(argv[4]).starts_with("actor:");
        const bool future_map=argc==5&&!from_actor;
        const auto actor=Actors::slot(from_actor?unsigned(std::stoul(std::string(argv[4]).substr(6))):initial.read(globals::active_party_index));
        // Optional future-map planning starts at that map's arrival0;
        //actor:SLOT inspects the other party marker in the current map.
        //Both are private geometry queries, never live campaign relocations.
        const auto from=future_map?unsigned(std::stoul(argv[4])):initial.read(globals::current_map_id);
        std::vector<Node> queue{{from,future_map?0u:0xffffffffu,{signed32(initial.read(actor+0x128)),signed32(initial.read(actor+0x12c)),int(initial.read(actor+0x1c)/65536)}, {}}};
        std::set<std::pair<unsigned,unsigned>> visited{{queue[0].map,queue[0].entry}};std::map<unsigned,MapAssets> bundles;
        const auto emit=[&](const Node& node){
                std::cout<<"# Static terrain/MFO candidate; original callbacks and NPC gates still execute.\n";
                for(const auto& [map,e]:node.route){
                    std::cout<<"walk "<<map<<' '<<e.exit.x<<' '<<e.exit.y;
                    if(e.map<10)std::cout<<"\nuntil 0x80465c 0xffffffff 10";
                    else std::cout<<"\nuntil 0x5d229c 0xffffffff "<<e.map;
                    std::cout<<"\nwait 1000\n";
                }
                if(target_tile)std::cout<<"walk "<<target<<' '<<target_tile->x<<' '<<target_tile->y<<'\n';
                std::cerr<<"map_entry_states="<<visited.size()<<" transitions="<<node.route.size()<<'\n';
        };
        for(std::size_t cursor=0;cursor<queue.size();++cursor){
            auto node=queue[cursor];if(node.map==target&&!target_tile){emit(node);return 0;}
            if(!bundles.count(node.map)){
                MapAssets data;const auto names=Map::asset_names(initial,node.map);
                for(unsigned i=0;i<6;++i)data.files[i]=fsb::lab::read(assets/"MAPSET"/names[i]);bundles[node.map]=std::move(data);
            }
            Memory scratch=initial;Map map(scratch);
            // The live map may inherit a JMP row omitted by its MFO (TMR0),
            // or have script-modified terrain. Keep those observed bytes.
            // Future maps still use conservative explicit asset/patch edges.
            if(node.map!=initial.read(globals::current_map_id)){
            for(unsigned i=0;i<32;++i){for(unsigned j=0;j<6;++j)scratch.write(0x7ab9a0+i*24+j*4,0xffffffffu);for(unsigned j=0;j<8;++j)scratch.write(0x7ab5a0+i*32+j*4,0xffffffffu);}
            map.load_assets(node.map,bundles.at(node.map));
            for(unsigned i=0;i<0x199;++i)if(scratch.read(tables::map_patches+i*36)==node.map)map.apply_patch(i,(initial.read(globals::event_flag_bits+(i/32)*4)&(1u<<(i&31)))!=0);
            }
            for(unsigned i=0;i<8192;++i)scratch.write(globals::tile_occupancy+i*4,0);
            if(node.entry!=0xffffffffu){const auto p=0x7ab9a0+node.entry*24;node.position={signed32(scratch.read(p)),signed32(scratch.read(p+4)),signed32(scratch.read(p+8))};}
            if(node.map==target&&target_tile&&reachable(scratch,node.position,*target_tile)){emit(node);return 0;}
            const auto width=scratch.read(globals::grid_row_stride),height=scratch.read(globals::grid_height),layers=std::min(2u,scratch.read(globals::active_map_layer_count));
            // pcpos is arrival metadata. 458ed7 chooses outgoing JMP slots
            // from actual tile attributes; TGP5 pcpos1.link even says0 while
            // its exit at16,3 selects JMP1. Do not "repair" the original MFO.
            for(unsigned z=0;z<layers;++z)for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
                const auto attr=scratch.read(globals::tile_attributes+(z*4096+y*width+x)*4);if((attr&0x180000)!=0x80000)continue;
                const auto j=0x7ab5a0+((attr>>21)&31)*32;
                Edge edge{scratch.read(j),scratch.read(j+4),{int(x),int(y),int(z)}};
                // Regional world maps can be terminal destinations. Their
                // navigation belongs to the world-input fixture, not this BFS.
                if((edge.map<10&&edge.map!=target)||edge.map>=500||edge.entry>=32||scratch.read(j+8)||visited.count({edge.map,edge.entry}))continue;
                if(!reachable(scratch,node.position,edge.exit))continue;
                visited.emplace(edge.map,edge.entry);auto route=node.route;route.emplace_back(node.map,edge);queue.push_back({edge.map,edge.entry,{},std::move(route)});
            }
        }
        std::cerr<<"No static MFO/terrain route; inspect puzzle or callback gates. Visited:";for(const auto& node:queue)std::cerr<<' '<<node.map<<'/'<<node.entry;std::cerr<<'\n';return 1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
