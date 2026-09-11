// Compares RingEffect and SweepController against
// reference/parallel/D_EFFECTS/effect-objects-x86.bin, which
// tools/parallel/D_EFFECTS/prepare_effect_objects_reference.py recorded by
// running the original bodies. Expected values never come from this code.
#include "fsb_core/visual_effects/effect_objects.hpp"
#include "fsb_core/visual_effects/directional_particles.hpp"
#include "fsb_core/visual_effects/effect_lifetimes.hpp"
#include "fsb_core/visual_effects/projectiles.hpp"
#include "fsb_core/visual_effects/sequences.hpp"
#include "../../../tools/lab_io.hpp"
#include <iostream>
#include <vector>

using namespace fsb::core;
using namespace fsb::core::visual_effects;

namespace {
constexpr Address spawn_object = 0x45d89c, finalize_object = 0x45d91d, oscillation = 0x45d208;
constexpr Address swirl = 0x466cfc, notify = 0x461d25, run_script = 0x447841;
constexpr Address cue = 0x435373, floating_number = 0x4647ad, flash = 0x464c59;
constexpr Address homing_clamp = 0x462de2, stop_cue = 0x4353cb;
constexpr Address place_effect = 0x464c27, floating_number_plain = 0x4646be, restore_motion = 0x45db6c;
constexpr Address place_effect_notify = 0x464cf4;
constexpr std::uint32_t result_base = 0x5eed0000;
constexpr unsigned object_bytes = 428;
// Must match the scratch pool the fixture hands out for 45d89c.
constexpr Address pool = 0x8073d8 + 700 * object_bytes;
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        const auto initial = Memory::from_pe32(fsb::lab::read(argv[1]));
        const auto bytes = fsb::lab::read(argv[2]);
        std::size_t cursor = 8;
        if (bytes.size() < 12 || std::string(bytes.begin(), bytes.begin() + 8) != std::string("FSBEFO1\0", 8))
            throw std::runtime_error("bad effect object fixture");
        const auto word = [&]() {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes.at(cursor++)) << (8 * i);
            return value;
        };
        using Call = std::pair<Address, std::vector<std::uint32_t>>;
        const auto count = word();
        for (unsigned index = 0; index < count; ++index) {
            const auto entry = word(), flash_result = word(), argument_count = word(), wanted = word();
            std::vector<std::uint32_t> args;
            for (unsigned i = 0; i < argument_count; ++i) args.push_back(word());

            auto memory = initial;
            for (auto writes = word(); writes; --writes) { const auto at = word(); memory.write(at, word()); }
            auto expected = memory.bytes(0x4a5000, 3882100);
            std::vector<Call> expected_calls;
            for (auto observed = word(); observed; --observed) {
                const auto address = word();
                std::vector<std::uint32_t> taken;
                for (auto n = word(); n; --n) taken.push_back(word());
                expected_calls.emplace_back(address, std::move(taken));
            }
            for (auto changes = word(); changes; --changes) {
                const auto at = word();
                expected.at(at - 0x4a5000) = bytes.at(cursor++);
            }

            std::vector<Call> calls;
            unsigned handed_out = 0;
            const auto note = [&](Address address, std::vector<std::uint32_t> taken) {
                calls.emplace_back(address, std::move(taken));
            };
            EffectServices services;
            services.spawn_object = [&](Address callback) {
                note(spawn_object, {callback});
                return pool + object_bytes * handed_out++;
            };
            services.finalize_object = [&](Address object) { note(finalize_object, {object}); };
            services.apply_oscillation = [&](Address object) { note(oscillation, {object}); };
            services.spawn_swirl_particle = [&](Address object) { note(swirl, {object}); };
            services.notify_controller = [&](Address object, std::int32_t state) {
                note(notify, {object, std::uint32_t(state)});
            };
            services.run_effect_script = [&](Address object, Address script) {
                note(run_script, {object, script});
            };
            services.play_cue = [&](unsigned id) { note(cue, {id}); };
            services.spawn_floating_number_glyphset = [&](Address source, std::int32_t amount) {
                note(floating_number, {source, std::uint32_t(amount)});
                return Address(result_base | (floating_number & 0xffff));
            };
            services.screen_flash_transition = [&](std::int32_t mode, std::int32_t frames) {
                note(flash, {std::uint32_t(mode), std::uint32_t(frames)});
                return std::int32_t(flash_result);
            };
            services.apply_homing_motion_clamp = [&](Address object) { note(homing_clamp, {object}); };
            services.stop_cue = [&](unsigned id) { note(stop_cue, {id}); };
            services.spawn_effect_object = [&](std::int32_t x, std::int32_t y, std::int32_t z,
                                              std::int32_t sprite, Address descriptor) {
                note(place_effect, {std::uint32_t(x), std::uint32_t(y), std::uint32_t(z),
                                    std::uint32_t(sprite), descriptor});
                return Address(result_base | (place_effect & 0xffff));
            };
            services.spawn_floating_number = [&](Address source, std::int32_t amount) {
                note(floating_number_plain, {source, std::uint32_t(amount)});
                return Address(result_base | (floating_number_plain & 0xffff));
            };
            services.spawn_effect_object_with_notify = [&](std::int32_t x, std::int32_t y, std::int32_t z,
                                                          std::int32_t sprite, Address descriptor,
                                                          std::int32_t notify) {
                note(place_effect_notify, {std::uint32_t(x), std::uint32_t(y), std::uint32_t(z),
                                           std::uint32_t(sprite), descriptor, std::uint32_t(notify)});
                return Address(result_base | (place_effect_notify & 0xffff));
            };
            services.restore_motion_block = [&](Address object) { note(restore_motion, {object}); };
            services.random = [&]() { return std::int32_t(crt_rand(memory)); };

            std::uint32_t result = 0;
            switch (entry) {
            case 0x467417: result = std::uint32_t(RingEffect(memory, services).spawn(args.at(0))); break;
            case 0x4672b5: RingEffect(memory, services).tick(args.at(0)); break;
            case 0x4674de: SweepController(memory, services).run(args.at(0), args.at(1)); break;
            case 0x46dca5: DirectionalHitParticles(memory, services).tick(args.at(0)); break;
            case 0x468790: Projectiles(memory, services).tick(args.at(0)); break;
            case 0x4687f3: result = Projectiles(memory, services).launch(args.at(0), args.at(1)); break;
            case 0x46bd92: result = Projectiles(memory, services).launch_arc(args.at(0), args.at(1)); break;
            case 0x47c807: result = Projectiles(memory, services).launch_directional(args.at(0), args.at(1)); break;
            case 0x46a1b6: AnimatedParticle(memory, services).tick(args.at(0)); break;
            case 0x467db3: DriftEffect(memory, services).spawn(args.at(0)); break;
            case 0x4681d6: DebrisEffect(memory, services).spawn(args.at(0)); break;
            case 0x4860ff: RisingEffect(memory, services).spawn(args.at(0)); break;
            case 0x481b11: FinishingEffects(memory, services).release_after_twenty_ticks(args.at(0)); break;
            case 0x46dfc3: FinishingEffects(memory, services).release_when_script_ends(args.at(0)); break;
            case 0x46c5bb: FinishingEffects(memory, services).travel_then_release(args.at(0)); break;
            case 0x4719e2: FinishingEffects(memory, services).drift_until_script_ends(args.at(0)); break;
            case 0x4705c6: FinishingEffects(memory, services).report_hit_when_script_ends(args.at(0)); break;
            case 0x478c12: FinishingEffects(memory, services).report_actor_done_when_script_ends(args.at(0)); break;
            case 0x485c52: FinishingEffects(memory, services).report_owner_hit_when_script_ends(args.at(0)); break;
            case 0x47eb35: FinishingEffects(memory, services).travel_then_report_done(args.at(0)); break;
            case 0x4727b4: FinishingEffects(memory, services).report_ready_when_script_ends(args.at(0)); break;
            case 0x482bd8: FinishingEffects(memory, services).scatter_drift_when_script_ends(args.at(0)); break;
            case 0x4682cc: FinishingEffects(memory, services).drop_then_scatter_debris(args.at(0)); break;
            case 0x48613c: FinishingEffects(memory, services).fall_then_raise_successor(args.at(0)); break;
            case 0x4692c6: FinishingEffects(memory, services).stop_screen_shake(); break;
            case 0x46dfdf: TrailSprite(memory, services).spawn(args.at(0)); break;
            case 0x47bde6: RecoverTargetChild(memory, services).spawn(args.at(0)); break;
            case 0x485c80: StatusBuffChild(memory, services).spawn(args.at(0)); break;
            case 0x467bcf: LiftNumberChild(memory, services).spawn(args.at(0)); break;
            case 0x46ff7a: DescendingBurst(memory, services).spawn(args.at(0)); break;
            case 0x46ff40: DescendingBurst(memory, services).tick(args.at(0)); break;
            case 0x467be6: TargetFanoutSequence(memory, services).all_target_lift_numbers(args.at(0), args.at(1)); break;
            case 0x46fffd: TargetFanoutSequence(memory, services).descending_particle_burst(args.at(0), args.at(1)); break;
            case 0x483179: TargetFanoutSequence(memory, services).single_target_particle_attack(args.at(0), std::int32_t(args.at(1))); break;
            case 0x482f4d: TargetFanoutSequence(memory, services).multitarget_particle_attack(args.at(0), std::int32_t(args.at(1))); break;
            case 0x4693ef: TargetFanoutSequence(memory, services).first_target_directional_burst(args.at(0), args.at(1)); break;
            case 0x469321: result = DirectionalHitParticles(memory, services).spawn_directional_expand(args.at(0), args.at(1)); break;
            case 0x467ed6: TargetFanoutSequence(memory, services).target_flash_drift(args.at(0), args.at(1)); break;
            case 0x467e8c: TargetFlashNotify(memory, services).spawn(args.at(0)); break;
            case 0x467e47: TargetFlashNotify(memory, services).tick(args.at(0)); break;
            case 0x47be07: TargetFanoutSequence(memory, services).recover_targets(args.at(0), args.at(1)); break;
            case 0x485ccf: TargetFanoutSequence(memory, services).shared_status_buff(args.at(0), args.at(1)); break;
            case 0x4692e5: FinishingEffects(memory, services).directional_expand_impact(args.at(0)); break;
            case 0x46d24c: FinishingEffects(memory, services).projectile_impact(args.at(0)); break;
            case 0x46ed0e: FinishingEffects(memory, services).particle_impact(args.at(0)); break;
            case 0x46f1f6: FinishingEffects(memory, services).dual_notify_impact(args.at(0)); break;
            case 0x47fcb7: FinishingEffects(memory, services).homing_impact(args.at(0)); break;
            case 0x47f526: FinishingEffects(memory, services).homing_impact_with_cue(args.at(0)); break;
            case 0x47dc33: FinishingEffects(memory, services).homing_impact_on_first_target(args.at(0)); break;
            case 0x483380: FinishingEffects(memory, services).homing_impact_on_link(args.at(0)); break;
            case 0x4823e1: FinishingEffects(memory, services).homing_impact_on_link_with_cue(args.at(0)); break;
            case 0x484f62: FinishingEffects(memory, services).homing_impact_after_delay(args.at(0)); break;
            case 0x46e013: FinishingEffects(memory, services).directional_trail_impact(args.at(0)); break;
            case 0x4837df: FinishingEffects(memory, services).arc_projectile_impact(args.at(0)); break;
            case 0x47219f: FinishingEffects(memory, services).rising_sprite_notify(args.at(0)); break;
            default: {
                DirectionalHitParticles particles(memory, services);
                const auto source = args.at(0), destination = args.at(1);
                switch (entry) {
                case 0x46d904: result = particles.spawn_hit(source, destination); break;
                case 0x46dcd7: result = particles.spawn_double_hit(source, destination); break;
                case 0x46f684: result = particles.spawn_high_hit(source, destination); break;
                case 0x46f92f: result = particles.spawn_flat_cue(source, destination); break;
                case 0x46a8e9: result = particles.spawn_slow_arc_hit(source, destination); break;
                case 0x46b772: result = particles.spawn_fast_arc_trigger_hit(source, destination); break;
                case 0x46e344: result = particles.spawn_deferred_trigger(source, destination); break;
                case 0x46e8b3: result = particles.spawn_facing_deferred_trigger(source, destination); break;
                case 0x470b90: result = particles.spawn_poison_liquid(source, destination); break;
                default: throw std::runtime_error("unknown effect object entry");
                }
                break;
            }
            }

            const auto actual = memory.bytes(0x4a5000, expected.size());
            // Compared where the original leaves a defined value: 467417's angle
            // carry and the three launchers' child. The tick entries run as
            // pumped object callbacks, whose result the pump discards. Each of
            // the nine travel helpers has exactly one caller and every one
            // discards EAX on the instruction after the call, so those return
            // the child they built rather than the value the original happens
            // to leave behind; nothing reads either.
            const bool compares_result = entry == 0x467417 || entry == 0x4687f3 ||
                                         entry == 0x46bd92 || entry == 0x47c807;
            if (actual != expected || calls != expected_calls ||
                (compares_result && result != wanted)) {
                std::cerr << std::hex;
                for (std::size_t i = 0, shown = 0; i < actual.size() && shown < 6; ++i)
                    if (actual[i] != expected[i]) {
                        ++shown;
                        std::cerr << "byte at=" << 0x4a5000 + i << " actual=" << unsigned(actual[i])
                                  << " expected=" << unsigned(expected[i]) << '\n';
                    }
                if (compares_result && result != wanted)
                    std::cerr << "result actual=" << result << " expected=" << wanted << '\n';
                const auto show = [](const char* label, const std::vector<Call>& list) {
                    std::cerr << label;
                    for (const auto& [address, taken] : list) {
                        std::cerr << ' ' << address << '(';
                        for (const auto value : taken) std::cerr << value << ' ';
                        std::cerr << ')';
                    }
                    std::cerr << '\n';
                };
                if (calls != expected_calls) { show("calls   ", calls); show("expected", expected_calls); }
                std::cerr << std::dec;
                throw std::runtime_error("effect object case " + std::to_string(index) +
                                         " entry=" + std::to_string(entry) + " mismatch");
            }
        }
        if (cursor != bytes.size()) throw std::runtime_error("unconsumed effect object fixture");
        std::cout << "effect_object_original_cases=" << count << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
