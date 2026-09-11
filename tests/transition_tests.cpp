#include "fsb_core/transition.hpp"
#include "fsb_core/viewport.hpp"
#include "../tools/lab_io.hpp"
#include <algorithm>
#include <iostream>

using namespace fsb::core;
namespace {
std::uint8_t pattern(int x,int y){return std::uint8_t(1+(x/7+y/5)%253);}
struct Scene {
    Memory memory;Surfaces surfaces;Viewport viewport;Transition transition;Address primary,back,actor;
    Scene(const std::vector<std::uint8_t>& exe,int height,int x,int y):memory(Memory::from_pe32(exe)),surfaces(memory),viewport(memory),transition(memory,surfaces){
        viewport.configure_framebuffer(640,480);viewport.center(640,height,true,false);
        primary=surfaces.create(640,480);back=surfaces.create(640,480);actor=memory.allocate_zeroed(428);
        memory.write(0x6e144c,primary);memory.write(0x6d9d00,back);memory.write(0x6db16c,back);memory.write(0x6d9ebc,0x21);
        memory.write(actor+0x120,std::uint32_t(x));memory.write(actor+0x124,std::uint32_t(y));
        std::fill(surfaces.get(primary).pixels.begin(),surfaces.get(primary).pixels.end(),254);
        for(int py=0;py<480;++py)for(int px=0;px<640;++px)surfaces.get(back).pixels[py*640+px]=pattern(px,py);
    }
    void progress(unsigned value,bool up=true){memory.write(0x6da4e8,0);memory.write(0x6da4ec,up?30030-value:value);memory.write(0x6da4f4,0);memory.write(0x6da500,up);}
};
}
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;
        const auto exe=fsb::lab::read(argv[1]),reference=fsb::lab::read(argv[2]);
        if(reference.size()<12||std::string(reference.begin(),reference.begin()+8)!=std::string("FSBRAD1\0",8))throw std::runtime_error("invalid radial reference");
        std::size_t at=8;const auto word=[&](){if(at+4>reference.size())throw std::runtime_error("truncated radial reference");std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(reference[at++])<<(8*i);return v;};
        const auto count=word();unsigned failures=0,checks=0;const auto check=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<label<<'\n';}};
        for(unsigned i=0;i<count;++i){
            const auto left=signed32(word()),top=signed32(word()),right=signed32(word()),bottom=signed32(word()),ax=signed32(word()),ay=signed32(word());
            const auto mode=word(),progress=word(),strips=word();
            Scene scene(exe,bottom-top,ax,ay);scene.transition.start(4,mode,1000,scene.actor);scene.progress(progress);
            scene.transition.present(0);auto expected=std::vector<std::uint8_t>(640*480,254);
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)expected[y*640+x]=0;
            for(unsigned j=0;j<strips;++j){
                const auto x0=signed32(word()),y0=signed32(word()),x1=signed32(word()),y1=signed32(word());
                for(int y=std::max(0,y0);y<std::min(480,y1);++y)for(int x=std::max(0,x0);x<std::min(640,x1);++x)expected[y*640+x]=pattern(x,y);
            }
            if(scene.surfaces.get(scene.primary).pixels!=expected)std::cerr<<"radial fixture="<<i<<" progress="<<progress<<'\n';
            check(scene.surfaces.get(scene.primary).pixels==expected,"radial image differs from actual x86 strip geometry");
        }
        check(at==reference.size(),"radial reference has unconsumed bytes");
        const auto contract=fsb::lab::read(std::filesystem::path(argv[2]).parent_path()/"contract-x86.bin");std::size_t contract_at=8;
        const auto contract_word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(contract.at(contract_at++))<<(i*8);return value;};
        const auto contract_cases=contract_word();
        for(unsigned i=0;i<contract_cases;++i){
            const Rect view{signed32(contract_word()),signed32(contract_word()),signed32(contract_word()),signed32(contract_word())};const auto progress=contract_word();
            Scene scene(exe,view.bottom-view.top,0,0);const auto expected=scene.surfaces.create(640,480);
            std::fill(scene.surfaces.get(expected).pixels.begin(),scene.surfaces.get(expected).pixels.end(),254);scene.surfaces.clear(expected,view,0);
            for(unsigned strip=0;strip<48;++strip){
                const Rect destination{signed32(contract_word()),signed32(contract_word()),signed32(contract_word()),signed32(contract_word())};
                const Rect source{signed32(contract_word()),signed32(contract_word()),signed32(contract_word()),signed32(contract_word())};
                scene.surfaces.stretch(expected,destination,scene.back,source);
            }
            scene.transition.start(3,0,1000,0);scene.progress(progress);scene.transition.present(0);
            check(scene.surfaces.get(scene.primary).pixels==scene.surfaces.get(expected).pixels,"contract image differs from original x86 strip geometry");
        }
        check(contract_cases==24&&contract_at==contract.size(),"contract reference covers24progress/viewport cases and1152strips");
        const auto campaign=fsb::lab::read(std::filesystem::path(argv[2]).parent_path()/"campaign-transitions-x86.bin");std::size_t campaign_at=8;
        const auto campaign_word=[&](){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(campaign.at(campaign_at++))<<(i*8);return v;};
        const auto campaign_rect=[&](){return Rect{signed32(campaign_word()),signed32(campaign_word()),signed32(campaign_word()),signed32(campaign_word())};};
        const auto campaign_cases=campaign_word();
        for(unsigned i=0;i<campaign_cases;++i){
            const auto kind=campaign_word(),mode=campaign_word(),progress=campaign_word(),seed=campaign_word();const auto view=campaign_rect();const auto rng=campaign_word(),points=campaign_word();
            Scene scene(exe,view.bottom-view.top,0,0);scene.memory.write(0x6d1bf0,seed);scene.transition.start(kind,mode,2000,0);
            check(scene.memory.read(0x6d1bf0)==rng,"scatter setup consumes the original RNG sequence");
            for(unsigned j=0;j<points;++j)check(scene.memory.read(0x6e0e10+j*4)==campaign_word(),"scatter start point differs from original x86");
            const Address expected[]={scene.surfaces.create(640,480),scene.surfaces.create(640,480),scene.back};
            std::fill(scene.surfaces.get(expected[0]).pixels.begin(),scene.surfaces.get(expected[0]).pixels.end(),254);
            const auto operations=campaign_word();
            for(unsigned j=0;j<operations;++j){
                const auto stretch=campaign_word(),dst=campaign_word(),src=campaign_word();const auto destination=campaign_rect(),source=campaign_rect();
                if(stretch)scene.surfaces.stretch(expected[dst],destination,expected[src],source);
                else scene.surfaces.blit(expected[dst],destination.left,destination.top,expected[src],source);
            }
            scene.progress(progress);scene.transition.present(0);
            if(scene.surfaces.get(scene.primary).pixels!=scene.surfaces.get(expected[0]).pixels)std::cerr<<"campaign transition="<<kind<<" progress="<<progress<<" case="<<i<<'\n';
            check(scene.surfaces.get(scene.primary).pixels==scene.surfaces.get(expected[0]).pixels,"campaign transition differs from original x86 blit geometry");
        }
        check(campaign_cases==48&&campaign_at==campaign.size(),"campaign transition oracle consumed all48cases");
        Scene radial(exe,450,320,225);radial.transition.start(4,1,100,radial.actor);radial.transition.present(100);
        check(!radial.memory.read(0x6d9e70)&&radial.memory.read(0x6d9e98)==0x402569,"count-down endpoint must retain radial hook but release wait");
        bool black=true;for(int y=15;y<465;++y)for(int x=0;x<640;++x)black&=radial.surfaces.get(radial.primary).pixels[y*640+x]==0;
        check(black,"closed radial effect must hide the viewport");
        radial.memory.write(0x6da2d8,100);radial.transition.start(4,0,100,radial.actor);radial.transition.present(200);
        check(!radial.memory.read(0x6d9e70)&&!radial.memory.read(0x6d9e98)&&radial.memory.read(0x6db16c)==radial.back,"count-up endpoint restores ordinary rendering");
        Scene zoom(exe,450,320,225);zoom.transition.start(0,1,100,zoom.actor);zoom.transition.present(100);
        bool magnified=true;for(int y=15;y<465;++y)for(int x=0;x<640;++x)magnified&=zoom.surfaces.get(zoom.primary).pixels[y*640+x]==pattern(320,225);
        check(magnified,"point zoom endpoint must magnify the actor pixel across the viewport");
        zoom.memory.write(0x6da2d8,100);zoom.transition.start(0,0,100,zoom.actor);zoom.transition.present(200);
        bool restored=true;for(int y=15;y<465;++y)for(int x=0;x<640;++x)restored&=zoom.surfaces.get(zoom.primary).pixels[y*640+x]==pattern(x,y);
        check(restored&&!zoom.memory.read(0x6d9e98),"reversed zoom must restore the source image and clear its hook");
        std::cout<<"transition_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
