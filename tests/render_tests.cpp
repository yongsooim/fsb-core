#include "fsb_core/draw_queue.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/audio.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/viewport.hpp"
#include "fsb_core/object_pump.hpp"
#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "../tools/sprite_assets.hpp"
#include <iostream>

using namespace fsb::core;
namespace {
unsigned checks=0,failed=0;
std::uint64_t hash=14695981039346656037ull;
void mix(std::uint32_t v){for(unsigned i=0;i<4;++i)hash=(hash^std::uint8_t(v>>(i*8)))*1099511628211ull;}
void check(bool v,const char* name){++checks;if(!v){++failed;std::cerr<<"FAIL "<<name<<'\n';}}
void dump_draw_state(const Memory& memory,const std::filesystem::path& path){
    const std::pair<Address,unsigned> ranges[]={{0x5cd3f0,48},{0x6e12b0,4},{0x6e1440,4},{0x6e1468,4},{0x74b470,4},
        {0x7760c4,8},{0x7764e0,0x14000},{0x7873c0,8},{0x787480,3224},{0x788118,4*428*84},
        {0x7cbca0,5*4096*4},{0x7d8ca0,8192*4},{0x7e0d08,2*0x8028},{0x800da8,28},{0x804d10,3*68},{0x8073d8,11*428}};
    std::ofstream file(path,std::ios::binary);file.write("FSBDRAW1",8);
    const auto u32=[&](std::uint32_t v){for(unsigned i=0;i<4;++i)file.put(char(v>>(i*8)));};
    u32(unsigned(std::size(ranges)));
    for(const auto& [base,size]:ranges){u32(base);u32(size);const auto bytes=memory.bytes(base,size);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    if(!file)throw std::runtime_error("draw oracle snapshot write failed");
}
}
int main(int argc,char** argv){
    try{
        if(argc!=4){std::cerr<<"Usage: fsb_render_tests ASSETS X86_REFERENCE OUTPUT_DIR\n";return 2;}
        const std::filesystem::path assets(argv[1]),output(argv[3]);std::filesystem::create_directories(output);
        auto memory=Memory::from_pe32(fsb::lab::read(assets/"FLYINGSB.EXE"));
        Arena arena(memory);arena.initialize();
        memory.write(0x6d9ebc,8); memory.write(0x57fd1c,0xffffffffu);
        Surfaces surfaces(memory);Palette palette(memory,{});Sprites sprites(memory,surfaces,palette);
        fsb::lab::register_sprite_assets(sprites,assets);sprites.initialize();sprites.initialize_scene_sheets();
        const auto reaction_surface=sprites.character(memory.read(0x766810)).surface;
        check(memory.read(0x766f6c)==0,"431907 bootstrap sprite families are resident before event activation");
        memory.write(0x57fd1c,0);
        check(memory.read(0x7664e8+26*4)==718 && memory.read(0x766a28+26*4)==7 && memory.read(0x7664e8+266*4)==5632,"original character family tables include organ and special effect banks");
        const auto empty=sprites.indexed(0,0);
        check(empty.surface==memory.read(0x57eb70) && empty.rect.right>0,"empty original indexed slots share the actual NOIMAGE surface");
        const auto organ=sprites.character(718);
        check(organ.rect.right==101 && organ.rect.bottom==115 && organ.origin_x==48 && organ.origin_y==77,"organ cached frame uses original dimensions and anchor");
        const auto raw=Image8::pcx(fsb::lab::read(assets/"ASE_FM/CSONA_E0.pcx"));
        const auto& cached=surfaces.get(organ.surface);bool pixels=true;
        for(unsigned y=0;y<115;++y)for(unsigned x=0;x<101;++x)pixels&=cached.pixels[y*101+x]==raw.pixels[(y+7)*raw.width+x+5];
        check(pixels,"original organ frame pixels survive cropped surface caching unchanged");
        check(memory.read(0x766f6c)==7 && memory.read(0x4a94c8+26*0x42c)!=0,"character cache prepares whole family and original40bdf9 retains resident source sheet");
        const auto standing=sprites.indexed(8,0), fallback=sprites.indexed(8,0xffffffff);
        check(standing.rect.left==fallback.rect.left && standing.rect.right==fallback.rect.right && memory.read(0x500638+8*68+0x30,2)==24,"indexed frame grid retains original out-of-range frame0 behavior");
        check(memory.read(0x5cd3f0)==0x804d98 && memory.read(0x804dc4,2)==48 && memory.read(0x804dc6,2)==20,"original shadow cache is48x20 per frame");
        const auto item=Image8::pcx(fsb::lab::read(assets/"pcxset/SITEM.pcx"));const auto color=item.palette[112];
        check(memory.read(0x804660+112*4)==(color.r|(std::uint32_t(color.g)<<8)|(std::uint32_t(color.b)<<16)),"SITEM supplies the scene's112..247 palette slice");

        DrawQueue draw(memory,surfaces,sprites);Viewport viewport(memory);viewport.configure_framebuffer(640,480);viewport.center(640,360);
        const auto reference=fsb::lab::read(argv[2]);
        const auto read=[&](unsigned a){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(reference.at(a+i))<<(i*8);return v;};
        check(reference.size()==12+24*33*4 && std::string(reference.begin(),reference.begin()+7)=="FSBREN1","draw oracle format");
        if(failed)return 1;
        const unsigned fields[]={8,12,16,28,32,36,0x130,0x134,0x138};unsigned mismatch=0;
        for(unsigned c=0;c<24;++c){
            const auto begin=12+c*33*4,index=read(begin),actor=Actors::slot(index);
            for(unsigned i=0;i<9;++i)memory.write(actor+fields[i],read(begin+4+i*4));
            memory.write(0x7873c0,384);memory.write(0x7873c4,1560);
            for(auto a:{0x6da2d4,0x768370,0x757dbc,0x74b6d8})memory.write(a,1);memory.write(0x766f74,266);
            draw.reset();for(unsigned p=0;p<4;++p)for(unsigned off=0;off<84;off+=4)memory.write(DrawQueue::record(p,0)+off,0);
            const auto command=draw.actor(index);
            for(unsigned field=0;field<21;++field){
                const auto value=field==20?0:memory.read(command+field*4), wanted=read(begin+40+field*4);
                if(value!=wanted && mismatch++<5)std::cerr<<"record oracle case="<<c<<" field="<<field<<" got="<<value<<" expected="<<wanted<<'\n';mix(value);
            }
            for(unsigned i=0;i<2;++i){const auto v=memory.read(actor+0x120+i*4);mismatch+=v!=read(begin+124+i*4);mix(v);}
        }
        check(mismatch==0,"all24 actor draw records and anchors match original x86 instruction execution");
        const auto freed=sprites.flush_deferred();
        check((freed&0xffff)==1 && (freed>>16)==15 && memory.read(0x514b94+718*64)==0,"deferred release drains event-owned indexed and character surfaces with original packed counts");
        check(surfaces.get(reaction_surface).width>0&&sprites.character(memory.read(0x766810)).surface==reaction_surface,"event cache flush preserves bootstrapped reaction surfaces used by live sweat children");
        bool released=false;try{surfaces.get(organ.surface);}catch(const Fault&){released=true;}check(released,"released frame surface cannot be silently reused");

        // Preserve the original selection-sort tie behavior. [A2,B2,C1] -> [C1,B2,A2].
        draw.reset();
        for(unsigned key:{2u,2u,1u})draw.tile(0,0,key,0x8000,0,0,0x57eb30,0,1);
        draw.flush(1);draw.sort(1,0);
        check(memory.read(0x787498)==DrawQueue::record(1,2)&&memory.read(0x78749c)==DrawQueue::record(1,1)&&memory.read(0x7874a0)==DrawQueue::record(1,0),"selection sort preserves original equal-depth swap order");

        // Compositor fixture: actual root preamble and early children, then
        // three explicit camera samples. This is NOT a full scheduled Event0.
        Actors actors(memory);actors.bootstrap_new_game_actors();
        Map map(memory);MapAssets maps;const auto names=Map::asset_names(memory,469);
        for(unsigned i=0;i<6;++i)maps.files[i]=fsb::lab::read(assets/"MAPSET"/names[i]);map.register_assets(469,maps);
        Audio audio(memory);audio.initialize();for(auto id:{8u,49u,50u})audio.register_wave(true,id,fsb::lab::read(assets/"audio"/Audio::resource_name(memory,true,id)));
        HsmQueue messages;Dialogue dialogue(memory,messages);dialogue.configure_timing(0,30,0);
        VmEnvironment env;env.map=&map;env.palette=&palette;env.audio=&audio;env.viewport=&viewport;env.dialogue=&dialogue;
        const auto root=arena.activate_event(0), root_obj=*resolve_compact(memory,root);Vm vm(memory,messages,env,root_obj);
        vm.run_frame(1);check(vm.pc()==0x61f2af,"actual root preamble still reaches first forced scheduling yield");
        ObjectPump pump(memory,arena,[&](Address callback,Address object,unsigned jobs){if(callback!=0x41a042)throw Fault(callback,"unexpected fixture callback");Vm(memory,messages,env,object).run_frame(jobs);});
        pump.update_group(1,1);
        check(memory.read(memory.read(0x6d050d)+0x134)==266,"actual SB child selects originalESON08 sprite family");
        const auto frame=surfaces.create(640,480);memory.write(0x6db16c,frame);
        const Address sheet=0x804d10;
        for(unsigned plane=0;plane<2;++plane){const auto definition=sheet+plane*68;const auto& image=map.sheet(plane);
            memory.write(definition+0x20,image.width/64,2);memory.write(definition+0x2c,64,2);memory.write(definition+0x2e,48,2);
            memory.write(definition+0x30,(image.width/64)*(image.height/48),2);memory.write(definition+0x40,surfaces.insert(image));
        }
        unsigned picture=0;
        for(unsigned camera_y:{264u,1200u,1560u,1640u}){
            memory.write(0x7873c0,384);memory.write(0x7873c4,camera_y);memory.write(0x6da2d4,100+picture);
            memory.write(0x74b6d8,0);memory.write(0x74b6f4,0);palette.upload(0x804660);map.update_scroll();surfaces.clear(frame,{0,0,640,480});draw.reset();
            for(unsigned i=0;i<768;++i)if(memory.read(Actors::slot(i)+4)&0x40)draw.actor(i);
            for(unsigned layer=0;layer<2;++layer){
                draw.background(map.background_commands(layer),sheet);const auto pass=layer*2+1;
                memory.write(0x800da8+layer*4,memory.read(0x787488+layer*8));
                if(picture==3)dump_draw_state(memory,output/("layer-"+std::to_string(layer)+"-before.bin"));
                draw.shadows(pass,memory.read(0x5cd3f0));draw.foreground(layer);
                if(picture==3)dump_draw_state(memory,output/("layer-"+std::to_string(layer)+"-after.bin"));
                const auto first=memory.read(0x787480);draw.flush(pass);draw.sort(pass,first);
            }
            draw.execute();surfaces.get(frame).palette=palette.colors();
            for(auto pixel:surfaces.get(frame).pixels)mix(pixel);
            fsb::lab::bmp(surfaces.get(frame),output/("scene-"+std::to_string(picture++)+".bmp"));
        }
        // Event0's loaded map has no visible foreground flags. Exercise both
        // branches separately in an explicitly synthetic, in-memory fixture.
        set_actor_tile_position(memory,Actors::slot(0),6,36,1);
        memory.write(Actors::slot(0)+4,memory.read(Actors::slot(0)+4)&~8u);
        memory.write(Actors::slot(0)+0x134,0x20000);memory.write(Actors::slot(0)+0x138,0);
        memory.write(0x7873c4,1752);draw.reset();draw.actor(0);
        memory.write(0x7cbca0+(4096+35*12+6)*4,5);memory.write(0x7cbca0+(4096+36*12+6)*4,3);
        const auto secondary_tile=memory.read(0x7e0d30+0x8028+(36*12+6)*4);memory.write(0x7dcca0+secondary_tile*4,0);
        dump_draw_state(memory,output/"occlusion-before.bin");draw.foreground(1);dump_draw_state(memory,output/"occlusion-after.bin");
        check(memory.read(0x787488+3*4)==3 && memory.read(DrawQueue::record(3,1)+0x3c)==0x804d10 && memory.read(DrawQueue::record(3,2)+0x3c)==0x804d54,
              "explicit occluder fixture emits primary and secondary foreground tiles using source height test");
        // Dense125 effects reach the pass stride while emitting shadows, then
        // append foreground into the next portion of the shared original pool.
        // Five shadows with425 existing records exercise three successful tile
        // allocations and two saturated returns of index428.
        for(unsigned i=0;i<2;++i){
            const auto actor=Actors::slot(i);set_actor_tile_position(memory,actor,6,36,0);
            memory.write(actor+4,i?0:8);memory.write(actor+0x134,0x20000);memory.write(actor+0x138,0);
        }
        draw.reset();draw.actor(0);draw.actor(1);
        const auto prototype=memory.bytes(DrawQueue::record(1,1),84);
        for(unsigned i=2;i<425;++i){
            const auto command=DrawQueue::record(1,i);
            for(unsigned j=0;j<84;++j)memory.write(command+j,prototype[j],1);
            if(i>=420)memory.write(command+0x10,Actors::slot(0));
        }
        // The first actor is a non-shadow placeholder too; only the final five
        // records point at the shadow-enabled source.
        memory.write(DrawQueue::record(1,0)+0x10,Actors::slot(1));
        memory.write(0x78748c,425);
        memory.write(0x7cbca0+(35*12+6)*4,5);memory.write(0x7cbca0+(36*12+6)*4,3);
        for(unsigned j=0;j<84;j+=4)memory.write(DrawQueue::record(2,0)+j,0xabababab);
        dump_draw_state(memory,output/"crowded-before.bin");
        draw.shadows(1,memory.read(0x5cd3f0));
        check(memory.read(0x78748c)==428&&memory.read(0x787480)==5,"original saturated shadow allocations keep428 and still queue five references");
        check(memory.read(0x787498+3*4)==DrawQueue::record(2,0)&&memory.read(0x787498+4*4)==DrawQueue::record(2,0),"saturated shadow indices preserve original shared-record aliasing");
        draw.foreground(0);
        dump_draw_state(memory,output/"crowded-after.bin");
        check(memory.read(0x78748c)>428&&memory.read(DrawQueue::record(2,0))==3,"foreground continues across the pass stride inside the physical pool");
        bool outside=false;try{DrawQueue::record(3,428);}catch(const Fault&){outside=true;}
        check(outside,"shared draw pool cannot overwrite the following sparkle table");
        //449's500-object burst leaves a zero type0 record at pass1,index500.
        //4546f4 tests its signed wrapped rectangle difference before touching
        //the source. Null sheet/surface pointers are therefore safe here.
        draw.reset();const auto empty_command=DrawQueue::record(1,500);
        for(unsigned j=0;j<84;j+=4)memory.write(empty_command+j,0);
        draw.enqueue(1,500);const auto previous_pixels=surfaces.get(frame).pixels;
        for(const auto [top,bottom]:{std::pair{0u,0u},std::pair{2u,3u},std::pair{0x7fffffffu,0xffffffffu},std::pair{0xffffffffu,0u}}){
            memory.write(empty_command+draw_offset::source_top,top);memory.write(empty_command+draw_offset::source_bottom,bottom);draw.execute();
            check(surfaces.get(frame).pixels==previous_pixels,"original type0 no-op skips empty source before blitting");
        }
        std::ofstream report(output/"render-probe.json");report<<"{\"scope\":\"root preamble plus early actor children, explicit camera samples264/1200/1560/1640; actor/tile/shadow/foreground compositor without full frame loop\",\"event0_complete\":false,\"x86_actor_record_cases\":24,\"record_mismatches\":"<<mismatch<<",\"synthetic_occlusion_fixture_separate_from_scene\":true,\"state_pixel_fnv1a\":\""<<std::hex<<hash<<"\"}\n";
        std::cout<<"render_state_pixel_fnv1a="<<std::hex<<hash<<std::dec<<'\n'<<"render_checks="<<checks<<" failures="<<failed<<'\n';
        return failed?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
