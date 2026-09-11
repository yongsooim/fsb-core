#include "host_audio.hpp"
#import <AVFoundation/AVFoundation.h>
#include <algorithm>
#include <cmath>

namespace fsb::host {
namespace {
class AppleAudio final : public PlatformAudio {
public:
    explicit AppleAudio(bool offline=false):offline_(offline){
        @autoreleasepool {@try {
            engine_=[[AVAudioEngine alloc] init];
            AVAudioFormat* format=[[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:2];
            for(unsigned i=0;i<players_.size();++i){
                players_[i]=[[AVAudioPlayerNode alloc] init];[engine_ attachNode:players_[i]];
                [engine_ connect:players_[i] to:engine_.mainMixerNode fromBus:0 toBus:i format:format];rates_[i]=44100;
            }
            NSError* error=nil;
            if(offline_&&![engine_ enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline format:format maximumFrameCount:4096 error:&error])fail(error.localizedDescription);
            if(error_.empty()&&![engine_ startAndReturnError:&error])fail(error.localizedDescription);
            if(error_.empty())active_=true;
        }@catch(NSException* e){fail(e.reason);}}
    }
    ~AppleAudio(){@autoreleasepool {[engine_ stop];}}
    const char* name()const override{return "av-audio-engine";}
    bool healthy()const override{return error_.empty();}
    std::string error()const override{return error_;}
    void reset(const std::array<core::AudioVoice,17>& voices)override{
        @autoreleasepool {@try {
            for(auto player:players_)[player stop];handles_.fill(0);pending_.fill(std::nullopt);
            for(unsigned i=0;i<voices.size();++i){if(voices[i].pcm)apply(core::AudioEvent::Kind::Load,i,voices[i]);if(voices[i].playing)apply(core::AudioEvent::Kind::Play,i,voices[i]);}
        }@catch(NSException* e){fail(e.reason);}}
    }
    void apply(core::AudioEvent::Kind kind,unsigned slot,const core::AudioVoice& voice)override{
        if(!healthy())return;
        @autoreleasepool {@try {
            auto player=players_.at(slot);
            switch(kind){
            case core::AudioEvent::Kind::Load:[player stop];handles_[slot]=voice.handle;pending_[slot].reset();break;
            case core::AudioEvent::Kind::Play:{
                [player stop];handles_[slot]=voice.handle;
                // AVAudioEngine can invalidate a newly connected player's
                // schedule when starting after a graph change while paused.
                // Submit such play commands only after the engine resumes.
                if(!active_){pending_[slot]=voice;break;}
                auto full=buffer(voice.pcm);if(!full)return;
                if(rates_[slot]!=voice.pcm->rate){
                    [engine_ disconnectNodeOutput:player];
                    [engine_ connect:player to:engine_.mainMixerNode fromBus:0 toBus:slot format:full.format];rates_[slot]=voice.pcm->rate;
                }
                player.volume=std::pow(10.0f,core::Audio::volume_millibels(voice.volume)/2000.0f);
                const auto offset=voice.phase/44100;start_offsets_[slot]=offset;loops_[slot]=voice.loop;auto first=full;
                if(offset&&offset<full.frameLength){
                    first=[[AVAudioPCMBuffer alloc] initWithPCMFormat:full.format frameCapacity:full.frameLength-AVAudioFrameCount(offset)];first.frameLength=first.frameCapacity;
                    for(unsigned ch=0;ch<2;++ch)std::copy_n(full.floatChannelData[ch]+offset,first.frameLength,first.floatChannelData[ch]);
                }
                [player scheduleBuffer:first atTime:nil options:(voice.loop&&first==full?AVAudioPlayerNodeBufferLoops:0) completionHandler:nil];
                if(voice.loop&&first!=full)[player scheduleBuffer:full atTime:nil options:AVAudioPlayerNodeBufferLoops completionHandler:nil];
                [player play];break;
            }
            case core::AudioEvent::Kind::Stop:if(handles_[slot]==voice.handle){[player stop];pending_[slot].reset();}break;
            case core::AudioEvent::Kind::Volume:if(handles_[slot]==voice.handle){player.volume=std::pow(10.0f,core::Audio::volume_millibels(voice.volume)/2000.0f);if(pending_[slot])pending_[slot]->volume=voice.volume;}break;
            case core::AudioEvent::Kind::Loop:{
                if(handles_[slot]!=voice.handle)break;
                if(pending_[slot]){pending_[slot]->loop=voice.loop;break;}
                if(!player.playing||!voice.pcm)break;
                auto node_time=player.lastRenderTime;
                auto player_time=node_time?[player playerTimeForNodeTime:node_time]:nil;
                const auto elapsed=player_time?std::uint64_t(std::max<AVAudioFramePosition>(0,player_time.sampleTime)):0;
                auto cursor=start_offsets_[slot]+elapsed;
                if(!loops_[slot]&&cursor>=voice.pcm->frames())break;
                cursor%=voice.pcm->frames();auto resumed=voice;resumed.phase=cursor*44100;
                apply(core::AudioEvent::Kind::Play,slot,resumed);break;
            }
            case core::AudioEvent::Kind::Complete:break;
            }
        }@catch(NSException* e){fail(e.reason);}}
    }
    void set_active(bool active)override{
        if(!healthy())return;
        @autoreleasepool {@try {
            if(active_&&!engine_.running){fail(@"audio engine stopped after device/configuration change");return;}
            if(active!=active_){
                if(active){NSError* error=nil;if(![engine_ startAndReturnError:&error]){fail(error.localizedDescription);return;}}
                else [engine_ pause];active_=active;
                if(active)for(unsigned i=0;i<pending_.size()&&healthy();++i)if(pending_[i]){const auto voice=*pending_[i];pending_[i].reset();apply(core::AudioEvent::Kind::Play,i,voice);}
            }
        }@catch(NSException* e){fail(e.reason);}}
    }
    // Offline renderer exercises the identical OS player/mixer graph without
    // producing speaker output. Only the platform test uses this entry point.
    std::vector<float> render(unsigned frames){
        @autoreleasepool {
            AVAudioPCMBuffer* out=[[AVAudioPCMBuffer alloc] initWithPCMFormat:engine_.manualRenderingFormat frameCapacity:frames];NSError* error=nil;
            const auto status=[engine_ renderOffline:frames toBuffer:out error:&error];
            if(status!=AVAudioEngineManualRenderingStatusSuccess)throw std::runtime_error(error?error.localizedDescription.UTF8String:"offline render failed");
            std::vector<float> result(out.frameLength*2);for(unsigned i=0;i<out.frameLength;++i)for(unsigned ch=0;ch<2;++ch)result[i*2+ch]=out.floatChannelData[ch][i];return result;
        }
    }
private:
    struct Cached {std::shared_ptr<const core::Pcm> pcm;AVAudioPCMBuffer* buffer;};
    AVAudioEngine* engine_=nil;
    std::array<AVAudioPlayerNode*,17> players_{};
    std::array<unsigned,17> rates_{},handles_{};
    std::array<std::uint64_t,17> start_offsets_{};
    std::array<bool,17> loops_{};
    std::array<std::optional<core::AudioVoice>,17> pending_{};
    std::map<const core::Pcm*,Cached> buffers_;
    bool offline_=false,active_=false;
    std::string error_;
    void fail(NSString* reason){error_=reason?reason.UTF8String:"AVAudioEngine failure";[engine_ stop];}
    AVAudioPCMBuffer* buffer(const std::shared_ptr<const core::Pcm>& pcm){
        if(!pcm){fail(@"missing PCM asset");return nil;}
        if(const auto it=buffers_.find(pcm.get());it!=buffers_.end())return it->second.buffer;
        AVAudioFormat* format=[[AVAudioFormat alloc] initStandardFormatWithSampleRate:pcm->rate channels:2];
        AVAudioPCMBuffer* out=[[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:AVAudioFrameCount(pcm->frames())];
        if(!out){fail(@"native audio buffer allocation failed");return nil;}out.frameLength=out.frameCapacity;
        for(unsigned i=0;i<out.frameLength;++i)for(unsigned ch=0;ch<2;++ch)out.floatChannelData[ch][i]=pcm->samples[std::size_t(i)*pcm->channels+(pcm->channels==1?0:ch)]/32768.0f;
        buffers_.emplace(pcm.get(),Cached{pcm,out});return out;
    }
};
}
std::unique_ptr<PlatformAudio> make_platform_audio(std::string& reason){
    auto output=std::make_unique<AppleAudio>();if(!output->healthy()){reason=output->error();return {};}return output;
}
} // namespace fsb::host
