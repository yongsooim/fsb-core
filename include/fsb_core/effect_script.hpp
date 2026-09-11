#pragma once
#include "primitives.hpp"
#include <functional>

namespace fsb::core {
namespace effect_script {
enum class Command : std::uint8_t {
    Stop, Restart, SaveAppearance, RestoreAppearance,
    SetCharacterAnimation, SetIndexedAnimation, SetEffectAnimation, SetFrame, Wait,
    SaveTransform, RestoreTransform, TranslatePosition, TranslateDrawOffset,
    AdvanceController, NotifyController, SetFlags, ClearFlags, PlayCue, StopCue,
    SetTimingMode
};
// These fields alias callback-private actor storage when no effect script owns it.
inline constexpr Address script=0x150, wait=0x154, cursor=0x158, saved_appearance=0x15c;
inline constexpr Address saved_transform=0x3c;
inline constexpr unsigned transform_bytes=0x24;
inline constexpr std::uint32_t running_flag=0x20000, visible_flag=0x40;
inline constexpr Address yielded=0x773f70, controller=0x805680, timing_mode=0x5d3a14;
}

// The effect language has twenty commands; it is separate from the event VM.
// Execution uses ordinary C++ control flow, with one existing state store.
class EffectScript {
public:
    explicit EffectScript(Memory& memory):memory_(memory){}
    std::function<void(Address callback,Address actor)> invoke_actor;
    std::function<void(unsigned cue)> play_cue,stop_cue;
    void start(Address actor,Address script);
    void stop(Address actor);
    void tick();
    void execute(Address actor,effect_script::Command command);
    Address notify_controller(Address actor,std::int32_t state);
private:
    Memory& memory_;
    void run(Address actor,bool honor_wait);
    void next(Address actor);
    void wait(Address actor,unsigned frames);
};
}
