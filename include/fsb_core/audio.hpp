#pragma once
#include "primitives.hpp"
#include <map>
#include <memory>

namespace fsb::core {
struct Pcm {
    unsigned rate = 0, channels = 0, bits = 0, byte_count = 0;
    std::vector<std::int16_t> samples;
    std::size_t frames() const { return samples.size() / channels; }
    static Pcm wave(const std::vector<std::uint8_t>& bytes);
};
struct AudioEvent {
    enum class Kind { Load, Play, Stop, Volume, Complete, Loop };
    Kind kind;
    bool bgm;
    unsigned id, handle;
    std::uint64_t output_sample;
    int millibels;
};
struct AudioVoice {
    std::shared_ptr<const Pcm> pcm;
    unsigned id=0,handle=0,priority=0,volume=100;
    bool playing=false,loop=false;
    std::uint64_t phase=0;
};
// Command boundary only: no device API or wall clock enters the simulation.
class AudioSink {
public:
    virtual ~AudioSink()=default;
    virtual void reset(const std::array<AudioVoice,17>& voices)=0;
    virtual void apply(AudioEvent::Kind kind,unsigned slot,const AudioVoice& voice)=0;
};
class Audio {
public:
    explicit Audio(Memory& memory) : memory_(memory) {}
    void initialize(std::uint32_t runtime_flags = 4); // Original0x448739 enables eligible PCM-buffer reclaim, not "four channels".
    void register_wave(bool bgm, unsigned id, const std::vector<std::uint8_t>& bytes);
    static std::string resource_name(const Memory&, bool bgm, unsigned id);
    void set_time(std::uint32_t now) { now_ = now; }
    void load_bgm(unsigned id);
    void select_bgm(unsigned id,std::uint32_t loop_mode,bool play_now); // Original0x4335c0.
    void set_bgm_loop(std::uint32_t mode); //433573/434dad; preserves the current playback position.
    void seek_bgm(std::uint32_t byte_position); //4335b4/434f04; source PCM byte units.
    unsigned subvolume_percent() const { return memory_.read(0x76e918); }
    void play_bgm();
    void stop_bgm();
    void apply_bgm(unsigned cue);
    void subvolume(std::uint32_t percent);
    void master_volume(bool bgm,int percent);
    void cancel_fade();
    void fade(std::uint32_t duration, std::uint32_t start, std::uint32_t target, bool keep_playing);
    void tick_fade(Address object, unsigned jobs);
    void fade_volume(int frames,int target); // Original433794 global fade worker.
    void transition_bgm(int out_frames,int out_volume,unsigned next_id,int in_volume,int in_frames,int target);
    void tick_global_fade(unsigned jobs); // 43362b; independent of opcodeC2's object.
    bool global_fade_busy()const{return memory_.read(0x76965c)!=0;}
    void play_cue(unsigned id);
    std::optional<unsigned> play_cached_cue(unsigned id); //434584; new handle only, no resource load/cache accounting.
    std::uint32_t last_result()const{return memory_.read(0x76ea28);}
    void stop_cue(unsigned id);
    bool cue_finished(unsigned id) const;
    bool bgm_playing() const;
    // Pure sample clock: produces interleaved stereo44100Hz signed16 PCM.
    // Integer linear sample-rate conversion is a portable output policy;
    // equivalence with the original DirectSound driver's DSP is not yet proved.
    std::vector<std::int16_t> mix(unsigned frames);
    // Same logical voice positions/completion events, without synthesizing PCM.
    void advance_silently(unsigned frames);
    // Borrowed output sink; the host supplies device-time samples independently.
    void set_output(AudioSink* output);
    const std::vector<AudioEvent>& events() const { return events_; }
    std::uint64_t output_sample() const { return output_sample_; }
    static int volume_millibels(unsigned percent);
private:
    using Voice=AudioVoice;
    Memory& memory_;
    std::map<unsigned, std::shared_ptr<const Pcm>> bgm_assets_, cue_assets_;
    std::array<Voice, 17> voices_{};
    std::vector<AudioEvent> events_;
    std::uint64_t output_sample_ = 0;
    std::uint32_t now_ = 0;
    bool initialized_ = false;
    AudioSink* output_ = nullptr;
    void ready() const;
    void emit(AudioEvent::Kind, unsigned slot);
    void start_voice(unsigned slot, bool loop, unsigned priority);
    void create_voice(unsigned slot, unsigned id, std::shared_ptr<const Pcm> pcm);
    std::optional<unsigned> find_handle(unsigned handle) const;
    void trim_cache();
};
// Portable device-side mixer. Explicit play/stop/volume commands follow the
// game, while sample positions and natural completion follow the audio device.
// It never writes guest memory or feeds completion back into the simulation.
class AudioOutput : public AudioSink {
public:
    void reset(const std::array<AudioVoice,17>& voices)override;
    void apply(AudioEvent::Kind kind,unsigned slot,const AudioVoice& voice)override;
    std::vector<std::int16_t> mix(unsigned frames);
    void advance_silently(unsigned frames);
    std::uint64_t output_sample()const{return output_sample_;}
private:
    std::array<AudioVoice,17> voices_{};
    std::uint64_t output_sample_=0;
};
} // namespace fsb::core
