#include "fsb_core/audio.hpp"
#include "../tools/lab_io.hpp"
#include <algorithm>
#include <iostream>
using namespace fsb::core;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
std::vector<std::uint8_t> wave(unsigned rate,unsigned frames,unsigned channels){
    std::vector<std::uint8_t> w(44+frames*channels*2);
    const auto put=[&](unsigned at,unsigned value,unsigned size=4){for(unsigned i=0;i<size;++i)w[at+i]=std::uint8_t(value>>(8*i));};
    put(0,0x46464952);put(4,unsigned(w.size())-8);put(8,0x45564157);put(12,0x20746d66);put(16,16);put(20,1,2);put(22,channels,2);put(24,rate);put(28,rate*channels*2);put(32,channels*2,2);put(34,16,2);put(36,0x61746164);put(40,frames*channels*2);
    for(unsigned i=0;i<frames*channels;++i)put(44+i*2,(i*997)%30000+1,2);
    return w;
}
struct Fixture {
    Memory memory;AudioOutput output;Audio audio;
    Fixture(const std::vector<std::uint8_t>& exe,unsigned rate=32000):memory(Memory::from_pe32(exe)),audio(memory){
        audio.initialize();audio.register_wave(true,8,wave(22050,301,2));
        for(unsigned id:{125u,126u}){audio.register_wave(false,id,wave(rate,4097,1));memory.write(0x5ad390+id*16,0);}
    }
    void start(){audio.load_bgm(8);audio.play_bgm();audio.play_cue(125);audio.play_cue(126);}
};
bool same_events(const Audio& a,const Audio& b){
    if(a.output_sample()!=b.output_sample()||a.events().size()!=b.events().size())return false;
    for(unsigned i=0;i<a.events().size();++i){const auto& x=a.events()[i];const auto& y=b.events()[i];if(x.kind!=y.kind||x.bgm!=y.bgm||x.id!=y.id||x.handle!=y.handle||x.output_sample!=y.output_sample||x.millibels!=y.millibels)return false;}return true;
}
bool same_memory(const Memory& a,const Memory& b){
    const auto a_regions=a.snapshot_regions(),b_regions=b.snapshot_regions();
    if(a_regions.size()!=b_regions.size())return false;
    for(unsigned i=0;i<a_regions.size();++i){const auto& x=a_regions[i];const auto& y=b_regions[i];if(x.base!=y.base||x.bytes!=y.bytes||x.writable!=y.writable||x.allocation!=y.allocation)return false;}return true;
}
}
int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;const auto exe=fsb::lab::read(std::filesystem::path(argv[1])/"FLYINGSB.EXE");
        for(unsigned rate:{11025u,32000u,44100u,192000u}){
            Fixture scalar(exe,rate),silent(exe,rate);scalar.start();silent.start();
            for(unsigned count:{0u,1u,43u,44u,137u,4096u,44100u,50000u}){
                scalar.audio.mix(count);silent.audio.advance_silently(count);
                check(same_events(scalar.audio,silent.audio)&&same_memory(scalar.memory,silent.memory),"silent advance preserves guest state, sample cursor and exact simultaneous completion ordering");
                check(scalar.audio.mix(97)==silent.audio.mix(97),"PCM after silent advance matches scalar interpolation and looping phase");
            }
        }
        {
            Fixture device(exe),reference(exe);device.audio.set_output(&device.output);device.start();reference.start();
            check(device.output.mix(1024)==reference.audio.mix(1024),"device mixer initially matches canonical stereo PCM");
            device.audio.advance_silently(44100*16);
            check(device.audio.cue_finished(125),"game sound completion follows accelerated logical time");
            check(device.output.mix(1024)==reference.audio.mix(1024),"logical acceleration and natural completion do not speed up or cut audible sounds");
            device.audio.stop_cue(125);reference.audio.stop_cue(125);
            check(device.output.mix(256)==reference.audio.mix(256),"explicit stop still reaches a sound already completed in the logical clock");
            device.audio.subvolume(30);reference.audio.subvolume(30);
            check(device.output.mix(256)==reference.audio.mix(256),"volume commands preserve device BGM phase");
            device.audio.play_cue(126);reference.audio.play_cue(126);
            // The two clocks can reclaim different logical slots. Stop all
            // sounds explicitly before testing a fresh synchronized playback.
            device.audio.master_volume(false,0);reference.audio.master_volume(false,0);
            check(device.output.mix(256)==reference.audio.mix(256),"master mute clears output tails even after logical voice completion/reuse");
            device.audio.stop_bgm();reference.audio.stop_bgm();
            const auto stopped=device.output.mix(128);check(std::all_of(stopped.begin(),stopped.end(),[](auto v){return !v;}),"explicit BGM and effect stops silence the device");
            check(device.output.output_sample()==2944,"device time counts only requested output samples, not simulated samples");
        }
        {
            Fixture device(exe),reference(exe);device.audio.set_output(&device.output);device.audio.load_bgm(8);device.audio.play_bgm();reference.audio.load_bgm(8);reference.audio.play_bgm();
            for(unsigned speed:{1u,16u,1u,8u,1u}){
                device.audio.advance_silently(1000*speed);
                check(device.output.mix(1000)==reference.audio.mix(1000),"1x/16x/1x/8x transitions keep BGM pitch, phase and continuity on the device clock");
            }
        }
        {
            Fixture device(exe),reference(exe);device.audio.set_output(&device.output);
            device.audio.load_bgm(8);device.audio.play_bgm();reference.audio.load_bgm(8);reference.audio.play_bgm();
            check(device.output.mix(100)==reference.audio.mix(100),"loop-change fixture begins at the same device cursor");
            device.audio.advance_silently(4000);
            device.audio.set_bgm_loop(0);reference.audio.set_bgm_loop(0);
            check(device.output.mix(600)==reference.audio.mix(600),"changing loop mode preserves device position despite a different logical cursor");
            const auto stopped=device.output.mix(64);check(std::all_of(stopped.begin(),stopped.end(),[](auto v){return !v;}),"disabling BGM repeat finishes the remaining device tail once");
            device.audio.advance_silently(1000);const auto count=device.audio.events().size();device.audio.set_bgm_loop(7);
            check(device.memory.read(0x76e91c)==7&&!device.audio.bgm_playing()&&device.audio.events().size()==count,"stopped BGM changes raw loop policy without restarting");
        }
        std::cout<<"audio_output_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
