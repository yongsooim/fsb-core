#pragma once
#include "fsb_core/audio.hpp"
#include <SDL3/SDL.h>
#include <functional>

namespace fsb::host {
class PlatformAudio : public core::AudioSink {
public:
    virtual void set_active(bool active)=0;
    virtual bool healthy()const=0;
    virtual const char* name()const=0;
    virtual std::string error()const=0;
};
std::unique_ptr<PlatformAudio> make_platform_audio(std::string& reason);
using AudioFactory=std::function<std::unique_ptr<PlatformAudio>(std::string&)>;

class HostAudio : public core::AudioSink {
public:
    explicit HostAudio(bool software=false,AudioFactory factory=make_platform_audio);
    ~HostAudio();
    HostAudio(const HostAudio&)=delete;
    HostAudio& operator=(const HostAudio&)=delete;
    void reset(const std::array<core::AudioVoice,17>& voices)override;
    void apply(core::AudioEvent::Kind kind,unsigned slot,const core::AudioVoice& voice)override;
    void pump(bool active,bool stalled=false);
    const char* name()const{return native_?native_->name():stream_?"sdl-software":"unavailable";}
    std::uint64_t software_frames()const{return software_frames_;}
    const std::string& fallback_reason()const{return fallback_reason_;}
private:
    core::AudioOutput fallback_;
    std::unique_ptr<PlatformAudio> native_;
    SDL_AudioStream* stream_=nullptr;
    bool active_=false,started_=false;
    std::uint64_t previous_ms_=0,fraction_=0,software_frames_=0;
    std::string fallback_reason_;
    void use_fallback(const std::string& reason);
    void advance_shadow(std::uint64_t now);
};
} // namespace fsb::host
