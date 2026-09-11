// Test the production AVAudioEngine graph in its offline rendering mode.
#include "../tools/native_audio_macos.mm"
#include <iostream>
using namespace fsb;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
std::shared_ptr<core::Pcm> tone(unsigned rate,unsigned frames){
    auto pcm=std::make_shared<core::Pcm>();pcm->rate=rate;pcm->channels=1;pcm->bits=16;pcm->byte_count=frames*2;
    for(unsigned i=0;i<frames;++i)pcm->samples.push_back(std::int16_t(std::sin(i*6.283185307179586*1000/rate)*12000));return pcm;
}
double energy(const std::vector<float>& pcm){double total=0;for(float sample:pcm)total+=sample*sample;return total/pcm.size();}
unsigned crossings(const std::vector<float>& pcm){unsigned count=0;for(unsigned i=2;i<pcm.size();i+=2)if(pcm[i-2]<=0&&pcm[i]>0)++count;return count;}
}
int main(){
    try{
        host::AppleAudio output(true);if(!output.healthy())throw std::runtime_error(output.error());
        std::array<core::AudioVoice,17> voices{};auto& voice=voices[16];voice.pcm=tone(22050,2205);voice.handle=1;voice.playing=true;voice.loop=true;
        output.reset(voices);output.set_active(true);check(output.healthy(),"native graph starts with source-rate buffers");
        const auto initial=output.render(4096);const auto count=crossings(initial);
        check(count>=80&&count<=100,"22050Hz source plays1000Hz tone at44100Hz device rate (no doubled pitch)");
        check(energy(initial)>0.01,"OS mixer renders nonzero audio");
        bool stereo=true;for(unsigned i=0;i<initial.size();i+=2)stereo&=initial[i]==initial[i+1];check(stereo,"mono source is sent equally to both output channels");
        output.apply(core::AudioEvent::Kind::Complete,16,voice);
        check(energy(output.render(4096))>0.01,"logical completion does not stop the native device loop");
        output.set_active(false);output.set_active(true);check(output.healthy()&&energy(output.render(1024))>0.01,"focus pause/resume preserves scheduled native audio");
        voice.volume=50;output.apply(core::AudioEvent::Kind::Volume,16,voice);output.render(1024);
        const auto quiet=energy(output.render(2048));check(quiet>0.005&&quiet<0.025,"original millibel volume maps to OS mixer gain");
        output.apply(core::AudioEvent::Kind::Stop,16,voice);output.render(1024);check(energy(output.render(1024))<1e-9,"explicit stop silences native player");
        voice.pcm=tone(48000,4800);voice.handle=2;voice.loop=false;voice.volume=100;
        output.apply(core::AudioEvent::Kind::Load,16,voice);output.apply(core::AudioEvent::Kind::Play,16,voice);
        check(output.healthy()&&energy(output.render(2048))>0.01,"voice can switch source sample rate while engine runs");
        output.apply(core::AudioEvent::Kind::Complete,16,voice);
        check(energy(output.render(1024))>0.01,"short sound retains normal audible duration after accelerated logical completion");
        output.render(4096);check(energy(output.render(1024))<1e-9,"nonlooping native sound ends naturally on device time");
        output.set_active(false);voice.pcm=tone(22050,2205);voice.handle=3;voice.loop=true;
        output.apply(core::AudioEvent::Kind::Load,16,voice);output.apply(core::AudioEvent::Kind::Play,16,voice);output.set_active(true);
        check(output.healthy()&&energy(output.render(2048))>0.01,"new source scheduled while paused starts correctly on resume");
        auto ramp=tone(44100,16384);for(unsigned i=0;i<ramp->frames();++i)ramp->samples[i]=std::int16_t(i+1);
        voice.pcm=ramp;voice.handle=4;voice.phase=0;voice.loop=true;
        output.apply(core::AudioEvent::Kind::Load,16,voice);output.apply(core::AudioEvent::Kind::Play,16,voice);output.render(4096);
        voice.loop=false;voice.phase=13000ull*44100; // Deliberately unrelated simulation cursor.
        output.apply(core::AudioEvent::Kind::Loop,16,voice);const auto tail=output.render(1024);
        check(output.healthy()&&tail[1024]>0.10f&&tail[1024]<0.20f,"native loop change resumes near device frame4096, not at zero or the logical cursor");
        for(unsigned i=0;i<4;++i)output.render(4096);
        check(energy(output.render(1024))<1e-9,"native loop disable ends after the remaining buffer tail");
        std::cout<<"native_audio_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
