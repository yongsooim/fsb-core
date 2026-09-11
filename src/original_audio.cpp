#include "fsb_core/original_audio.hpp"
#include "fsb_core/audio.hpp"
#include "fsb_core/recovered_battle.hpp"

namespace fsb::core::original_audio {
bool dispatch(Address entry,RecoveredBattle& call,Audio& audio){
    const auto arg=[&](unsigned i){return call.argument(i);};
    // Callee stack cleanup is checked against actual RET immediates in
    // reference/original-audio-services.json, not inferred from C prototypes.
    switch(entry){
    case cancel_subvolume_fade:audio.cancel_fade();call.result(0);return true;
    case apply_bgm_cue:audio.apply_bgm(arg(0));call.result(0,4);return true;
    case load_bgm:audio.load_bgm(arg(0));call.result(0,4);return true;
    case play_bgm:audio.play_bgm();call.result(0);return true;
    case apply_subvolume:audio.subvolume(arg(0));call.result(0,4);return true;
    case snapshot_subvolume:call.result(audio.subvolume_percent());return true;
    case seek_bgm:audio.seek_bgm(arg(0));call.result(audio.last_result(),4);return true;
    case bgm_playing:call.result(audio.bgm_playing());return true;
    case cue_finished:call.result(audio.cue_finished(arg(0)),4);return true;
    case bgm_loop_mode:audio.set_bgm_loop(arg(0));call.result(audio.last_result(),4);return true;
    case transition_bgm:
        audio.transition_bgm(signed32(arg(0)),signed32(arg(1)),arg(2),signed32(arg(3)),signed32(arg(4)),signed32(arg(5)));call.result(0,24);return true;
    case cue_master_volume:case bgm_master_volume:
        audio.master_volume(entry==bgm_master_volume,signed32(arg(0)));call.result(0,4);return true;
    case play_cue_alias:case play_cue:audio.play_cue(arg(0));call.result(0,4);return true;
    case play_pcm_slot:{const auto output=arg(1);if(const auto handle=audio.play_cached_cue(arg(0)))if(output)call.write(output,*handle);call.result(audio.last_result(),8);return true;}
    case stop_cue:audio.stop_cue(arg(0));call.result(0,4);return true;
    case fade_busy:call.result(audio.global_fade_busy());return true;
    case fade_volume:audio.fade_volume(signed32(arg(0)),signed32(arg(1)));call.result(0,8);return true;
    case select_bgm:audio.select_bgm(arg(0),arg(1),arg(2)!=0);call.result(0,12);return true;
    default:return false;
    }
}
} // namespace fsb::core::original_audio
