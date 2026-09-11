#include "fsb_core/playback.hpp"
#include <iostream>
#ifdef FSB_TEST_TEXT
#include "runtime_assets.hpp"
#include "fsb_core/symbols.hpp"
#endif
using namespace fsb::core;
namespace {
unsigned checks=0,failures=0;
void check(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
#ifdef FSB_TEST_TEXT
struct Snapshot {std::vector<Memory::Region> regions;std::vector<std::uint8_t> image;std::vector<std::int16_t> pcm;Address pc;unsigned frames;};
Snapshot replay(const std::filesystem::path& assets,unsigned speed,bool automatic,bool changing=false,bool silent=false){
    Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,0);
    runtime.start_event0();runtime.dialogue.set_auto_advance(automatic);runtime.advance(0);PlaybackClock clock(speed);Snapshot result{};
    while(clock.now()<20000){
        clock.elapse(1);
        while(clock.pending()&&clock.now()<20000){
            clock.step();
            if(clock.now()==200){runtime.post_input(keyboard_message(37,0xcb,true));runtime.post_input(keyboard_message(39,0xcd,true));}
            if(clock.now()==248){runtime.post_input(keyboard_message(37,0xcb,false));runtime.post_input(keyboard_message(39,0xcd,false));}
            const auto step=runtime.advance(clock.now(),!silent||clock.now()>19000);if(step.rendered)++result.frames;
            result.pcm.insert(result.pcm.end(),step.pcm.begin(),step.pcm.end());
            if(changing){if(clock.now()==3000)clock.set_speed(8);if(clock.now()==9000)clock.set_speed(16);if(clock.now()==17000)clock.set_speed(1);}
        }
    }
    result.regions=runtime.memory.snapshot_regions();result.image=runtime.frame().pixels;for(auto c:runtime.frame().palette){result.image.push_back(c.r);result.image.push_back(c.g);result.image.push_back(c.b);}result.pc=runtime.root_pc();return result;
}
bool equal(const Snapshot& a,const Snapshot& b){
    if(a.regions.size()!=b.regions.size()||a.image!=b.image||a.pcm!=b.pcm||a.pc!=b.pc||a.frames!=b.frames)return false;
    for(unsigned i=0;i<a.regions.size();++i)if(a.regions[i].base!=b.regions[i].base||a.regions[i].bytes!=b.regions[i].bytes||a.regions[i].writable!=b.regions[i].writable||a.regions[i].allocation!=b.regions[i].allocation)return false;return true;
}
void choices(const std::filesystem::path& assets){
    Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,11);runtime.start_field_fixture(22,11);runtime.dialogue.set_auto_advance(true);
    auto& m=runtime.memory;const auto make=[&](const std::string& value){const auto address=m.allocate_zeroed(unsigned(value.size()+1));for(unsigned i=0;i<value.size();++i)m.write(address+i,std::uint8_t(value[i]),1);return runtime.dialogue.create_at(0,0,address,0);};
    const auto normal=make("Ordinary; text"),choice=make("<ITEM>First|<ITEM>Second<SELECT>"),blocked=make("Waiting/");const auto mailbox=m.allocate_zeroed(4);m.write(mailbox,0xcafebabe);m.write(*resolve_compact(m,choice)+0xe8,mailbox);
    for(unsigned now=0;now<=3000;++now)runtime.advance(now);
    check(!resolve_compact(m,normal),"automatic ordinary dialogue finishes through its normal end/cleanup path");
    const auto choice_state=runtime.dialogue.state(choice);check((m.read(choice_state+dialog_offset::flags_c)&64)&&m.read(mailbox)==0xcafebabe,"automatic dialogue leaves SELECT waiting without choosing the default answer");
    check((m.read(runtime.dialogue.state(blocked)+dialog_offset::flags_c)&16)!=0,"automatic dialogue never clears an HSM message wait");
    runtime.post_input(keyboard_message(40,0xd0,true));for(unsigned now=3001;now<=3050;++now)runtime.advance(now);
    runtime.post_input(keyboard_message(40,0xd0,false));for(unsigned now=3051;now<=3100;++now)runtime.advance(now);
    runtime.post_input(keyboard_message(13,0x1c,true));for(unsigned now=3101;now<=3200;++now)runtime.advance(now);
    check(m.read(mailbox)==1,"manual selection still returns the user's second answer while auto dialogue is enabled");
}
#endif
}
int main(int argc,char** argv){
    try{
        for(unsigned speed:{1u,4u,8u,16u}){PlaybackClock clock(speed);clock.elapse(25);unsigned steps=0;while(clock.step()){++steps;check(clock.now()==steps,"each accelerated step advances exactly one virtual millisecond");}check(steps==25*speed,"requested rate determines the pacing budget");}
        PlaybackClock clock(16);clock.elapse(20);for(unsigned i=0;i<10;++i)clock.step();clock.set_speed(1);check(clock.now()==10&&!clock.pending(),"speed changes preserve current time and discard old unexecuted debt");clock.elapse(7);while(clock.step()){}check(clock.now()==17,"normal speed resumes without backward or giant time jumps");
        clock.elapse(10);clock.elapse(11001);check(clock.now()==17&&!clock.pending(),"suspend-size gaps do not become elapsed game time");clock.elapse(10);clock.elapse(3,false);check(!clock.pending()&&clock.now()==17,"inactive host time never accumulates for resume");clock.set_speed(16);clock.elapse(1000);check(clock.pending()==1000,"work debt is bounded when the device cannot reach requested speed");
        for(unsigned invalid:{0u,2u,3u,5u,32u}){bool rejected=false;try{clock.set_speed(invalid);}catch(const Fault&){rejected=true;}check(rejected,"rates outside the offered presets are rejected");}
        {bool accepted=true;try{PlaybackClock quarter(4);quarter.elapse(3);unsigned steps=0;while(quarter.step())++steps;accepted=steps==12;}catch(const Fault&){accepted=false;}check(accepted,"4x is an offered preset and paces four ticks per real millisecond");}
        for(unsigned preset:{1u,4u,8u,16u}){
            PlaybackClock held(preset);held.elapse(1);held.step();const auto now=held.now();held.set_fast_forward_held(true);
            check(held.speed()==16&&held.now()==now,"hold temporarily selects16x without changing simulation time");
            held.elapse(2);const auto pending=held.pending();held.set_fast_forward_held(true);
            check(held.pending()==pending,"repeated keydown does not reset pacing debt or overwrite the selected speed");
            held.set_fast_forward_held(false);held.set_fast_forward_held(false);
            check(held.speed()==preset&&!held.fast_forward_held(),"release or focus loss restores the selected preset and tolerates a duplicate keyup");
        }
        PlaybackClock held;held.set_fast_forward_held(true);held.set_speed(8);check(held.speed()==16,"holding turbo takes precedence over changing the base preset");held.set_fast_forward_held(false);check(held.speed()==8,"release applies the latest explicitly selected base preset");
#ifdef FSB_TEST_TEXT
        if(argc!=2)return 2;const std::filesystem::path assets=argv[1];const auto normal=replay(assets,1,false);
        for(unsigned rate:{8u,16u})check(equal(normal,replay(assets,rate,false)),"real Event0 state, image, palette and PCM match1x at accelerated pacing");
        check(equal(normal,replay(assets,1,false,true)),"real Event0 remains identical across1x→8x→16x→1x changes");
        const auto automatic=replay(assets,1,true);check(automatic.pc!=normal.pc,"auto dialogue makes real Event0 progress beyond the manual text wait");check(equal(automatic,replay(assets,16,true)),"automatic dialogue is deterministic across playback rates");choices(assets);
        auto resumed=automatic;resumed.pcm.erase(resumed.pcm.begin(),resumed.pcm.begin()+19000*44100/1000*2);
        check(equal(resumed,replay(assets,16,true,true,true)),"PCM-free accelerated runtime preserves complete guest state, frame, PC and resumed PCM");
#endif
        std::cout<<"playback_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
