#include "fsb_core/original_audio.hpp"
#include "fsb_core/runtime.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
}
int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;const std::filesystem::path assets=argv[1];
        Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));auto& m=runtime.memory;auto& audio=runtime.audio;auto& call=runtime.battle.recovered;
        for(unsigned id:{8u,49u})audio.register_wave(true,id,fsb::lab::read(assets/"audio"/Audio::resource_name(m,true,id)));
        for(unsigned id:{125u,126u}){auto p=assets/"se_event"/Audio::resource_name(m,false,id);p.replace_extension(".wav");audio.register_wave(false,id,fsb::lab::read(p));}
        call.invoke(original_audio::fade_busy);const auto empty_sp=call.r[4];
        const auto invoke=[&](Address entry,std::vector<std::uint32_t> args){
            const auto result=call.invoke(entry,args);
            check(call.r[4]==empty_sp+args.size()*4,"original audio address routes through runtime and preserves original callee stack cleanup");return result;
        };
        invoke(original_audio::select_bgm,{8,7,0});
        check(m.read(0x769654)==8&&m.read(0x76e91c)==7&&!audio.bgm_playing(),"select without play keeps raw loop flags and does not start BGM");
        auto loop_events=audio.events().size();
        check(invoke(original_audio::bgm_loop_mode,{0})==0&&m.read(0x76e91c)==0&&!audio.bgm_playing()&&audio.events().size()==loop_events,"loop thunk changes a stopped BGM without starting or seeking it");
        audio.play_bgm();audio.advance_silently(100);loop_events=audio.events().size();
        check(invoke(original_audio::bgm_loop_mode,{7})==0&&m.read(0x76e91c)==7&&audio.bgm_playing()&&audio.events().size()==loop_events+1&&audio.events().back().kind==AudioEvent::Kind::Loop,"live loop change emits only a cursor-preserving mode command");
        loop_events=audio.events().size();invoke(original_audio::bgm_loop_mode,{7});check(audio.events().size()==loop_events,"same BGM loop mode is an exact no-op");
        const auto output=m.allocate_zeroed(4);m.write(output,0xabcdef01);auto event_count=audio.events().size();
        check(invoke(original_audio::play_pcm_slot,{125,output})==0&&m.read(output)==0xabcdef01&&audio.events().size()==event_count&&!m.read(0x76f258+125*4),"raw PCM play does not lazy-load an unloaded source or write its out pointer");
        invoke(original_audio::play_cue_alias,{125});invoke(original_audio::play_cue,{126});
        check(!audio.cue_finished(125)&&!audio.cue_finished(126),"both original sound entry points reach the same portable audio service");
        const auto requests=m.read(0x76fa58+125*4),timestamp=m.read(0x770258+125*4),last_cue=m.read(0x76ea58+125*4);audio.set_time(333);
        check(invoke(original_audio::play_pcm_slot,{125,output})==0&&m.read(output)==m.read(0x76981c+125*40)&&m.read(output)!=0xabcdef01,"raw PCM play publishes the newly allocated playback handle");
        check(m.read(0x76fa58+125*4)==requests&&m.read(0x770258+125*4)==timestamp&&m.read(0x76ea58+125*4)==last_cue,"raw PCM play preserves cue request accounting and the caller-owned last handle");
        m.write(0x76e8fc,6);m.write(output,0xabcdef01);const auto next_handle=m.read(0x76e904);
        invoke(original_audio::play_pcm_slot,{125,output});check(m.read(output)==0xabcdef01&&m.read(0x76e904)==next_handle,"original replay branch reuses the buffer and does not overwrite out_handle");
        m.write(0x76e8fc,4);m.write(0x76e908,0);event_count=audio.events().size();
        check(invoke(original_audio::play_pcm_slot,{125,output})==0&&m.read(output)==0xabcdef01&&audio.events().size()==event_count,"disabled PCM is a quiet success without a new handle or play event");m.write(0x76e908,1);
        invoke(original_audio::play_pcm_slot,{125,0});check(m.read(0x76fa58+125*4)==requests,"raw PCM accepts the null out_handle used by treasure pickup");
        invoke(original_audio::stop_cue,{126});check(audio.cue_finished(126),"original stop entry follows the latest cue handle");
        invoke(original_audio::cue_master_volume,{0xffffffffu});check(m.read(0x76e90c)==0,"signed negative master volume is preserved through argument conversion");
        invoke(original_audio::bgm_master_volume,{101});check(m.read(0x76e914)==100,"BGM master volume retains clamping");
        invoke(original_audio::fade_volume,{10,50});check(invoke(original_audio::fade_busy,{})==1,"fade-state query returns the original boolean result");
        invoke(original_audio::transition_bgm,{0,0,49,100,0,100});check(m.read(0x769654)==49&&audio.bgm_playing(),"six-argument BGM transition reaches the semantic core operation");
        invoke(original_audio::select_bgm,{0,1,1});check(!audio.bgm_playing(),"out-of-range BGM still stops playback");
        const auto registers=call.r;const auto events=audio.events().size();
        check(!original_audio::dispatch(0x401d66,call,audio)&&call.r==registers&&audio.events().size()==events,"unowned addresses are declined without mutating registers or audio");
        {
            struct Sink final:AudioSink {
                std::array<AudioVoice,17> voices{};unsigned commands=0;
                void reset(const std::array<AudioVoice,17>& value)override{voices=value;}
                void apply(AudioEvent::Kind,unsigned slot,const AudioVoice& value)override{voices[slot]=value;++commands;}
            } sink;
            audio.set_output(&sink);invoke(original_audio::load_bgm,{8});invoke(original_audio::play_bgm,{});
            check(invoke(original_audio::bgm_playing,{})==1,"native BGM playing query missing");
            const auto frame_bytes=sink.voices[16].pcm->channels*sink.voices[16].pcm->bits/8;
            invoke(original_audio::seek_bgm,{frame_bytes*17});check(sink.voices[16].phase==17*44100ull&&sink.voices[16].playing,"source-byte seek did not reach the device command boundary");
            audio.advance_silently(100);invoke(original_audio::load_bgm,{8});check(sink.voices[16].phase==0&&sink.voices[16].playing,"loading the current playing BGM failed to publish its rewind");
            audio.stop_bgm();const auto commands=sink.commands;invoke(original_audio::seek_bgm,{frame_bytes*31});check(sink.commands==commands,"stopped seek unexpectedly started device playback");
            invoke(original_audio::play_bgm,{});check(sink.voices[16].phase==31*44100ull,"next Play lost the stopped seek position");
            const auto count=sink.commands;m.write(0x76ea28,0x12345678);
            check(invoke(original_audio::seek_bgm,{m.read(0x7697e8)+1})==0x12345678&&sink.commands==count,"original out-of-range seek diagnostic changed HRESULT or playback");
            invoke(original_audio::apply_subvolume,{23});check(invoke(original_audio::snapshot_subvolume,{})==23,"native subvolume snapshot mismatch");
            audio.fade(10,23,50,true);invoke(original_audio::cancel_subvolume_fade,{});check(!m.read(0x768a84)&&m.read(0x76e918)==50,"native fade cancel did not preserve its original target");
            invoke(original_audio::apply_bgm_cue,{8});check(invoke(original_audio::cue_finished,{126})==1,"native cue-completion query missing");
            audio.set_output(nullptr);
        }
        {
            Runtime full(fsb::lab::read(assets/"FLYINGSB.EXE"));auto& fm=full.memory;auto& sound=full.audio;
            auto path=assets/"se_event"/Audio::resource_name(fm,false,125);path.replace_extension(".wav");sound.register_wave(false,125,fsb::lab::read(path));
            for(unsigned i=0;i<16;++i)sound.play_cue(125);
            const auto full_events=sound.events().size();const auto full_handle=fm.read(0x76e904),last=fm.read(0x76ea58+125*4),out=fm.allocate_zeroed(4);fm.write(out,0xabcdef01);
            check(full.battle.recovered.invoke(original_audio::play_pcm_slot,{125,out})==0x8898000eu,"full equal-priority PCM pool returns original HRESULT without aborting gameplay");
            check(fm.read(out)==0xabcdef01&&fm.read(0x76e904)==full_handle&&sound.events().size()==full_events,"failed PCM request does not publish or reclaim a voice");
            sound.play_cue(125);
            check(fm.read(0x76fa58+125*4)==17&&fm.read(0x76ea58+125*4)==last,"sound helper counts failed requests but preserves its last playback handle");
            sound.advance_silently(441000);
            check(full.battle.recovered.invoke(original_audio::play_pcm_slot,{125,out})==0&&fm.read(out)!=0xabcdef01,"completed PCM voices become available after exhaustion");
        }
        std::cout<<"original_audio_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
