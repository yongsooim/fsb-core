#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>

namespace fsb::core::visual_effects {

// Everything the visual effect logic calls that this session does not own.
// Each entry names the original address it stands for so the integration
// session can bind it to whichever implementation is current: an already
// reconstructed C++ function, another worker's reconstruction once it lands,
// or the existing generated body in the meantime.
//
// Nothing here is optional in the sense of "skip it when unbound". A call
// through an unbound service raises Fault, because silently returning zero
// would invent an outcome the original never produces.
struct EffectServices {
    // --- Runtime callback objects (A_ACTORS) ---
    std::function<Address(Address callback)> spawn_object;       // 45d89c
    std::function<void(Address object)> finalize_object;         // 45d91d
    std::function<void(Address object)> apply_oscillation;       // 45d208
    std::function<void(Address object)> snapshot_motion_block;   // 45db53
    std::function<void(Address object)> restore_motion_block;    // 45db6c
    std::function<void(Address object, std::int32_t substate)> invoke_callback_substate; // 45db85
    std::function<void(Address object, std::int32_t x, std::int32_t y)> set_tile_position; // 45d774

    // --- Battle spawn and dispatch helpers (B_COMBAT) ---
    std::function<Address(Address source, std::int32_t amount)> spawn_floating_number; // 4646be
    std::function<Address(Address source, std::int32_t amount, std::int32_t style)> spawn_floating_number_alt; // 464b74
    std::function<Address(Address source, std::int32_t amount)> spawn_floating_number_glyphset; // 4647ad
    std::function<Address(Address source, std::int32_t amount, std::int32_t glyphset)> spawn_floating_number_glyphset_alt; // 464925
    // 464c27: place an effect object at an explicit point, wearing `sprite`,
    // running the script the descriptor names.
    std::function<Address(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t sprite,
                          Address descriptor)> spawn_effect_object;
    // 464cf4: the same as spawn_effect_object, plus the code the object reports
    // to its controller when it finishes.
    std::function<Address(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t sprite,
                          Address descriptor, std::int32_t notify)> spawn_effect_object_with_notify;
    std::function<void(Address object)> spawn_swirl_particle;    // 466cfc
    std::function<Address(Address source)> spawn_debris_burst;   // 465b66
    std::function<std::int32_t(std::int32_t mode, std::int32_t frames)> screen_flash_transition; // 464c59
    std::function<void(Address object)> apply_homing_motion_clamp; // 462de2
    std::function<std::int32_t(Address object, Address target)> distance_to_target; // 462f5b
    std::function<std::int32_t(std::int32_t from, std::int32_t to, std::int32_t span)> tween_axis_slope; // 463038
    std::function<std::int32_t(std::int32_t from, std::int32_t to, std::int32_t span)> tween_axis_slope_inverse; // 4630ed
    std::function<void(Address object, std::int32_t angle, std::int32_t radius)> polar_offset_step; // 4631a2
    std::function<void(std::int32_t index)> release_marker_slot; // 44c2f4
    std::function<bool()> fade_transition_idle;                  // 4622b6

    // Battle action dispatchers the skill sequences hand control to (B_COMBAT).
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> melee_action_dispatch;   // 464d59
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> status_phase_gate;       // 46542d
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> ground_burst_dispatch;   // 466aaf
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> scatter_dispatch;        // 465b7d
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> orbit_dispatch;          // 467111
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> fountain_dispatch;       // 4665e1
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> explosion_dispatch;      // 4660c4
    std::function<std::uint32_t(Address sequence, std::int32_t a, std::int32_t b)> beam_dispatch;           // 4658de

    // --- Already reconstructed logic and platform services ---
    std::function<void(Address object, Address script)> run_effect_script; // 447841, EffectScript::start
    std::function<void(Address object, std::int32_t state)> notify_controller; // 461d25
    std::function<std::int32_t()> random;                        // 498090, crt_rand
    std::function<void(unsigned cue)> play_cue, stop_cue;        // 435373, 4353cb
    std::function<void(std::int32_t mode)> palette_fade_begin;   // 40536b
    std::function<bool()> palette_fade_busy;                     // 405572
    std::function<void(std::int32_t first, std::int32_t count, std::int32_t scale)> palette_copy_scaled; // 404dbb
    std::function<void()> clamp_camera_to_map;                   // 453fe3
};

}
