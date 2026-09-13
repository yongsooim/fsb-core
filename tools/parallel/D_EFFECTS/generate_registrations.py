#!/usr/bin/env python3
"""Export reconstructed effect entry points. --apply also updates the registry."""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BRIDGE = 'd_effects_bridge.cpp'


# Hand written reconstructions, with the bridge that reaches each one.
DIRECT = {
    '0x467417': 'RingEffect::spawn',
    '0x4672b5': 'RingEffect::tick',
    '0x4674de': 'SweepController::run',
    '0x46dca5': 'DirectionalHitParticles::tick',
    '0x46d904': 'DirectionalHitParticles::spawn_hit',
    '0x46dcd7': 'DirectionalHitParticles::spawn_double_hit',
    '0x46f684': 'DirectionalHitParticles::spawn_high_hit',
    '0x46f92f': 'DirectionalHitParticles::spawn_flat_cue',
    '0x46a8e9': 'DirectionalHitParticles::spawn_slow_arc_hit',
    '0x46b772': 'DirectionalHitParticles::spawn_fast_arc_trigger_hit',
    '0x46e344': 'DirectionalHitParticles::spawn_deferred_trigger',
    '0x46e8b3': 'DirectionalHitParticles::spawn_facing_deferred_trigger',
    '0x470b90': 'DirectionalHitParticles::spawn_poison_liquid',
    '0x468790': 'Projectiles::tick',
    '0x4687f3': 'Projectiles::launch',
    '0x46bd92': 'Projectiles::launch_arc',
    '0x47c807': 'Projectiles::launch_directional',
    '0x46a1b6': 'AnimatedParticle::tick',
    '0x467db3': 'DriftEffect::spawn',
    '0x4681d6': 'DebrisEffect::spawn',
    '0x4860ff': 'RisingEffect::spawn',
    '0x481b11': 'FinishingEffects::release_after_twenty_ticks',
    '0x46dfc3': 'FinishingEffects::release_when_script_ends',
    '0x46c5bb': 'FinishingEffects::travel_then_release',
    '0x4719e2': 'FinishingEffects::drift_until_script_ends',
    '0x4705c6': 'FinishingEffects::report_hit_when_script_ends',
    '0x478c12': 'FinishingEffects::report_actor_done_when_script_ends',
    '0x485c52': 'FinishingEffects::report_owner_hit_when_script_ends',
    '0x47eb35': 'FinishingEffects::travel_then_report_done',
    '0x4727b4': 'FinishingEffects::report_ready_when_script_ends',
    '0x482bd8': 'FinishingEffects::scatter_drift_when_script_ends',
    '0x4682cc': 'FinishingEffects::drop_then_scatter_debris',
    '0x48613c': 'FinishingEffects::fall_then_raise_successor',
    '0x4692c6': 'FinishingEffects::stop_screen_shake',
    '0x46dfdf': 'TrailSprite::spawn',
    '0x4692e5': 'FinishingEffects::directional_expand_impact',
    '0x46d24c': 'FinishingEffects::projectile_impact',
    '0x46ed0e': 'FinishingEffects::particle_impact',
    '0x46f1f6': 'FinishingEffects::dual_notify_impact',
    '0x47fcb7': 'FinishingEffects::homing_impact',
    '0x47f526': 'FinishingEffects::homing_impact_with_cue',
    '0x47dc33': 'FinishingEffects::homing_impact_on_first_target',
    '0x483380': 'FinishingEffects::homing_impact_on_link',
    '0x4823e1': 'FinishingEffects::homing_impact_on_link_with_cue',
    '0x484f62': 'FinishingEffects::homing_impact_after_delay',
    '0x46e013': 'FinishingEffects::directional_trail_impact',
    '0x4837df': 'FinishingEffects::arc_projectile_impact',
    '0x47219f': 'FinishingEffects::rising_sprite_notify',
    '0x47be07': 'TargetFanoutSequence::recover_targets',
    '0x485ccf': 'TargetFanoutSequence::shared_status_buff',
    '0x47bde6': 'RecoverTargetChild::spawn',
    '0x485c80': 'StatusBuffChild::spawn',
    '0x467be6': 'TargetFanoutSequence::all_target_lift_numbers',
    '0x46fffd': 'TargetFanoutSequence::descending_particle_burst',
    '0x467bcf': 'LiftNumberChild::spawn',
    '0x46ff7a': 'DescendingBurst::spawn',
    '0x46ff40': 'DescendingBurst::tick',
    '0x483179': 'TargetFanoutSequence::single_target_particle_attack',
    '0x482f4d': 'TargetFanoutSequence::multitarget_particle_attack',
    '0x4693ef': 'TargetFanoutSequence::first_target_directional_burst',
    '0x469321': 'DirectionalHitParticles::spawn_directional_expand',
    '0x467ed6': 'TargetFanoutSequence::target_flash_drift',
    '0x467e8c': 'TargetFlashNotify::spawn',
    '0x467e47': 'TargetFlashNotify::tick',
}
OBJECT_BRIDGE = 'effect_objects_bridge.cpp'


def rows():
    """Reconstructed effect entry points and their implementations."""
    table = (ROOT / 'src/visual_effects/skill_callback_table.inc').read_text().splitlines()
    for line in table:
        if not line.startswith('{0x'):
            continue
        entry = line.split(',', 1)[0].lstrip('{')
        yield entry, {
            'implementation': 'SkillCallbacks::invoke',
            'bridge': BRIDGE,
            'stage': 'native_logic_with_legacy_callers',
        }
    for entry, implementation in DIRECT.items():
        yield entry, {
            'implementation': implementation,
            'bridge': OBJECT_BRIDGE,
            'stage': 'native_logic_with_legacy_callers',
        }


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='Registration fragment destination')
    parser.add_argument('--apply', action='store_true', help='Also update tools/native_reconstructions.json')
    args = parser.parse_args(argv)
    fragment = dict(rows())
    destination = args.output
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps({
        'description': 'D_EFFECTS entry points reconstructed as semantic C++. Merge these '
                       'into the entries object of tools/native_reconstructions.json.',
        'bridge_source': f'src/visual_effects/{BRIDGE}',
        'entries': fragment,
    }, indent=2) + '\n')
    print(f'registrations {len(fragment)} -> {destination}')

    if args.apply:
        registry_path = ROOT / 'tools/native_reconstructions.json'
        registry = json.loads(registry_path.read_text())
        registry['entries'].update(fragment)
        registry_path.write_text(json.dumps(registry, indent=2) + '\n')
        print(f'merged into {registry_path.relative_to(ROOT)}, now {len(registry["entries"])} entries')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
