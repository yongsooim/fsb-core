#include "runtime_assets.hpp"
#include "field_input_fixture.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){try{
    const bool walk=argc==4&&std::string(argv[3])=="--walk";
    if(!walk&&(argc<6||argc>7))return 2;const std::filesystem::path assets=argv[1],out=argv[2];std::filesystem::create_directories(out);
    Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,16);runtime.start_field_fixture(22,16);auto& m=runtime.memory;unsigned now=0;
    std::ofstream inputs(out/"inputs.tsv");inputs<<"ms kind key flags shift control\n";
    const auto advance=[&](unsigned count){for(unsigned i=0;i<count;++i)runtime.advance(now++,false);};
    const auto post=[&](InputMessage e){runtime.post_input(e);inputs<<now<<' '<<e.kind<<' '<<e.key<<' '<<e.flags<<' '<<e.shift<<' '<<e.control<<'\n';};
    const auto actor=Actors::slot(0);
    const auto capture=[&](const std::filesystem::path& directory,bool check_visibility,bool hidden){
        std::filesystem::create_directories(directory);fsb::lab::guest_snapshot(m,directory/"state.bin");fsb::lab::bmp(runtime.frame(),directory/"current.bmp");
        std::ofstream queue(directory/"queue.tsv");queue<<"ordinal pointer owner type layer depth actor x y tile sheet flags\n";
        Address hero=0;unsigned ordinal=0;
        for(unsigned i=0;i<m.read(globals::draw_queue_count);++i){const auto c=m.read(globals::draw_queue+i*4);queue<<i<<' '<<std::hex<<c;for(auto off:{0u,4u,8u,12u,16u,20u,24u,64u,60u,68u})queue<<' '<<m.read(c+off);queue<<std::dec<<'\n';if(m.read(c+4)==10&&m.read(c+16)==actor){hero=c;ordinal=i;}}
        if(!check_visibility)return;
        if(!hero)throw std::runtime_error("player has no draw record");
        struct Tile{const Image8* image;Rect rect;int x,y;bool key;};std::vector<Tile> tiles;
        for(unsigned i=ordinal+1;i<m.read(globals::draw_queue_count);++i){const auto c=m.read(globals::draw_queue+i*4),flags=m.read(c+68);if(m.read(c)!=3||m.read(c+4)!=3||flags==0xffffffffu)continue;const auto sprite=runtime.sprites.sheet(m.read(c+60),m.read(c+64));tiles.push_back({&runtime.surfaces.get(sprite.surface),sprite.rect,signed32(m.read(c+20)),signed32(m.read(c+24)),flags!=0});}
        const auto& sprite=runtime.surfaces.get(m.read(hero+80));const int sx=signed32(m.read(hero+28)),sy=signed32(m.read(hero+32)),w=signed32(m.read(hero+36))-sx,h=signed32(m.read(hero+40))-sy,dx=signed32(m.read(hero+20)),dy=signed32(m.read(hero+24));
        unsigned opaque=0,covered=0,wrong=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int px=dx+x,py=dy+y;if(px<0||py<0||px>=640||py>=480||!sprite.pixels[std::size_t(sy+y)*sprite.width+sx+x])continue;++opaque;std::optional<std::uint8_t> foreground;
            for(const auto& t:tiles){const int tx=px-t.x+t.rect.left,ty=py-t.y+t.rect.top;if(tx<t.rect.left||tx>=t.rect.right||ty<t.rect.top||ty>=t.rect.bottom)continue;const auto color=t.image->pixels[std::size_t(ty)*t.image->width+tx];if(color||!t.key)foreground=color;}
            if(foreground){++covered;if(runtime.frame().pixels[std::size_t(py)*640+px]!=*foreground)++wrong;}
        }
        if(!opaque||wrong||(hidden?covered*10<opaque*9:covered!=0))throw std::runtime_error("tree visibility failed: opaque="+std::to_string(opaque)+" covered="+std::to_string(covered)+" wrong="+std::to_string(wrong));
        std::ofstream(directory/"visibility.json")<<"{\"logical_ms\":"<<now<<",\"player_pixels\":"<<opaque<<",\"covered_by_later_foreground\":"<<covered<<",\"foreground_pixel_mismatches\":"<<wrong<<",\"expected_hidden\":"<<(hidden?"true":"false")<<"}\n";
    };
    advance(1000);
    if(walk){
        // Source bank guard: original MAPSET.dll Order=11 on this canopy.
        // MAPSET2.dll stores zero here and must not silently become an input.
        if((m.read(globals::tile_attributes+(35*40+9)*4)&31)!=11)throw std::runtime_error("wrong map bank: village canopy Order is not11");
        // Return to the clear entry path; tile(9,38) is partly covered by the fence.
        for(const auto y:{35,38}){fsb::lab::FieldWalkFixture route;route.map=22;route.x=y==35?9:13;route.y=y;
            while(!route.settled()||m.read(actor+actor_offset::motion_state)){if(now>90000)throw std::runtime_error("tree walk timed out");if(const auto e=route.next(runtime,now))post(*e);advance(1);}advance(300);capture(out/(y==35?"behind":"front"),true,y==35);}
        std::ofstream(out/"result.json")<<"{\"map\":22,\"walk_only\":true,\"behind_hidden\":true,\"front_visible\":true,\"logical_ms\":"<<now<<"}\n";
    }else{
        const int x=std::stoi(argv[3]),y=std::stoi(argv[4]),z=std::stoi(argv[5]);set_actor_tile_position(m,actor,x,y,z);m.write(actor+actor_offset::motion_state,0);m.write(actor+actor_offset::motion_frame,0);advance(50);capture(out,false,false);
        if(argc==7){const auto data=fsb::lab::read(argv[6]);for(unsigned i=0;i<data.size();++i)m.write(globals::draw_queue_count+i,data[i],1);runtime.surfaces.clear(m.read(globals::render_target_surface),{0,0,640,480});runtime.draw.execute();auto frame=runtime.surfaces.get(m.read(globals::render_target_surface));frame.palette=runtime.palette.colors();fsb::lab::bmp(frame,out/"original-queue.bmp");}
    }
    std::cout<<"draw-order probe passed; ms="<<now<<" queue="<<m.read(globals::draw_queue_count)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
