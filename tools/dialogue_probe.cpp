#include "fsb_core/dialogue.hpp"
#include "fsb_core/dialog_graphics.hpp"
#include "fsb_core/font_raster.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/viewport.hpp"
#include "fsb_core/vm.hpp"
#include "fsb_core/object_pump.hpp"
#include "lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3){std::cerr<<"Usage: fsb_dialogue_probe ASSETS OUTPUT_DIR\n";return 2;}
        const std::filesystem::path assets(argv[1]),output(argv[2]);std::filesystem::create_directories(output);
        auto memory=Memory::from_pe32(fsb::lab::read(assets/"FLYINGSB.EXE"));Arena arena(memory);arena.initialize();Actors actors(memory);actors.bootstrap_new_game_actors();
        const auto root=arena.activate_event(0),root_obj=*resolve_compact(memory,root);actors.set_party_mask(0x41,true);
        Viewport viewport(memory);viewport.configure_framebuffer(640,480);Surfaces surfaces(memory);const auto frame=surfaces.create(640,480);memory.write(0x6db16c,frame);
        const auto skin=fsb::lab::read(assets/"pcxset/WHDLGBOX.pcx");surfaces.get(frame).palette=Image8::pcx(skin).palette;
        // Map-loader fixed UI palette slices override the PCX's editing colors.
        // This isolated fixture uses the skin palette for the remaining entries;
        // complete Event0 still needs the real shared scene-palette producer chain.
        for(unsigned i=0;i<8;++i){const auto value=memory.read(0x5ab250+i*4);surfaces.get(frame).palette[248+i]={std::uint8_t(value),std::uint8_t(value>>8),std::uint8_t(value>>16)};}
        surfaces.get(frame).palette[0]={0,0,0};
        DialogGraphics graphics(memory,surfaces);graphics.load_skin(skin);graphics.load_arrows(fsb::lab::read(assets/"pcxset/WHARROW.pcx"));
        FontRaster fonts(memory);fonts.load(fsb::lab::read(assets/"fonts/gulim.ttc"),fsb::lab::read(assets/"fonts/batang.ttc"),fsb::lab::read(assets/"fonts/cp949.bin"));
        unsigned glyphs=0;std::ofstream glyph_trace(output/"glyphs.tsv");glyph_trace<<"ms\tcp949\tfont\tleft\ttop\tright\tbottom\n";
        graphics.glyph_renderer=[&](Image8& image,const DialogGlyph& glyph){fonts.draw(image,glyph);++glyphs;glyph_trace<<memory.read(0x6da2d8)<<"\t0x"<<std::hex<<glyph.cp949<<std::dec<<'\t'<<glyph.font_index<<'\t'<<glyph.rect.left<<'\t'<<glyph.rect.top<<'\t'<<glyph.rect.right<<'\t'<<glyph.rect.bottom<<'\n';};
        HsmQueue queue;Dialogue dialogue(memory,queue);dialogue.configure_timing(0,30,0);dialogue.graphics=&graphics;
        const auto actor=lookup_actor(memory,9);memory.write(actor+0x120,352);memory.write(actor+0x124,138);memory.write(actor+0x110,1);
        const auto handle=dialogue.spawn_markup(9,0x61ecc2,packed_id("SONA")),ctrl=*resolve_compact(memory,handle),d=dialogue.state(handle);
        VmEnvironment environment;Vm sender(memory,queue,environment,root_obj);
        std::ofstream trace(output/"dialogue-state.tsv");trace<<"ms\tbase\tcursor\tlifecycle\tflags_a\tflags_b\tflags_c\twidth\theight\tbox_left\tbox_top\tglyphs\n";
        bool initial_wait=false,marker=false;
        for(unsigned tick=0;tick<400;++tick){
            const auto now=tick*16;memory.write(0x6da2d8,now);memory.write(0x6d9e74,16);memory.write(0x768460,0);
            memory.write(0x6da2dc,now==4000?0x100:now==4016?0x101:0);memory.write(0x6d66b0,13);
            if(tick==10){memory.write(root_obj+0x30,0x61f33e);sender.step();} // Actual X -> SONA opcode from original Event0.
            surfaces.clear(frame,{0,0,640,480});dialogue.tick(ctrl);
            if(tick==9)initial_wait=(memory.read(d+0x15c)&16)&&glyphs==0;
            trace<<now<<"\t0x"<<std::hex<<memory.read(d+8)<<std::dec<<'\t'<<memory.read(d+0x150)<<'\t'<<memory.read(ctrl+0x20)<<"\t0x"<<std::hex<<memory.read(d+0x154)<<"\t0x"<<memory.read(d+0x158)<<"\t0x"<<memory.read(d+0x15c)<<std::dec<<'\t'<<memory.read(d+0x70)<<'\t'<<memory.read(d+0x74)<<'\t'<<signed32(memory.read(d+0xc4))<<'\t'<<signed32(memory.read(d+0xc8))<<'\t'<<glyphs<<'\n';
            if(tick==60||tick==120||tick==188)fsb::lab::bmp(surfaces.get(frame),output/("dialogue-"+std::to_string(now)+".bmp"));
            if(queue.find(packed_id("010"),packed_id("SONA"))){marker=true;fsb::lab::bmp(surfaces.get(frame),output/"dialogue-marker010.bmp");break;}
        }
        const auto sona_glyphs=glyphs,sona_bitmaps=fonts.bitmap_glyphs(),sona_outlines=fonts.outline_glyphs();
        unsigned caption_glyphs=0;graphics.glyph_renderer=[&](Image8& image,const DialogGlyph& glyph){fonts.draw(image,glyph);++caption_glyphs;};
        // The actual MES1 subtitle exercises source end, explicit delay, closing
        // tween and object/surface teardown without confirm or fabricated ACKs.
        const auto caption=dialogue.spawn_markup(0xffffffff,0x61f03a,packed_id("MES1")),caption_ctrl=*resolve_compact(memory,caption),caption_state=dialogue.state(caption);
        ObjectPump pump(memory,arena,[&](Address callback,Address object,unsigned){if(callback!=0x40dd27)throw Fault(callback,"unexpected dialogue fixture callback");dialogue.tick(object);});
        unsigned caption_end_ms=0;
        for(unsigned tick=0;tick<1000&&resolve_compact(memory,caption);++tick){const auto now=5000+tick*16;memory.write(0x6da2d8,now);memory.write(0x6d9e74,16);memory.write(0x768460,0);memory.write(0x6da2dc,0);
            surfaces.clear(frame,{0,0,640,480});pump.update_object(caption_ctrl,1);caption_end_ms=now;
        }
        bool caption_freed=false;try{memory.read(caption_state);}catch(const Fault&){caption_freed=true;}
        const bool caption_complete=!resolve_compact(memory,caption)&&caption_freed&&memory.read(0x768684)==1;
        std::ofstream report(output/"dialogue-probe.json");report<<"{\n  \"scope\": \"original SONA first chunk and MES1 lifecycle, explicit anchor/input fixtures; not full Event0\",\n  \"start_message_ms\": 160,\n  \"confirm_key_ms\": 4000,\n  \"initial_hsm_wait\": "<<(initial_wait?"true":"false")<<",\n  \"emitted_original_010_marker\": "<<(marker?"true":"false")<<",\n  \"drawn_sona_glyphs\": "<<sona_glyphs<<",\n  \"sona_embedded_bitmap_cache_entries\": "<<sona_bitmaps<<",\n  \"sona_outline_fallback_cache_entries\": "<<sona_outlines<<",\n  \"caption_completed_without_input\": "<<(caption_complete?"true":"false")<<",\n  \"caption_end_ms\": "<<caption_end_ms<<",\n  \"caption_glyphs\": "<<caption_glyphs<<",\n  \"event0_complete\": false\n}\n";
        std::cout<<"initial_wait="<<initial_wait<<" marker010="<<marker<<" glyphs="<<sona_glyphs<<" bitmap_cache="<<sona_bitmaps<<" outline_fallback="<<sona_outlines<<" caption_complete="<<caption_complete<<'\n';
        return initial_wait&&marker&&caption_complete?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
