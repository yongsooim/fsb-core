#pragma once
#include "fsb_core/primitives.hpp"
#include "fsb_core/symbols.hpp"
#include <functional>

namespace fsb::core::combat {
namespace handler_flow {
inline constexpr Address completion_latch=0x5d2698, deferred_enemy_action=0x5d269c;
inline constexpr Address active_handler=0x805680, action_banner=0x8064f4;
// All observed readers gate action-name presentation, not PCM playback.
inline constexpr Address action_banner_enabled=0x773014;
inline constexpr Address phase_gate=0x77e570, active_actor_slot=0x7757e0, active_delta=0x7760d0;
inline constexpr Address player_callbacks=0x60885c, skill_callbacks=0x60916c;
inline constexpr Address special_item_callbacks=0x6131cc, enemy_callbacks=0x610c24;
inline constexpr unsigned player_stride=24, skill_stride=44, special_item_stride=76, enemy_stride=32;
inline constexpr Address player_failure=0x5d271c, skill_failure=0x5d2748;
inline constexpr Address special_item_failure=0x5d2774, enemy_failure=0x5d27a4, delta_failure=0x5d27cc;
inline constexpr Address delta_callback=0x46196c, reaction_scripts=0x5d2b18;
inline constexpr std::uint32_t pre_action_gate=0xc0000000u, no_deferred_action=0xffffffffu;
inline constexpr std::uint32_t pending=0, completed=1;
inline constexpr Address number_wait_count=0x78, final_tick=0x16c;
inline constexpr unsigned ready_tick=10, finish_delay=16, phase_increment=10;
inline constexpr Address actor_state_byte=actor_offset::flags+2;
inline constexpr unsigned actor_state_bit=1;
enum class Mode : std::uint32_t { None=0, PlayerAttack=1, SkillOrItem=2, SpecialItem=3 };
enum class DeltaPhase : std::int32_t {
    RestoreActor=-20, Initialize=-1, WaitReady=0, StartReaction=10, WaitNumber=20, FinalDelay=30
};
}
struct HandlerLifecycleServices {
    std::function<Address(Address callback)> spawn;
    std::function<void(Address message)> report_failure;
    std::function<void(unsigned action)> show_banner;
    std::function<void(Address actor,Address script)> start_script;
    std::function<void(Address actor,std::int32_t amount)> show_number;
    std::function<void(Address object)> release;
};
class HandlerLifecycle {
public:
    HandlerLifecycle(Memory& memory,const HandlerLifecycleServices& services):memory_(memory),services_(services){}
    bool start_player(std::uint32_t mode,unsigned action);
    bool start_enemy(unsigned action);
    bool queue_status_delta();
    std::uint32_t poll_completion();
    void tick_status_delta(Address controller);
private:
    Memory& memory_;
    const HandlerLifecycleServices& services_;
    void spawn_pending(Address callback,Address failure_message);
};
} // namespace fsb::core::combat
