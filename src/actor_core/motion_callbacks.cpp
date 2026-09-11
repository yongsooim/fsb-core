#include "fsb_core/actor_core/motion_callbacks.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/actor_core/actor_runtime.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
// The oscillation parameters, one triple per axis.
struct OscillationAxis {
    unsigned position;   // The lane the shape drives.
    unsigned velocity;   // Accumulated delta, for the ping-pong shape.
    unsigned step;       // How much the delta changes each frame.
    unsigned centre;     // The limit it reverses at, and the wave's centre.
    unsigned amplitude;
    unsigned angle;
};
constexpr OscillationAxis oscillation_axes[] = {
    {actor_offset::world_x, 0x16c, 0x178, 0x184, 0x190, 0x19c},
    {actor_offset::world_y, 0x170, 0x17c, 0x188, 0x194, 0x1a0},
    {actor_offset::elevation, 0x174, 0x180, 0x18c, 0x198, 0x1a4},
};
constexpr std::uint32_t axis_enable[] = {oscillation_axis_x, oscillation_axis_y, oscillation_axis_z};

std::int32_t at(const GuestWords& words, Address object, unsigned offset) {
    return signed32(words.read(object + offset));
}
void put(const GuestWords& words, Address object, unsigned offset, std::int32_t value) {
    words.write(object + offset, std::uint32_t(value));
}
// The original multiplies the Q16 wave by the amplitude and keeps the low32.
std::int32_t scaled(std::uint32_t wave, std::int32_t amplitude) {
    return std::int32_t(wave * std::uint32_t(amplitude));
}
} // namespace

void tick_entrance_dive(const GuestWords& words, const Memory& memory, Address object) {
    put(words, object, actor_offset::tile_x, at(words, object, actor_offset::tile_x_q16) / 0x10000);
    put(words, object, actor_offset::tile_y, at(words, object, actor_offset::tile_y_q16) / 0x10000);
    const auto step = at(words, object, dive_step);
    if (step < at(words, object, dive_length)) {
        const auto next = step + 1;
        put(words, object, dive_step, next);
        // Past the apex step the arc is over and the actor sits at ground level.
        put(words, object, actor_offset::elevation,
            next > dive_last_arc_step ? 0 : std::int32_t((dive_apex - next * next) << 16));
        words.write(object + actor_offset::motion_frame,
                    memory.read(dive_frame_table + std::uint32_t(next) * 4));
    } else {
        // The dive is done: hand the record to the default visual callback.
        words.write(object + actor_offset::callback, 0x45c526);
        words.write(object + path_active_marker, 0);
        words.write(object + dive_length, 0);
        words.write(object + actor_offset::motion_state, 0);
    }
    const auto pixels = tile_to_pixels(at(words, object, actor_offset::tile_x_q16),
                                       at(words, object, actor_offset::tile_y_q16));
    put(words, object, actor_offset::world_x, pixels.x);
    put(words, object, actor_offset::world_y, pixels.y);
    words.write(object + actor_offset::sprite_selector, dive_sprite_selector);
    words.write(object + actor_offset::sprite_frame,
                memory.read(tables::direction_to_cardinal + words.read(object + actor_offset::facing) * 4) +
                    words.read(object + actor_offset::motion_frame) * 4);
}

