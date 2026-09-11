#include "fsb_core/combat/followup.hpp"
#include "fsb_core/combat/attached_effects.hpp"
#include "fsb_core/combat/flow.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/battle_rules.hpp"
#include "fsb_core/symbols.hpp"
#include <utility>

namespace fsb::core::combat {
namespace {
using namespace followup;
template<class F> const F& required(const F& service,Address at){if(!service)throw Fault(at,"follow-up service is not connected");return service;}
Address row(unsigned index){return result_rows+index*result_stride;}
}
std::uint32_t Followup::primary_presentation(std::uint32_t flags){
    auto value=flags&(result::miss|result::hit|result::drain_damage|result::heal|result::recovery);
    if(flags&result::force_reaction)value|=presentation::force_reaction;
    if(flags&result::life_drain)value|=presentation::life_drain;
    if(flags&result::drain_link)value|=presentation::drain_link;
    if(flags&result::guard)value&=~presentation::hit;
    if(flags&result::reflect)value=(value&~presentation::hit)|presentation::force_reaction;
    if(flags&result::copy_marker)value|=presentation::extra_marker;
    return value;
}
std::uint32_t Followup::counter_presentation(std::uint32_t flags){
    std::uint32_t value=(flags&result::counter_miss)?1u:0u;
    if(flags&result::counter_force)value|=presentation::force_reaction;
    if(flags&result::counter_hit)value|=presentation::hit;
    if(flags&result::counter_guard)value&=~presentation::hit;
    if(flags&result::counter_reflect)value=(value&~presentation::hit)|presentation::force_reaction;
    if(flags&result::counter_critical)value|=presentation::extra_marker;
    return value;
}
void Followup::snapshot_targets(){
    const auto actor=Actors::slot(memory_.read(active_slot));memory_.write(active_actor,actor);
    required(services_.snapshot_motion,0x45db53)(actor);
    for(std::int32_t i=0;i<signed32(memory_.read(target_count));++i){
        memory_.write(mirrored_target_count,memory_.read(target_count));
        const auto object=Actors::slot(memory_.read(row(unsigned(i))));
        memory_.write(targets+unsigned(i)*4,object);
        required(services_.snapshot_motion,0x45db53)(object);
        const auto flags=memory_.read(row(unsigned(i))+flags_offset);
        memory_.write(target_flags+unsigned(i)*4,primary_presentation(flags));
    }
}
void Followup::restore_targets(){
    if(const auto actor=memory_.read(active_actor)){
        memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)|actor_pending_bit);
        required(services_.restore_motion,0x45db6c)(memory_.read(active_actor));
    }
    for(std::int32_t i=0;i<signed32(memory_.read(target_count));++i){
        const auto target_at=targets+unsigned(i)*4,flags_at=row(unsigned(i))+flags_offset;
        const auto target=memory_.read(target_at);if(!target)continue;
        memory_.write(target+actor_offset::flags,memory_.read(target+actor_offset::flags)|actor_pending_bit);
        required(services_.restore_motion,0x45db6c)(memory_.read(target_at));
        using Kind=attached_effects::Kind;
        const std::pair<std::uint32_t,Kind> statuses[]{
            {result::poison,Kind::Poison},{result::sleep,Kind::Sleep},{result::silence,Kind::Silence},
            {result::paralysis,Kind::Paralysis},{result::curse,Kind::Curse}};
        for(const auto [flag,kind]:statuses)
            if(memory_.read(flags_at)&flag)required(services_.attach_effect,0x4622c2)(memory_.read(target_at),std::int32_t(kind));
        // Callbacks may change the row or its target, so each subsequent test
        // and call reads the live values just as the original does.
        if((memory_.read(flags_at)&result::special_mask)==result::special_value){
            const auto id=memory_.read(globals::party_actor_ids+memory_.read(flags_at-4)*4);
            if(memory_.read(BattleRules::party_record(id)+party_special_status_byte,1)&party_special_status_bit)
                required(services_.attach_effect,0x4622c2)(memory_.read(target_at),std::int32_t(Kind::PartySpecial));
        }
        if(memory_.read(flags_at)&result::guard)required(services_.attach_effect,0x4622c2)(memory_.read(target_at),std::int32_t(Kind::Guard));
        if(memory_.read(flags_at)&result::reflect)required(services_.attach_effect,0x4622c2)(memory_.read(target_at),std::int32_t(Kind::Reflect));
    }
}
void Followup::prepare_counter(){
    const auto selected=memory_.read(row(memory_.read(selected_row)));
    if(signed32(selected)<std::int32_t(party_slot_limit)){
        const auto action=memory_.read(selected_action);memory_.write(action_mode,player_attack_mode);memory_.write(player_action,action);
    }else memory_.write(enemy_action,memory_.read(selected_action));
    const auto actor=Actors::slot(selected);memory_.write(active_actor,actor);
    required(services_.snapshot_motion,0x45db53)(actor);
    auto object=memory_.read(active_actor);
    memory_.write(object+actor_offset::target_facing,memory_.read(object+actor_offset::facing));
    const auto primary_actor=Actors::slot(memory_.read(active_slot));
    object=memory_.read(active_actor);
    memory_.write(object+actor_offset::facing,memory_.read(opposite_facing+memory_.read(primary_actor+actor_offset::facing)*4));
    const auto previous_slot=memory_.read(active_slot),saved_first_target=memory_.read(result_rows);
    memory_.write(result_rows,previous_slot);
    const auto selected_flags=row(memory_.read(selected_row))+flags_offset;
    const auto target=Actors::slot(previous_slot);
    memory_.write(target_count,1);
    memory_.write(result_rows+flags_offset,memory_.read(selected_flags));
    memory_.write(result_rows+amount_offset,memory_.read(active_amount));
    const auto auxiliary=memory_.read(counter_auxiliary);
    memory_.write(mirrored_target_count,1);memory_.write(targets,target);
    // This is the old first-row target, not necessarily the selected counter.
    memory_.write(active_slot,saved_first_target);memory_.write(result_rows+auxiliary_offset,auxiliary);
    required(services_.snapshot_motion,0x45db53)(target);
    memory_.write(target_flags,counter_presentation(memory_.read(result_rows+flags_offset)));
}
void Followup::restore_counter(){
    if(const auto object=memory_.read(active_actor)){
        memory_.write(object+actor_offset::flags+2,memory_.read(object+actor_offset::flags+2,1)|1u,1);
        required(services_.restore_motion,0x45db6c)(memory_.read(active_actor));
        const auto restored=memory_.read(active_actor);
        memory_.write(restored+actor_offset::facing,memory_.read(restored+actor_offset::target_facing));
    }
    if(const auto object=memory_.read(targets)){
        memory_.write(object+actor_offset::flags+2,memory_.read(object+actor_offset::flags+2,1)|1u,1);
        required(services_.restore_motion,0x45db6c)(memory_.read(targets));
    }
}
void Followup::prepare(unsigned stage){
    switch(static_cast<Stage>(stage)){
    case Stage::Snapshot:snapshot_targets();break;case Stage::Restore:restore_targets();break;
    case Stage::Counter:prepare_counter();break;case Stage::RestoreCounter:restore_counter();break;
    default:break;
    }
}
void Followup::queue_actor(Address actor,bool first_wave){
    const auto clear_pending=[&]{memory_.write(actor+actor_offset::flags,memory_.read(actor+actor_offset::flags)&~actor_pending_bit);};
    // The second wave clears before reading/publishing the queue index; the
    // first wave publishes first. Preserve this even for overlapping records.
    if(!first_wave)clear_pending();
    const auto slot=memory_.read(flow::pending_count);
    memory_.write(flow::pending_actors+slot*4,actor);
    if(first_wave)clear_pending();
    required(services_.start_script,0x447841)(actor,departure_script);
    memory_.write(flow::pending_count,memory_.read(flow::pending_count)+1u);
    required(services_.clear_effects,0x4623dd)(actor);
}
void Followup::enqueue_wave(unsigned wave){
    const bool first=wave==primary_wave;memory_.write(flow::pending_count,0);
    const auto flag_byte=first?primary_wave_byte:secondary_wave_byte,target_bit=first?primary_target_bit:secondary_target_bit,active_bit=first?primary_active_bit:secondary_active_bit;
    for(std::int32_t i=0;i<signed32(memory_.read(target_count));++i){
        const auto flags_at=row(unsigned(i))+flags_offset;
        if(memory_.read(flags_at+flag_byte,1)&target_bit)queue_actor(Actors::slot(memory_.read(flags_at-4)),first);
        if(memory_.read(flags_at+flag_byte,1)&active_bit)queue_actor(memory_.read(active_actor),first);
    }
    memory_.write(flow::outstanding,memory_.read(flow::pending_count));
}
} // namespace fsb::core::combat
