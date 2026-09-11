#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>
namespace fsb::core::combat {
namespace entrance {
enum class Phase : unsigned { Initialize=0, WaitParty=1, ArmEnemyFocus=2, BeginEnemies=3, StepEnemy=4, ReadyFlash=5, Flash=6, Handoff=7 };
inline constexpr Address enemy_count=0x776484;
inline constexpr Address phase=0x77ec04, music_started=0x77ec50, enemy_index=0x77ecd8;
inline constexpr Address party_staging=0x7755dc, enemy_staging=0x775630, entrance_marker=0x7760d4;
inline constexpr unsigned party_stage_stride=16, enemy_stage_stride=20;
inline constexpr unsigned dive_flag=0x4000, ready_flags=0x10040, enemy_flags=0x10400;
inline constexpr unsigned special_monster=0x17, dive_phase=29, dive_done=21, dive_height=1000;
inline constexpr unsigned focus_ticks=15, music_fade=30, music_volume=100, gauge_modulus=20;
inline constexpr Address scripted_steps=0x45befc, field_movement=0x458ed7, enemy_dive=0x45c063, default_visual=0x45c526;
inline constexpr Address enemy_motion_phase=0x34, enemy_animation_phase=0x38;
inline constexpr Address command_busy=0x2c, enemy_kind=4, enemy_status=8, enemy_timers=12, enemy_resource=24, enemy_gauge=32;
inline constexpr Address definition_status=8, definition_hp=24, definition_mp=28;
}
struct EntranceServices {
    std::function<unsigned()> select_music;
    std::function<void(unsigned fade_out,unsigned from,unsigned track,unsigned start,unsigned fade_in,unsigned volume)> transition_music;
    std::function<void(Address actor,std::uint32_t x,std::uint32_t y,std::uint32_t grid)> place_actor;
    std::function<void(Address)> clear_timer;
    std::function<std::uint32_t()> random;
    std::function<void(unsigned enemy,bool refresh)> activate_enemy;
    std::function<void(unsigned from,unsigned to,unsigned ticks,bool enabled)> move_focus;
    std::function<void(unsigned to,unsigned ticks)> focus;
    std::function<void()> mark_entrance;
};
class Entrance {
public:
    Entrance(Memory& memory,const EntranceServices& services):memory_(memory),services_(services){}
    std::uint32_t activate_enemy(unsigned index,bool refresh);
    void tick(bool staged_party,bool immediate_enemies);
private:
    Memory& memory_;
    const EntranceServices& services_;
    void arm_enemy_focus();
    void wait_party(std::int32_t count);
    void begin_enemies(bool immediate);
    void step_enemy();
    void finish_flash();
};
}