void tick_oscillation(const GuestWords& words, Address object,
                      const FixedWave& sine, const FixedWave& cosine) {
    const auto mode = words.read(object + oscillation_mode);
    const bool accumulate = mode & oscillation_accumulate;
    const bool absolute = !accumulate && (mode & oscillation_absolute);
    const bool relative = !accumulate && !absolute && (mode & oscillation_relative);
    // No shape selected means the routine returns without even refreshing the
    // tile lanes, so the early exit has to come before the tail below.
    if (!accumulate && !absolute && !relative) return;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!(mode & axis_enable[axis])) continue;
        const auto& lane = oscillation_axes[axis];
        if (accumulate) {
            // The delta reverses once the axis has passed its limit.
            auto step = words.read(object + lane.step);
            if (at(words, object, lane.position) >= at(words, object, lane.centre)) step = 0u - step;
            // Original NEG/ADD keep the low32 bits, including INT_MIN and carries.
            words.write(object + lane.velocity, words.read(object + lane.velocity) + step);
            words.write(object + lane.position,
                        words.read(object + lane.position) + words.read(object + lane.velocity));
            continue;
        }
        // X rides a sine, Y and Z ride a cosine, and Y is measured downward.
        const auto& wave = axis == 0 ? sine : cosine;
        const auto offset = scaled(wave(words.read(object + lane.angle)),
                                   at(words, object, lane.amplitude));
        const auto base = words.read(object + (absolute ? lane.centre : lane.position));
        const auto delta = std::uint32_t(offset);
        words.write(object + lane.position, axis == 1 ? base - delta : base + delta);
    }
    put(words, object, actor_offset::tile_x_q16,
        at(words, object, actor_offset::world_x) / tile_width_pixels);
    put(words, object, actor_offset::tile_y_q16,
        at(words, object, actor_offset::world_y) / tile_height_pixels);
}

void tick_scripted_path(const GuestWords& words, Memory& memory, Address object,
                        const StepActorMotionByte& step, const ResolveActorFrame& resolve_frame) {
    const auto finish = [&] { if (resolve_frame) resolve_frame(object); };
    if (step && (step(object) & 0xff)) return finish();
    if (words.read(object + actor_offset::motion_state)) return finish();
    const auto tile_x_q16 = at(words, object, actor_offset::tile_x_q16);
    const auto tile_y_q16 = at(words, object, actor_offset::tile_y_q16);
    put(words, object, actor_offset::tile_x, tile_x_q16 / 0x10000);
    put(words, object, actor_offset::tile_y, tile_y_q16 / 0x10000);
    if (words.read(object + path_active_marker) != path_running) return finish();
    // Bit14 of the flag word parks the walk without ending it.
    if (words.read(object + actor_offset::flags) & 0x4000) return finish();
    const auto cursor = at(words, object, path_cursor_index);
    if (cursor >= at(words, object, path_length)) {
        words.write(object + path_active_marker, 0);
        words.write(object + path_length, 0);
        // In field mode the actor goes back to being driven by input.
        if (memory.read(globals::game_mode) == 3)
            words.write(object + actor_offset::callback, 0x458ed7);
        return finish();
    }
    // Commands are packed two per word; word access picks out the half.
    const auto slot = object + actor_offset::path_commands + std::uint32_t(cursor) * 2;
    const auto packed = words.read(slot & ~3u);
    const auto command = (slot & 2) ? (packed >> 16) : (packed & 0xffffu);
    words.write(object + path_cursor_index, std::uint32_t(cursor) + 1);
    const auto kind = command & 0xff00;
    if (kind == path_command_turn) {
        // Turn in place: the two nibbles are the facing and the facing to reach.
        words.write(object + actor_offset::motion_state, 5);
        words.write(object + actor_offset::facing, (command >> 4) & 0xf);
        words.write(object + actor_offset::target_facing, command & 0xf);
        return finish();
    }
    if (!(kind & path_command_step_bit)) return finish();
    const auto direction = command & 0xf;
    std::uint32_t distance = 1;
    if (kind == path_command_long_step) {
        distance = 2;
        words.write(object + actor_offset::motion_state, 2);
    } else if (kind == path_command_step) {
        words.write(object + actor_offset::motion_state, 1);
    }
    words.write(object + actor_offset::motion_frame, 1);
    words.write(object + actor_offset::facing, direction);
    const auto shift = std::int32_t(distance << path_step_shift);
    switch (direction) {
    case 0: put(words, object, actor_offset::tile_y_q16, tile_y_q16 - shift);
            put(words, object, actor_offset::tile_y, at(words, object, actor_offset::tile_y) - 1); break;
    case 1: put(words, object, actor_offset::tile_y_q16, tile_y_q16 + shift);
            put(words, object, actor_offset::tile_y, at(words, object, actor_offset::tile_y) + 1); break;
    case 2: put(words, object, actor_offset::tile_x_q16, tile_x_q16 - shift);
            put(words, object, actor_offset::tile_x, at(words, object, actor_offset::tile_x) - 1); break;
    case 3: put(words, object, actor_offset::tile_x_q16, tile_x_q16 + shift);
            put(words, object, actor_offset::tile_x, at(words, object, actor_offset::tile_x) + 1); break;
    default: break; // Diagonals are not steppable here; the command is dropped.
    }
    return finish();
}

