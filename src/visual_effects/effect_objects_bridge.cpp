// Binds the reconstructed effect objects to the helpers they call.
//
// Every helper below still belongs to another scope, so each one is reached at
// its original address through the existing call path. Two of them, the effect
// script start at 447841 and the controller notify at 461d25, already resolve
// to reconstructed C++ inside that path; going through the address keeps this
// bridge from having to know which of them have moved yet.
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/original_audio.hpp"
#include "fsb_core/visual_effects/directional_particles.hpp"
#include "fsb_core/visual_effects/effect_lifetimes.hpp"
#include "fsb_core/visual_effects/effect_objects.hpp"
#include "fsb_core/visual_effects/projectiles.hpp"
#include "fsb_core/visual_effects/sequences.hpp"

namespace fsb::core {

bool RecoveredBattle::dispatch_d_effect_objects(Address entry) {
    if(entry<0x4672b5u||entry>0x48613cu)return false;
    visual_effects::EffectServices services;
    services.spawn_object = [this](Address cb) { return callback(0x45d89c, {cb}); };
    services.finalize_object = [this](Address object) { callback(0x45d91d, {object}); };
    services.apply_oscillation = [this](Address object) { callback(0x45d208, {object}); };
    services.restore_motion_block = [this](Address object) { callback(0x45db6c, {object}); };
    services.spawn_floating_number = [this](Address object, std::int32_t amount) {
        return callback(0x4646be, {object, std::uint32_t(amount)});
    };
    services.spawn_effect_object = [this](std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t sprite, Address descriptor) {
        return callback(0x464c27, {std::uint32_t(x), std::uint32_t(y), std::uint32_t(z), std::uint32_t(sprite), descriptor});
    };
    services.spawn_effect_object_with_notify = [this](std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t sprite, Address descriptor, std::int32_t notify) {
        return callback(0x464cf4, {std::uint32_t(x), std::uint32_t(y), std::uint32_t(z), std::uint32_t(sprite), descriptor, std::uint32_t(notify)});
    };
    services.spawn_swirl_particle = [this](Address object) { callback(0x466cfc, {object}); };
    services.notify_controller = [this](Address object, std::int32_t state) {
        callback(0x461d25, {object, std::uint32_t(state)});
    };
    services.run_effect_script = [this](Address object, Address script) {
        callback(0x447841, {object, script});
    };
    services.play_cue = [this](unsigned cue) { callback(original_audio::play_cue, {cue}); };
    services.stop_cue = [this](unsigned cue) { callback(original_audio::stop_cue, {cue}); };
    services.spawn_floating_number_glyphset = [this](Address source, std::int32_t amount) {
        return callback(0x4647ad, {source, std::uint32_t(amount)});
    };
    services.screen_flash_transition = [this](std::int32_t mode, std::int32_t frames) {
        return std::int32_t(callback(0x464c59, {std::uint32_t(mode), std::uint32_t(frames)}));
    };
    services.apply_homing_motion_clamp = [this](Address object) { callback(0x462de2, {object}); };
    services.random = [this] { return std::int32_t(crt_rand(memory_)); };

    switch (entry) {
    case 0x467417:
        result(std::uint32_t(visual_effects::RingEffect(memory_, services).spawn(argument(0))), 4);
        return true;
    case 0x4672b5:
        visual_effects::RingEffect(memory_, services).tick(argument(0));
        // The object pump discards a callback's result, so nothing reads this.
        result(0, 4);
        return true;
    case 0x4674de:
        visual_effects::SweepController(memory_, services).run(argument(0), argument(1));
        result(0, 8);
        return true;
    case 0x46dca5:
        visual_effects::DirectionalHitParticles(memory_, services).tick(argument(0));
        result(0, 4);
        return true;
    case 0x468790:
        visual_effects::Projectiles(memory_, services).tick(argument(0));
        result(0, 4);
        return true;
    case 0x4687f3:
        result(visual_effects::Projectiles(memory_, services).launch(argument(0), argument(1)), 8);
        return true;
    case 0x46bd92:
        result(visual_effects::Projectiles(memory_, services).launch_arc(argument(0), argument(1)), 8);
        return true;
    case 0x47c807:
        result(visual_effects::Projectiles(memory_, services).launch_directional(argument(0), argument(1)), 8);
        return true;
    // The skill callback table forwards into these two, handing them the acting
    // actor's script table as their second argument.
    case 0x47be07:
        visual_effects::TargetFanoutSequence(memory_, services).recover_targets(argument(0), argument(1));
        result(0, 8);
        return true;
    case 0x485ccf:
        visual_effects::TargetFanoutSequence(memory_, services).shared_status_buff(argument(0), argument(1));
        result(0, 8);
        return true;
    case 0x467be6:
        visual_effects::TargetFanoutSequence(memory_, services).all_target_lift_numbers(argument(0), argument(1));
        result(0, 8);
        return true;
    case 0x46fffd:
        visual_effects::TargetFanoutSequence(memory_, services).descending_particle_burst(argument(0), argument(1));
        result(0, 8);
        return true;
    // These two take a weapon tier as their second argument, not a table.
    case 0x483179:
        visual_effects::TargetFanoutSequence(memory_, services)
            .single_target_particle_attack(argument(0), std::int32_t(argument(1)));
        result(0, 8);
        return true;
    case 0x482f4d:
        visual_effects::TargetFanoutSequence(memory_, services)
            .multitarget_particle_attack(argument(0), std::int32_t(argument(1)));
        result(0, 8);
        return true;
    case 0x4693ef:
        visual_effects::TargetFanoutSequence(memory_, services)
            .first_target_directional_burst(argument(0), argument(1));
        result(0, 8);
        return true;
    case 0x467ed6:
        visual_effects::TargetFanoutSequence(memory_, services)
            .target_flash_drift(argument(0), argument(1));
        result(0, 8);
        return true;
    case 0x469321:
        result(visual_effects::DirectionalHitParticles(memory_, services)
                   .spawn_directional_expand(argument(0), argument(1)), 8);
        return true;
    default:
        break;
    }

    // The nine travel helpers. Each has one caller, and every one of those
    // discards EAX on the instruction after the call, so handing back the child
    // rather than the value the original happened to leave is safe.
    visual_effects::DirectionalHitParticles particles(memory_, services);
    using Helper = Address (visual_effects::DirectionalHitParticles::*)(Address, Address);
    const auto travel = [&](Helper helper) {
        result((particles.*helper)(argument(0), argument(1)), 8);
        return true;
    };
    switch (entry) {
    case 0x46d904: return travel(&visual_effects::DirectionalHitParticles::spawn_hit);
    case 0x46dcd7: return travel(&visual_effects::DirectionalHitParticles::spawn_double_hit);
    case 0x46f684: return travel(&visual_effects::DirectionalHitParticles::spawn_high_hit);
    case 0x46f92f: return travel(&visual_effects::DirectionalHitParticles::spawn_flat_cue);
    case 0x46a8e9: return travel(&visual_effects::DirectionalHitParticles::spawn_slow_arc_hit);
    case 0x46b772: return travel(&visual_effects::DirectionalHitParticles::spawn_fast_arc_trigger_hit);
    case 0x46e344: return travel(&visual_effects::DirectionalHitParticles::spawn_deferred_trigger);
    case 0x46e8b3: return travel(&visual_effects::DirectionalHitParticles::spawn_facing_deferred_trigger);
    case 0x470b90: return travel(&visual_effects::DirectionalHitParticles::spawn_poison_liquid);
    default:
        break;
    }

    // Small helpers and the effects that step and then finish. None of their
    // call sites reads EAX within six instructions of the call, so the value
    // these leave is not reproduced.
    switch (entry) {
    case 0x4692c6:
        // The one entry here that takes no argument.
        visual_effects::FinishingEffects(memory_, services).stop_screen_shake();
        result(0, 0);
        return true;
    default:
        break;
    }
    visual_effects::FinishingEffects finishing(memory_, services);
    switch (entry) {
    case 0x46a1b6: visual_effects::AnimatedParticle(memory_, services).tick(argument(0)); break;
    case 0x467db3: visual_effects::DriftEffect(memory_, services).spawn(argument(0)); break;
    case 0x4681d6: visual_effects::DebrisEffect(memory_, services).spawn(argument(0)); break;
    case 0x4860ff: visual_effects::RisingEffect(memory_, services).spawn(argument(0)); break;
    case 0x481b11: finishing.release_after_twenty_ticks(argument(0)); break;
    case 0x46dfc3: finishing.release_when_script_ends(argument(0)); break;
    case 0x46c5bb: finishing.travel_then_release(argument(0)); break;
    case 0x4719e2: finishing.drift_until_script_ends(argument(0)); break;
    case 0x4705c6: finishing.report_hit_when_script_ends(argument(0)); break;
    case 0x478c12: finishing.report_actor_done_when_script_ends(argument(0)); break;
    case 0x485c52: finishing.report_owner_hit_when_script_ends(argument(0)); break;
    case 0x47eb35: finishing.travel_then_report_done(argument(0)); break;
    case 0x4727b4: finishing.report_ready_when_script_ends(argument(0)); break;
    case 0x482bd8: finishing.scatter_drift_when_script_ends(argument(0)); break;
    case 0x4682cc: finishing.drop_then_scatter_debris(argument(0)); break;
    case 0x48613c: finishing.fall_then_raise_successor(argument(0)); break;
    case 0x46dfdf: visual_effects::TrailSprite(memory_, services).spawn(argument(0)); break;
    case 0x47bde6: visual_effects::RecoverTargetChild(memory_, services).spawn(argument(0)); break;
    case 0x485c80: visual_effects::StatusBuffChild(memory_, services).spawn(argument(0)); break;
    case 0x467bcf: visual_effects::LiftNumberChild(memory_, services).spawn(argument(0)); break;
    case 0x46ff7a: visual_effects::DescendingBurst(memory_, services).spawn(argument(0)); break;
    case 0x46ff40: visual_effects::DescendingBurst(memory_, services).tick(argument(0)); break;
    case 0x467e8c: visual_effects::TargetFlashNotify(memory_, services).spawn(argument(0)); break;
    case 0x467e47: visual_effects::TargetFlashNotify(memory_, services).tick(argument(0)); break;
    case 0x4692e5: finishing.directional_expand_impact(argument(0)); break;
    case 0x46d24c: finishing.projectile_impact(argument(0)); break;
    case 0x46ed0e: finishing.particle_impact(argument(0)); break;
    case 0x46f1f6: finishing.dual_notify_impact(argument(0)); break;
    case 0x47fcb7: finishing.homing_impact(argument(0)); break;
    case 0x47f526: finishing.homing_impact_with_cue(argument(0)); break;
    case 0x47dc33: finishing.homing_impact_on_first_target(argument(0)); break;
    case 0x483380: finishing.homing_impact_on_link(argument(0)); break;
    case 0x4823e1: finishing.homing_impact_on_link_with_cue(argument(0)); break;
    case 0x484f62: finishing.homing_impact_after_delay(argument(0)); break;
    case 0x46e013: finishing.directional_trail_impact(argument(0)); break;
    case 0x4837df: finishing.arc_projectile_impact(argument(0)); break;
    case 0x47219f: finishing.rising_sprite_notify(argument(0)); break;
    default:
        return false;
    }
    result(0, 4);
    return true;
}

}
