#pragma once
#include "fsb_core/primitives.hpp"
#include <array>
#include <functional>

namespace fsb::core::combat {
namespace turn {
inline constexpr Address active_slot=0x7757e0, command_phase=0x77ecdc, command_ticks=0x77a568;
inline constexpr Address action_mode=0x77ebfc, selected_action=0x774180, selected_handler=0x77e588;
inline constexpr Address grid_id=0x77e598, left=0x7755c4, top=0x7755c8, width=0x7755cc, height=0x7755d0;
inline constexpr Address party_movement=0x60, enemy_record_index=0x118, enemy_status=8;
inline constexpr Address targeting_callback=0x45c14f;
inline constexpr unsigned first_enemy=60, end_enemy=90, attack_passability=2;
inline constexpr unsigned party_flag=0x100, enemy_flag=0x400, targeting_byte_bit=0x20;
inline constexpr unsigned movement_overlay_bit=0x80, visible_bit=0x40;
inline constexpr unsigned idle=0, menu_active=1, default_attack=1, menu_assets=1, command_menu=15, open_menu=1;
inline constexpr std::int32_t input_delay=5, movement_scale=10;
inline constexpr unsigned flood_marker=0x2d3, last_blink_tick=32;
enum class Input : std::uint32_t { Cancel=0, Confirm=1 };
// Direction of the edge tested from each occupied neighbor, not the probe side.
enum class Edge : unsigned { North=0, South=1, West=2, East=3 };
struct MovementArea {
    unsigned grid;
    std::array<std::uint32_t,4> bounds; // left, top, right, bottom, wrapping like original ADD
    std::uint32_t x,y;
    unsigned marker;
};
}
struct TurnControlServices {
    std::function<std::uint32_t(std::uint32_t x,std::uint32_t y)> occupant;
    std::function<std::uint8_t(std::uint32_t x,std::uint32_t y,turn::Edge edge,unsigned mode)> passable;
    std::function<unsigned(unsigned slot,bool secondary)> default_action, default_handler;
    std::function<void(unsigned handler,unsigned facing)> prepare_handler;
    std::function<void(unsigned set)> load_menu_assets;
    std::function<void(unsigned slot,unsigned mode)> open_menu;
    std::function<void(const turn::MovementArea&)> flood_costs;
    std::function<void(std::int32_t budget)> mark_reachable;
    std::function<void(Address actor)> enemy_turn, release;
};
class TurnControl {
public:
    TurnControl(Memory& memory,const TurnControlServices& services):memory_(memory),services_(services){}
    bool has_adjacent_enemy(std::uint32_t x,std::uint32_t y) const; //44ceb2, caller consumes AL only
    void begin_action(std::uint32_t input); //44cfe2
    void activate_context(); //44d17b
    void tick_defeated_enemy(Address actor); //461c66
private:
    Memory& memory_;
    const TurnControlServices& services_;
};
}
