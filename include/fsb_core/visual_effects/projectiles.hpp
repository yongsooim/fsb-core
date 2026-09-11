#pragma once
#include "fsb_core/visual_effects/effect_object.hpp"
#include "fsb_core/visual_effects/effect_services.hpp"

namespace fsb::core::visual_effects {

// motion_flags the three launchers write. 462de2 clears the upper three bits
// when the projectile reaches its anchor.
inline constexpr std::int32_t projectile_motion_flags = 0x71;
inline constexpr std::int32_t projectile_in_flight_bits = 0x70;

// A projectile that flies from an origin to a target and reports on landing.
//
// The launcher gives the dominant axis a fixed speed chosen by the origin's
// facing, works out how many frames that takes, then divides the other two
// axes by the same count so all three arrive together.
class Projectiles {
public:
    Projectiles(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}

    // 468790. Waits for the launch script to finish, then flies.
    void tick(Address projectile);

    // The three launchers, by original entry point.
    Address launch(Address target, Address origin);              // 4687f3
    Address launch_arc(Address target, Address origin);          // 46bd92
    Address launch_directional(Address target, Address origin);  // 47c807

    static constexpr Address child_callback = 0x468790;

private:
    // Everything the three do before the facing decides the flight.
    Address begin(Address target, Address origin, std::int32_t offset_x, std::int32_t offset_y,
                  std::int32_t lift);
    // Give one axis `speed`, then scale the other two to land on the same frame.
    // `clamp_frames` keeps the flight at least one frame long, which only the
    // directional launcher does.
    void aim(Address projectile, bool along_x, std::int32_t speed, bool clamp_frames);
    // A jitter of `spread` steps, shifted down by `bias`, in whole q16 units.
    std::int32_t jitter(Address origin, std::int32_t spread, std::int32_t bias);
    void finish(Address projectile, Address target, bool aimed, bool along_x);

    Memory& memory_;
    const EffectServices& services_;
};

}
