#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>

namespace fsb::core::combat {
namespace followup {
inline constexpr Address active_actor=0x8059f0, targets=0x8059f8, target_flags=0x805608;
inline constexpr Address target_count=0x77a50c, mirrored_target_count=0x805878, active_slot=0x7757e0;
inline constexpr Address result_rows=0x773f88, selected_row=0x774188, selected_action=0x7764d8;
inline constexpr Address action_mode=0x77ebfc, player_action=0x774180, enemy_action=0x7760d8;
inline constexpr Address active_amount=0x7760d0, counter_auxiliary=0x7760cc;
inline constexpr Address opposite_facing=0x5bf498, departure_script=0x5d2c98;
inline constexpr unsigned result_stride=16, flags_offset=4, amount_offset=8, auxiliary_offset=12;
inline constexpr unsigned party_slot_limit=16, player_attack_mode=1;
inline constexpr unsigned primary_wave=0, primary_wave_byte=2, secondary_wave_byte=3;
inline constexpr unsigned primary_target_bit=4, primary_active_bit=8, secondary_target_bit=8, secondary_active_bit=4;
inline constexpr Address party_special_status_byte=9;
inline constexpr unsigned party_special_status_bit=0x20;
inline constexpr std::uint32_t actor_pending_bit=0x10000;
enum class Stage : unsigned { Snapshot=0, Restore=1, Counter=2, RestoreCounter=3 };
namespace result {
inline constexpr std::uint32_t miss=1,hit=2,drain_damage=4,heal=8,recovery=0x10;
inline constexpr std::uint32_t life_drain=0x20,drain_link=0x40,force_reaction=0x80,copy_marker=0x200;
inline constexpr std::uint32_t poison=0x400,sleep=0x800,silence=0x1000,paralysis=0x2000,curse=0x4000;
inline constexpr std::uint32_t guard=0x8000,reflect=0x10000;
inline constexpr std::uint32_t counter_miss=0x100000,counter_hit=0x200000,counter_force=0x400000;
inline constexpr std::uint32_t counter_critical=0x800000,counter_guard=0x1000000,counter_reflect=0x2000000;
inline constexpr std::uint32_t special_mask=0xf0000000,special_value=0x70000000;
}
namespace presentation {
inline constexpr std::uint32_t hit=2,force_reaction=0x20,life_drain=0x80,drain_link=0x100,extra_marker=0x200;
}
}
struct FollowupServices {
    std::function<void(Address)> snapshot_motion;
    std::function<void(Address)> restore_motion;
    std::function<void(Address,std::int32_t)> attach_effect;
    std::function<void(Address)> clear_effects;
    std::function<void(Address,Address)> start_script;
};
class Followup {
public:
    Followup(Memory& memory,const FollowupServices& services):memory_(memory),services_(services){}
    void prepare(unsigned stage);
    void enqueue_wave(unsigned wave);
    static std::uint32_t primary_presentation(std::uint32_t flags);
    static std::uint32_t counter_presentation(std::uint32_t flags);
private:
    Memory& memory_;
    const FollowupServices& services_;
    void snapshot_targets();
    void restore_targets();
    void prepare_counter();
    void restore_counter();
    void queue_actor(Address actor,bool first_wave);
};
} // namespace fsb::core::combat
