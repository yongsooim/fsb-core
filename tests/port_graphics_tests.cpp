#include "runtime_assets.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=4)return 2;
        const std::filesystem::path assets=argv[1],output=argv[3];std::filesystem::create_directories(output);
        const auto executable=fsb::lab::read(assets/"FLYINGSB.EXE"),reference=fsb::lab::read(argv[2]);
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        check(reference.size()>=12&&std::string(reference.begin(),reference.begin()+8)==std::string("FSBGFX1\0",8),"invalid original draw geometry fixture");
        std::size_t cursor=8;const auto word=[&](){unsigned value=0;for(unsigned i=0;i<4;++i)value|=unsigned(reference.at(cursor++))<<(i*8);return value;};
        const auto cases=word();Runtime runtime(executable);auto& m=runtime.memory;
        const auto target=runtime.surfaces.create(24,24),source=runtime.surfaces.create(4,4),sheet=m.allocate_zeroed(68),command=m.allocate_zeroed(84);
        for(unsigned i=0;i<16;++i)runtime.surfaces.get(source).pixels[i]=i%3?5:0;
        m.write(sheet+0x20,1);m.write(sheet+0x2c,4,2);m.write(sheet+0x2e,4,2);m.write(sheet+0x30,1,2);m.write(sheet+0x40,source);
        m.write(globals::render_target_surface,target);
        for(unsigned i=0;i<4;++i)m.write(globals::clip_left+i*4,i<2?8:18);
        for(unsigned index=0;index<cases;++index){
            const auto kind=word(),flags=word();runtime.surfaces.clear(target,{0,0,24,24},9);
            for(unsigned i=0;i<84;i+=4)m.write(command+i,0);
            m.write(globals::draw_queue_count,1);m.write(globals::draw_queue,command);m.write(command+4,kind);
            m.write(command+0x14,6);m.write(command+0x18,6);
            for(unsigned i=0;i<4;++i){m.write(command+0x1c+i*4,i<2?0:4);m.write(command+0x2c+i*4,i<2?6:14);}
            m.write(command+0x3c,sheet);m.write(command+0x44,flags);m.write(command+0x50,source);
            runtime.draw.execute();const auto& actual=runtime.surfaces.get(target).pixels;
            check(std::equal(actual.begin(),actual.end(),reference.begin()+cursor),"draw output differs from original dispatcher geometry");cursor+=576;
        }
        check(cursor==reference.size(),"unconsumed draw oracle");
        //Actual overlay-tag actor command; the source frame supplies its own
        //rectangle and center anchors, independently of the selector low word.
        const auto old_metadata=m.bytes(0x5d1da0,24);m.write(0x5d1da0,0);m.write(0x5d1da4,0);m.write(0x5d1da8,0);m.write(0x5d1dac,4);m.write(0x5d1db0,4);m.write(0x804d0c,source);
        const auto actor=Actors::slot(100);m.write(actor+actor_offset::sprite_selector,0x300ab);m.write(actor+actor_offset::sprite_frame,0);m.write(actor+actor_offset::layer_q16,0);m.write(actor+actor_offset::world_x,10*65536);m.write(actor+actor_offset::world_y,10*65536);
        runtime.draw.reset();const auto draw=runtime.draw.actor(100);check(m.read(draw+draw_offset::surface)==source&&m.read(draw+draw_offset::source_right)==4&&m.read(draw+draw_offset::source_bottom)==4,"overlay actor frame was not resolved");
        for(unsigned i=0;i<old_metadata.size();++i)m.write(0x5d1da0+i,old_metadata[i],1);

        Runtime visual(executable);FontRaster fonts(visual.memory);fsb::lab::register_runtime_assets(visual,fonts,assets,Runtime::full_campaign);
        visual.start_field_fixture(22,Runtime::full_campaign);auto& memory=visual.memory;auto& call=visual.battle.recovered;
        const auto string=[&](const std::string& value){const auto p=memory.allocate_zeroed(unsigned(value.size())+1);for(unsigned i=0;i<value.size();++i)memory.write(p+i,std::uint8_t(value[i]),1);return p;};
        const auto text=string("<ids_MIRO>Portable name display.");const auto handle=visual.dialogue.create(0xffffffff,text,-1),object=*resolve_compact(memory,handle),state=visual.dialogue.state(handle);
        for(unsigned now=16;now<=2000&&!memory.read(state+dialog_offset::speaker_name_length);now+=16){memory.write(globals::frame_time_ms,now);visual.dialogue.tick(object);}
        check(memory.read(state+dialog_offset::speaker_name_length)==4&&memory.bytes(state+dialog_offset::speaker_name,4)==std::vector<std::uint8_t>({'M','I','R','O'}),"actual ids_NAME token did not populate the original buffer");
        visual.graphics.layout(state);const auto& atlas=visual.surfaces.get(memory.read(state+dialog_offset::skin_surface));
        check(std::count(atlas.pixels.begin()+96*512,atlas.pixels.end(),255)>0,"speaker name did not reach its atlas band");
        visual.surfaces.clear(memory.read(globals::render_target_surface),{0,0,640,480});
        memory.write(state+dialog_offset::anchor_x,320);memory.write(state+dialog_offset::target_rect+12,180);visual.graphics.draw_speaker_name(state,true);

        const auto resource=string("FORMAT_OVERRIDE.BMP");visual.sprites.register_image("FORMAT_OVERRIDE.BMP",fsb::lab::read(assets/"pcxset/WHDLGBOX.pcx"));
        const auto dimensions=memory.allocate_zeroed(8),palette=memory.allocate_zeroed(1024);
        const auto decoded=call.invoke(0x406044,{resource,dimensions,dimensions+4,palette,2});check(memory.read(dimensions)==512&&memory.read(dimensions+4)==480,"explicit PCX selector was ignored");
        call.invoke(0x4050fa,{resource,palette,2});visual.surfaces.release(decoded);

        const auto canvas=visual.surfaces.create(64,32);visual.surfaces.clear(canvas,{0,0,64,32},9);
        fonts.set_enhanced(true);visual.graphics.plain_text(canvas,{0,0,64,32},9,255,"&A && B",0x100,Rect{8,0,24,16},5);
        const auto& clipped=visual.surfaces.get(canvas);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)if(x<8||x>=24||y>=16)check(clipped.pixels[y*64+x]==9&&!clipped.detail_cell(y*64+x),"text or High-DPI detail escaped the explicit clip");
        check(std::count(clipped.pixels.begin(),clipped.pixels.end(),5)>0,"opaque text background was omitted");

        const auto before=memory.storage_usage().regions;visual.graphics.queue_text_overlay(10,12,0x10000ff,0x1000005,"Layer 0",0);visual.graphics.queue_text_overlay(310,205,0xffffff,0xffffffff,"Clipped layer 1",1);
        check(memory.read(globals::text_overlay_count)==2,"text overlay producer count mismatch");call.invoke(0x407e14);
        check(!memory.read(globals::text_overlay_count)&&memory.storage_usage().regions==before,"text overlay queue did not release its buffers");
        visual.viewport.center(620,460,true,false);call.invoke(0x406996,{37});
        const auto borders=visual.arena.members(0);const auto border=*resolve_compact(memory,borders.back());visual.viewport.tick(border,1);
        const auto fills=visual.viewport.take_fills();check(!fills.empty()&&std::all_of(fills.begin(),fills.end(),[](const auto& fill){return fill.index==37;}),"nonzero border COLORFILL was lost");
        auto image=visual.surfaces.get(memory.read(globals::render_target_surface));image.palette=visual.palette.colors();fsb::lab::bmp(image,output/"names-and-overlay.bmp");
        auto clipped_preview=clipped;clipped_preview.palette=visual.palette.colors();fsb::lab::bmp(clipped_preview,output/"clipped-text.bmp");
        // Use the product frame loop: an unclipped skill-name overlay may enter
        // a map's margins, but its indexed pixels AND enhanced ink must expire.
        Runtime margins(executable);FontRaster margin_fonts(margins.memory);
        fsb::lab::register_runtime_assets(margins,margin_fonts,assets,Runtime::full_campaign);
        margin_fonts.set_enhanced(true);margins.start_field_fixture(22,Runtime::full_campaign);
        unsigned now=16;check(margins.advance(now,false).rendered,"margin fixture did not render");
        for(const auto size: {std::pair{500,480},std::pair{640,300},std::pair{500,300}}){
            margins.viewport.center(size.first,size.second,true,false);
            const int left=(640-size.first)/2,top=(480-size.second)/2;
            const auto outside=[&](unsigned x,unsigned y){return x<unsigned(left)||x>=unsigned(left+size.first)||y<unsigned(top)||y>=unsigned(top+size.second);};
            for(const auto position: {std::pair{5,220},std::pair{580,220},std::pair{280,5},std::pair{280,450}})
                margins.graphics.queue_text_overlay(position.first,position.second,0x10000ff,0xffffffff,"SKILL",0);
            check(margins.advance(now+=16,false).rendered,"overlay frame did not render");
            unsigned ink=0,detail=0;
            for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x)if(outside(x,y)){
                const auto index=y*640+x;ink+=margins.frame().pixels[index]!=0;detail+=margins.frame().detail_cell(index)!=nullptr;
            }
            check(ink&&detail,"unclipped enhanced overlay must be visible in the margins during its frame");
            const auto overlay_pixels=margins.frame().pixels;
            check(!margins.advance(now,false).rendered&&margins.frame().pixels==overlay_pixels,"idle clock step erased the displayed overlay");
            if(size.first==500&&size.second==300)fsb::lab::bmp(margins.frame(),output/"margin-overlay.bmp");
            check(margins.advance(now+=16,false).rendered,"cleanup frame did not render");
            for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x)if(outside(x,y)){
                const auto index=y*640+x;
                check(!margins.frame().pixels[index]&&!margins.frame().detail_cell(index),"expired skill text remained in map margins");
            }
            if(size.first==500&&size.second==300)fsb::lab::bmp(margins.frame(),output/"margin-clean.bmp");
        }
        margins.memory.write(globals::game_mode,8);
        auto& retained=margins.surfaces.get(margins.memory.read(globals::render_target_surface));
        retained.set_pixel(0,37);SubpixelImage::Tile retained_ink;retained_ink.fill(255);retained.set_detail_cell(0,retained_ink);
        check(margins.advance(now+=16,false).rendered&&margins.frame().pixels[0]==37&&margins.frame().detail_cell(0),"capture/hold mode lost its retained border content");
        std::cout<<"original_draw_geometry_cases="<<cases<<" overlay_actor/name_token/image_format/opaque_clipped_text/queue_lifetime/border_color/map_margin_cleanup=passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
