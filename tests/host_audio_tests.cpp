#include "host_audio.hpp"
#include <iostream>
using namespace fsb;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
struct FakeNative:host::PlatformAudio {
    std::shared_ptr<bool> failed;explicit FakeNative(std::shared_ptr<bool> value):failed(value){}
    void reset(const std::array<core::AudioVoice,17>&)override{}
    void apply(core::AudioEvent::Kind,unsigned,const core::AudioVoice&)override{}
    void set_active(bool)override{}
    bool healthy()const override{return !*failed;}
    const char* name()const override{return "fake-native";}
    std::string error()const override{return "injected device failure";}
};
}
int main(){
    try{
        // SDL3 always has the timer available; the audio subsystem is started
        // by the fallback itself when it opens a device.
        if(!SDL_Init(0))throw std::runtime_error(SDL_GetError());
        std::array<core::AudioVoice,17> voices{};auto pcm=std::make_shared<core::Pcm>();pcm->rate=44100;pcm->channels=1;pcm->bits=16;pcm->samples.assign(4096,1000);pcm->byte_count=8192;
        voices[16].pcm=pcm;voices[16].playing=voices[16].loop=true;voices[16].handle=1;
        {
            auto failed=std::make_shared<bool>(false);
            host::HostAudio output(false,[failed](std::string&){return std::make_unique<FakeNative>(failed);});output.reset(voices);output.pump(true);
            check(std::string(output.name())=="fake-native"&&output.software_frames()==0,"native backend is preferred without synthesizing software PCM");
            *failed=true;output.apply(core::AudioEvent::Kind::Volume,16,voices[16]);output.pump(true);
            check(std::string(output.name())=="sdl-software"&&output.software_frames()>0,"runtime device failure automatically switches to usable SDL fallback");
            check(output.fallback_reason()=="injected device failure","fallback retains the actual native failure reason");
            output.pump(false);const auto count=output.software_frames();output.pump(false);check(output.software_frames()==count,"inactive fallback does not generate more PCM");
            output.pump(true);check(output.software_frames()>count,"fallback resumes after focus returns");
        }
        {
            host::HostAudio output(false,[](std::string& reason)->std::unique_ptr<host::PlatformAudio>{reason="native unavailable";return {};});output.reset(voices);output.pump(true);
            check(std::string(output.name())=="sdl-software"&&output.software_frames()>0,"native initialization failure falls back before first playback");
        }
        {
            bool called=false;host::HostAudio output(true,[&](std::string&)->std::unique_ptr<host::PlatformAudio>{called=true;return {};});output.reset(voices);output.pump(true);
            check(!called&&output.software_frames()>0,"explicit software selection skips native initialization");
        }
        core::AudioOutput scalar,shadow;scalar.reset(voices);shadow.reset(voices);scalar.mix(12345);shadow.advance_silently(12345);
        check(scalar.mix(256)==shadow.mix(256),"fallback shadow retains the device-time phase without PCM work");
        SDL_Quit();std::cout<<"host_audio_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 2;}
}