namespace {
// The four arrow directions, in the order the facing field numbers them.
// Each has a state that is set while the key is down and a companion that is
// set on the frame it goes down, so either one starts a move.
struct CursorDirection {
    Address held, pressed;
    unsigned lane;       // The Q16 lane the move walks.
    std::int32_t sign;   // Which way along it.
    Address limit;       // The grid extent that bounds this axis.
};
constexpr CursorDirection cursor_directions[] = {
    {globals::up_held, 0x6da688, actor_offset::tile_y_q16, -1, 0},
    {globals::down_held, 0x6da6a8, actor_offset::tile_y_q16, +1, globals::grid_height},
    {globals::left_held, 0x6da694, actor_offset::tile_x_q16, -1, 0},
    {globals::right_held, 0x6da69c, actor_offset::tile_x_q16, +1, globals::grid_row_stride},
};
} // namespace

void tick_grid_cursor(Memory& memory, const GuestWords& words, Address object) {
    if (!(words.read(object + actor_offset::flags) & 0x40)) return;
    const auto tile = [&](unsigned lane) {
        return at(words, object, lane) / 0x10000;
    };
    const auto walk = [&](const CursorDirection& direction) {
        put(words, object, direction.lane,
            at(words, object, direction.lane) + direction.sign * cursor_quarter_tile);
    };
    const auto hold = signed32(words.read(object + cursor_hold));
    if (!hold) {
        // Accepting input: take the first quarter tile and start the hold.
        for (unsigned index = 0; index < 4; ++index) {
            const auto& direction = cursor_directions[index];
            if (!memory.read(direction.held) && !memory.read(direction.pressed)) continue;
            // Stay one tile inside the grid on the side we are heading for.
            const auto position = tile(direction.lane);
            if (direction.sign < 0) {
                if (position <= 1) continue;
            } else if (position >= signed32(memory.read(direction.limit)) - cursor_edge_margin) {
                continue;
            }
            words.write(object + actor_offset::facing, index);
            walk(direction);
            words.write(object + cursor_hold, 1);
            break;
        }
    } else if (hold > 0) {
        // Holding: keep walking the direction already chosen, and step the
        // counter so it lands back on zero after the fourth quarter.
        words.write(object + cursor_hold, std::uint32_t((hold + 1) % cursor_hold_frames));
        const auto facing = words.read(object + actor_offset::facing);
        if (facing < 4) walk(cursor_directions[facing]);
    }
    // The cursor blinks between two frames every tick, regardless of movement.
    const auto blink = words.read(object + actor_offset::motion_frame);
    words.write(object + actor_offset::sprite_selector, cursor_sprite_selector);
    words.write(object + actor_offset::sprite_frame, blink + cursor_sprite_base_frame);
    words.write(object + actor_offset::motion_frame, (blink + 1) % cursor_blink_frames);
    const auto pixels = tile_to_pixels(at(words, object, actor_offset::tile_x_q16),
                                       at(words, object, actor_offset::tile_y_q16));
    put(words, object, actor_offset::world_x, pixels.x);
    put(words, object, actor_offset::world_y, pixels.y);
}
} // namespace fsb::core::actor_core
