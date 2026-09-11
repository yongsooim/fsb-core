#pragma once
#include "primitives.hpp"

namespace fsb::core {
class Audio;
class RecoveredBattle;
// Original x86 entry points. Keep guest calling conventions in this adapter;
// Audio itself exposes semantic operations without registers or return stacks.
namespace original_audio {
inline constexpr Address play_cue_alias=0x4317f4;
inline constexpr Address bgm_loop_mode=0x433573;
inline constexpr Address select_bgm=0x4335c0;
inline constexpr Address fade_busy=0x433788;
inline constexpr Address fade_volume=0x433794;
inline constexpr Address transition_bgm=0x4337f4;
inline constexpr Address play_pcm_slot=0x434584;
inline constexpr Address cue_master_volume=0x43489c;
inline constexpr Address bgm_master_volume=0x434d15;
inline constexpr Address play_cue=0x435373;
inline constexpr Address stop_cue=0x4353cb;
inline constexpr Address cancel_subvolume_fade=0x42c711;
inline constexpr Address apply_bgm_cue=0x431800;
inline constexpr Address load_bgm=0x4334ef;
inline constexpr Address play_bgm=0x43356e;
inline constexpr Address apply_subvolume=0x43357f;
inline constexpr Address snapshot_subvolume=0x4335a2;
inline constexpr Address seek_bgm=0x4335b4;
inline constexpr Address bgm_playing=0x433614;
inline constexpr Address cue_finished=0x4353f0;
// False means this adapter did not handle the address; no state is changed.
bool dispatch(Address entry,RecoveredBattle& call,Audio& audio);
}
} // namespace fsb::core
