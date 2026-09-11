#include "fsb_core/audio.hpp"
#include "audio_gain.hpp"
#include <algorithm>

namespace fsb::core {
void Audio::set_output(AudioSink* output){
    output_=output;if(output_)output_->reset(voices_);
}
void Audio::advance_silently(unsigned count){
    ready();if(count>44100*60)throw Fault(count,"audio output batch exceeds60 seconds");
    // The scalar mixer finishes voices in sample order, then slot order.
    // Preserve that ordering as well as its final (possibly overshot) phase.
    std::array<std::pair<std::uint64_t,unsigned>,17> completed{};unsigned size=0;
    for(unsigned slot=0;slot<voices_.size();++slot){
        auto& v=voices_[slot];if(!v.playing||!count)continue;
        const auto end=v.pcm->frames()*44100ull;
        if(v.loop)v.phase=(v.phase+std::uint64_t(count)*v.pcm->rate)%end;
        else{
            const auto remaining=(end-v.phase+v.pcm->rate-1)/v.pcm->rate;
            v.phase+=std::min<std::uint64_t>(count,remaining)*v.pcm->rate;
            if(remaining<=count){v.playing=false;completed[size++]={remaining,slot};}
        }
    }
    std::sort(completed.begin(),completed.begin()+size);
    for(unsigned i=0;i<size;++i){emit(AudioEvent::Kind::Complete,completed[i].second);events_.back().output_sample=output_sample_+completed[i].first;}
    output_sample_+=count;
}
void AudioOutput::reset(const std::array<AudioVoice,17>& voices){voices_=voices;output_sample_=0;}
void AudioOutput::advance_silently(unsigned count){
    if(count>44100*60)throw Fault(count,"device audio batch exceeds60 seconds");
    for(auto& v:voices_){
        if(!v.playing||!count)continue;const auto end=v.pcm->frames()*44100ull;
        if(v.loop)v.phase=(v.phase+std::uint64_t(count)*v.pcm->rate)%end;
        else{const auto remaining=(end-v.phase+v.pcm->rate-1)/v.pcm->rate;v.phase+=std::min<std::uint64_t>(count,remaining)*v.pcm->rate;if(remaining<=count)v.playing=false;}
    }
    output_sample_+=count;
}
void AudioOutput::apply(AudioEvent::Kind kind,unsigned slot,const AudioVoice& voice){
    auto& output=voices_[slot];
    switch(kind){
    case AudioEvent::Kind::Load:case AudioEvent::Kind::Play:output=voice;break;
    case AudioEvent::Kind::Stop:if(output.handle==voice.handle)output.playing=false;break;
    case AudioEvent::Kind::Volume:if(output.handle==voice.handle)output.volume=voice.volume;break;
    case AudioEvent::Kind::Loop:if(output.handle==voice.handle)output.loop=voice.loop;break;
    case AudioEvent::Kind::Complete:break; // The device finishes at its own sample rate.
    }
}
std::vector<std::int16_t> AudioOutput::mix(unsigned count){
    if(count>44100*60)throw Fault(count,"device audio batch exceeds60 seconds");
    std::vector<std::int16_t> output(std::size_t(count)*2);
    for(unsigned frame=0;frame<count;++frame){
        std::int64_t sum[2]{};
        for(auto& v:voices_){
            if(!v.playing)continue;
            const auto& pcm=*v.pcm;const auto index=v.phase/44100,fraction=v.phase%44100;
            const auto next=index+1<pcm.frames()?index+1:v.loop?0:index;
            for(unsigned channel=0;channel<2;++channel){
                const auto ch=pcm.channels==1?0:channel;
                const auto a=pcm.samples[index*pcm.channels+ch],b=pcm.samples[next*pcm.channels+ch];
                const auto sample=std::int64_t(a)+(std::int64_t(b)-a)*std::int64_t(fraction)/44100;
                sum[channel]+=sample*audio_gain_q24[v.volume]/16777216;
            }
            v.phase+=pcm.rate;
            if(v.phase>=pcm.frames()*44100ull){if(v.loop)v.phase%=pcm.frames()*44100ull;else v.playing=false;}
        }
        for(unsigned channel=0;channel<2;++channel)output[frame*2+channel]=std::int16_t(std::clamp<std::int64_t>(sum[channel],-32768,32767));
    }
    output_sample_+=count;return output;
}
} // namespace fsb::core
