#include "host_audio.hpp"
#include <algorithm>
#include <iostream>

namespace fsb::host {
#ifndef FSB_NATIVE_APPLE_AUDIO
std::unique_ptr<PlatformAudio> make_platform_audio(std::string& reason){reason="no native audio backend for this platform";return {};}
#endif
HostAudio::HostAudio(bool software,AudioFactory factory){
    previous_ms_=SDL_GetTicks();
    if(!software){
        try{native_=factory(fallback_reason_);}catch(const std::exception& e){fallback_reason_=e.what();}
    }
    if(!native_)use_fallback(fallback_reason_);
}
HostAudio::~HostAudio(){native_.reset();if(stream_)SDL_DestroyAudioStream(stream_);}
void HostAudio::use_fallback(const std::string& reason){
    fallback_reason_=reason;native_.reset();previous_ms_=SDL_GetTicks();fraction_=0;
    if(!stream_){
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)){
            const SDL_AudioSpec desired{SDL_AUDIO_S16,2,44100};
            stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&desired,nullptr,nullptr);
        }
        if(!stream_){if(!fallback_reason_.empty())fallback_reason_+="; ";fallback_reason_+=SDL_GetError();}
    }
    if(!fallback_reason_.empty())std::cerr<<"Audio fallback: "<<fallback_reason_<<" -> "<<name()<<'\n';
}
void HostAudio::advance_shadow(std::uint64_t now){
    const auto elapsed=now-previous_ms_;previous_ms_=now;
    if(!native_||!active_||elapsed>1000)return;
    const auto samples=fraction_+elapsed*44100;fraction_=samples%1000;
    fallback_.advance_silently(unsigned(samples/1000));
}
void HostAudio::reset(const std::array<core::AudioVoice,17>& voices){
    fallback_.reset(voices);previous_ms_=SDL_GetTicks();fraction_=0;
    if(native_){native_->reset(voices);if(!native_->healthy())use_fallback(native_->error());}
}
void HostAudio::apply(core::AudioEvent::Kind kind,unsigned slot,const core::AudioVoice& voice){
    advance_shadow(SDL_GetTicks());fallback_.apply(kind,slot,voice);
    if(native_){native_->apply(kind,slot,voice);if(!native_->healthy())use_fallback(native_->error());}
}
void HostAudio::pump(bool active,bool stalled){
    advance_shadow(SDL_GetTicks());
    if(native_){
        native_->set_active(active);
        if(!native_->healthy())use_fallback(native_->error());
        else{active_=active;return;}
    }
    if(!stream_){active_=active;return;}
    if(active!=active_||stalled){SDL_PauseAudioStreamDevice(stream_);SDL_ClearAudioStream(stream_);started_=false;}
    active_=active;if(!active)return;
    constexpr unsigned target_frames=2048,frame_bytes=2*sizeof(std::int16_t);
    // The stream is fed in its own input format, so queued bytes are the frames
    // this host handed over and the device has not consumed yet.
    const auto queued=unsigned(std::max(0,SDL_GetAudioStreamQueued(stream_)))/frame_bytes;
    if(queued<target_frames){
        const auto count=target_frames-queued;const auto pcm=fallback_.mix(count);
        if(!SDL_PutAudioStreamData(stream_,pcm.data(),int(pcm.size()*sizeof(std::int16_t))))throw std::runtime_error(SDL_GetError());
        software_frames_+=count;
    }
    if(!started_){SDL_ResumeAudioStreamDevice(stream_);started_=true;}
}
} // namespace fsb::host
