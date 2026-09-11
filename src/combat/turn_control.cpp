#include "fsb_core/combat/turn_control.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/combat/flow.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::combat {
using namespace turn;
bool TurnControl::has_adjacent_enemy(std::uint32_t x,std::uint32_t y) const {
    struct Neighbor { std::uint32_t x,y; Edge edge; };
    const Neighbor neighbors[]={{x,y-1,Edge::South},{x,y+1,Edge::North},
                                {x-1,y,Edge::East},{x+1,y,Edge::West}};
    for(const auto& neighbor:neighbors) {
        const auto slot=services_.occupant(neighbor.x,neighbor.y);
        if(slot<first_enemy||slot>=end_enemy)continue;
        if(!services_.passable(neighbor.x,neighbor.y,neighbor.edge,attack_passability))continue;
        // The edge callback may change the actor's record or its status.
        const auto record=memory_.read(Actors::slot(slot)+enemy_record_index);
        if(!(memory_.read(BattleRules::enemy_record(record)+enemy_status)&battle_status::action_blocked))return true;
    }
    return false;
}
void TurnControl::begin_action(std::uint32_t input) {
    const auto actor=Actors::slot(memory_.read(active_slot));
    if(input==unsigned(Input::Confirm)) {
        if(memory_.read(command_phase)!=idle||signed32(memory_.read(command_ticks))<=input_delay)return;
        memory_.write(actor+actor_offset::flags+1,memory_.read(actor+actor_offset::flags+1,1)|targeting_byte_bit,1);
        memory_.write(actor+actor_offset::callback,targeting_callback);
        memory_.write(action_mode,default_attack);
        const auto secondary=has_adjacent_enemy(memory_.read(actor+actor_offset::tile_x),memory_.read(actor+actor_offset::tile_y));
        memory_.write(selected_action,services_.default_action(memory_.read(active_slot),secondary));
        const auto handler=services_.default_handler(memory_.read(active_slot),secondary);
        memory_.write(selected_handler,handler);
        // Keep the entry actor even if a callback changed the active slot.
        services_.prepare_handler(handler,memory_.read(actor+actor_offset::facing));
    } else if(input==unsigned(Input::Cancel)&&memory_.read(command_phase)==idle) {
        services_.load_menu_assets(menu_assets);
        services_.open_menu(command_menu,open_menu);
        memory_.write(command_phase,menu_active);
    }
}
void TurnControl::activate_context() {
    const auto slot=memory_.read(active_slot),actor=Actors::slot(slot);
    const auto flags=memory_.read(actor+actor_offset::flags);
    if(flags&party_flag) {
        const auto record=BattleRules::party_record(memory_.read(globals::party_actor_ids+slot*4));
        const auto budget=signed32(memory_.read(record+party_movement))/movement_scale;
        const auto x=memory_.read(left),y=memory_.read(top);
        const MovementArea area{memory_.read(grid_id),{x,y,x+memory_.read(width),y+memory_.read(height)},
                                memory_.read(actor+actor_offset::tile_x),memory_.read(actor+actor_offset::tile_y),flood_marker};
        services_.flood_costs(area);
        memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags,1)|movement_overlay_bit,1);
        services_.mark_reachable(budget);
        memory_.write(command_phase,idle);
    } else if(flags&enemy_flag)services_.enemy_turn(actor);
}
void TurnControl::tick_defeated_enemy(Address actor) {
    if(memory_.read(actor+actor_offset::callback_state)!=0)return;
    const auto tick=memory_.read(actor+actor_offset::callback_tick_count);
    const auto flags=memory_.read(actor+actor_offset::flags);
    memory_.write(actor+actor_offset::flags,(tick&1)?flags|visible_bit:flags&~visible_bit);
    // JBE is unsigned: wrapped negative tick values also finish cleanup.
    if(tick>last_blink_tick) {
        memory_.write(flow::outstanding,memory_.read(flow::outstanding)-1);
        services_.release(actor);
    }
}
}
