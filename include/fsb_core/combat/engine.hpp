#pragma once
#include "fsb_core/combat/followup.hpp"
#include "fsb_core/combat/turn_control.hpp"

namespace fsb::core::combat {
namespace engine {
enum class Mode : unsigned { Idle=0, Initialize=1, Active=2 };
enum class Phase : unsigned {
    Precheck=0, PrepareContext=1, WaitFocus=2, WaitStatus=3, Command=4,
    StartAction=5, WaitAction=6, FinishAction=7, StartCounter=8, WaitCounter=9,
    FinishCounter=10, Cleanup=11, PostAction=12, FlashEntrance=13, FlashRetirement=14
};
enum class MenuCommand : unsigned { Cancel=1, Skill=4, Item=5, Finish=6 };
inline constexpr Address mode=0x775cb0, phase=0x775cac, force_cleanup=0x77ece0;
inline constexpr Address idle_ticks=0x77a4f8, generation=0x77ec38, extra_trigger=0x77ec44;
inline constexpr Address focus_slot=0x787478, focus_pending=0x7873b8, focus_x=0x8572dc, focus_y=0x8572e0;
inline constexpr Address frame_parity=0x804a60, results_phase=0x77a514, frame_submode=0x77e5a0, title_flow=0x804a64;
inline constexpr Address phase_gate=0x77e570, cursor_flags=0x77ec4c, field_phase=0x776478;
inline constexpr Address marker_objects=0x77ec60, enemy_vitality=0x77a518;
inline constexpr Address actor_ready=0x77a500, entrance_slot=0x77a510, flash_ticks=0x77a4e0;
inline constexpr Address palette_index=0x774168, normal_sprite=0x5b38c8, palette_sprites=0x5b390c;
inline constexpr Address enemy_handlers=0x610c20, skill_handlers=0x609168;
inline constexpr Address actor_command_wait=0x2c, enemy_gauge=32;
inline constexpr unsigned enemy_handler_stride=32, skill_handler_stride=44, palette_stride=68;
inline constexpr unsigned results_submode=2, game_over_screen=8, idle_delay=10, focus_duration=10;
inline constexpr unsigned cursor_active=1, cursor_effect=2, cursor_tracking=6, cursor_radius=10;
inline constexpr unsigned marker_count=30, marker_action=0xf7, marker_damage=444, marker_lethal=0x40000;
inline constexpr unsigned enemy_auto_marker_block=0x10, enemy_down_byte=0x20, ai_confirm_tick=20;
inline constexpr std::uint32_t pre_action_gate=0xc0000000, post_action_gate=0x60000000;
inline constexpr std::uint32_t no_handler=0xffffffff, flash_pattern=0xdeed5221;
inline constexpr std::int32_t last_flash_tick=30, retired_party_id=12;
// IDs whose original menu branch selects a distinct targeting footprint.
inline constexpr unsigned escape_item=0x133, no_target_item_a=0x134, no_target_item_b=0x137, no_target_item_c=0x139;
inline constexpr unsigned escape_handler=0x1a, no_target_handler=0, item_handler=0x40;
}
struct EngineServices {
    std::function<void()> decay_status, advance_gauges, prepare_vitality, commit_vitality;
    std::function<void(bool)> snapshot_party, snapshot_enemy;
    std::function<unsigned()> precheck;
    // Publishes the selected slot, including -1 when none is ready.
    std::function<bool()> select_next;
    std::function<void(unsigned from,unsigned to,unsigned ticks,bool enabled)> move_focus;
    std::function<bool(unsigned slot,unsigned mask)> has_status;
    std::function<std::uint32_t(unsigned slot)> poison_delta;
    std::function<void(unsigned slot,std::uint32_t delta)> apply_delta;
    std::function<void()> queue_status, activate_context;
    std::function<bool()> poll_handler, finish_followup;
    std::function<void(followup::Stage)> prepare_followup;
    std::function<void(unsigned wave)> enqueue_wave;
    std::function<void(unsigned mode,unsigned action)> start_player;
    std::function<void(unsigned action)> start_enemy;
    std::function<void()> invalidate_panel, clear_command, select_ai_followup, apply_status_icons;
    std::function<std::uint32_t()> menu_result;
    std::function<void(std::uint32_t x,std::uint32_t y,unsigned radius,unsigned keep)> clear_cursor;
    std::function<unsigned(unsigned slot,bool secondary)> default_handler;
    std::function<void(unsigned handler,unsigned facing)> prepare_handler;
    std::function<void(std::uint32_t x,std::uint32_t y,bool direct)> track_action;
    std::function<void()> mark_entrance, retire_entrance;
};
// One battle-engine tick. Each branch explicitly decides whether it yields or
// performs the original same-tick poll; no implicit run-until-blocked loop.
class Engine {
public:
    Engine(Memory& memory,const EngineServices& services):memory_(memory),services_(services){}
    void tick(); //44d45a
private:
    Memory& memory_;
    const EngineServices& services_;
    void set_phase(engine::Phase phase);
    void snapshot_panels(bool active);
    void precheck();
    void prepare_context();
    void command();
    void menu_command(Address actor);
    void enemy_command(Address actor,std::uint32_t x,std::uint32_t y);
    void begin_action(bool counter);
    void poll_action(bool counter);
    void finish_action(bool counter);
    void refresh_focus();
    void clear_active_overlay();
    void post_action();
    void flash_entrance(bool retiring);
};
}
