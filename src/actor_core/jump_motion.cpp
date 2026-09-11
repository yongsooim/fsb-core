#include "fsb_core/actor_core/jump_motion.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/actor_core/player_input.hpp"
#include "fsb_core/actor_core/field_interaction.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {

// Five tables the original rebuilds on its own stack every frame. They are
// five distinct tables but they live in one block, one after the other, and
// the code does read off the end of one into the head of the next, so the
// block is reproduced whole and the tables are named as views into it.
//
//   low arc / high arc  how high the actor is carried while it crosses a
//                       ledge, indexed by frames of the arc left to run; the
//                       low arc stops on a value, the high one settles to zero
//   step speed          how far it travels on each of the first 0x1d frames,
//                       in sixteenths of a tile, accelerating from one to four
//   short hop height    the ten-frame hop a stair plays, by frames remaining
//   short hop forward   and how far forward that hop carries, in Q16
//
// The block reserves one more slot after the last table that nothing writes
// and nothing reaches.
constexpr std::int32_t motion_block[113] = {
    // low arc [0x00]
    12, 16, 12, 0, 20, 28, 32, 28, 20, 0, 30, 50, 66, 78, 86, 91, 94,
    96, 97, 98, 99, 99, 99, 98, 97, 96, 94, 91, 86, 78, 66, 50, 30,
    // high arc [0x21]
    12, 16, 12, 0, 20, 28, 32, 28, 20, 0, 30, 50, 66, 78, 86,
    91, 94, 96, 97, 98, 97, 96, 94, 91, 86, 78, 66, 50, 30, 0,
    // step speed [0x3f]
    1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1, 2, 2, 2,
    2, 3, 2, 3, 2, 3, 3, 3, 4, 3, 4, 4, 4, 4,
    // short hop height [0x5c]
    0, 2, 4, 0, 10, 14, 16, 14, 10, 0,
    // short hop forward [0x66]
    0, 0, -0xa00, -0x1100, -0x1700, -0x1e00, -0x2800, -0x3800, -0x5000, 0,
    // unused [0x70]
    0,
};
constexpr const std::int32_t* low_arc = motion_block;
constexpr const std::int32_t* high_arc = motion_block + 0x21;
constexpr const std::int32_t* step_speed = motion_block + 0x3f;
constexpr const std::int32_t* short_hop_height = motion_block + 0x5c;
constexpr const std::int32_t* short_hop_forward = motion_block + 0x66;

// One grid cell of the map, and the tile the actor is standing on.
struct Cell {
    std::int32_t index = 0;      // Cell number within the whole attribute plane.
    std::int32_t map_width = 0;
    std::int32_t row_offset = 0; // map_width * tile_y, the row this cell is on.
    std::int32_t layer_base = 0; // First cell of the layer the actor is on.
    std::int32_t tile_x = 0, tile_y = 0;
    std::uint32_t attributes = 0;
};

} // namespace

void tick_jump_motion(Memory& memory, const GuestWords& words, Address object,
                      const JumpMotionSounds& sound) {
    const auto field = [&](unsigned at) { return words.read(object + at); };
    const auto put = [&](unsigned at, std::uint32_t value) { words.write(object + at, value); };
    const auto number = [&](unsigned at) { return signed32(field(at)); };
    const auto add = [&](unsigned at, std::int32_t delta) {
        put(at, std::uint32_t(number(at) + delta));
    };
    // +0x3c is sixteen bits wide here; the upper half belongs to the path
    // command block this record is not using while it jumps.
    const auto submode = [&] { return field(actor_offset::path_commands) & 0xffffu; };
    const auto set_submode = [&](std::uint32_t value) {
        put(actor_offset::path_commands,
            (field(actor_offset::path_commands) & 0xffff0000u) | (value & 0xffffu));
    };
    const auto cue = [&](std::uint32_t which) { if (sound.play) sound.play(which); };
    const auto attributes_of = [&](std::int32_t index) {
        return memory.read(globals::tile_attributes + std::uint32_t(index) * 4);
    };
    const auto occupancy_of = [&](std::int32_t index) {
        return memory.read(globals::tile_occupancy + std::uint32_t(index) * 4);
    };

    Cell cell;
    cell.tile_x = number(actor_offset::tile_x_q16) / jump_fixed_one;
    cell.tile_y = number(actor_offset::tile_y_q16) / jump_fixed_one;
    const auto layer = number(actor_offset::layer_q16) / jump_fixed_one;
    cell.map_width = signed32(
        memory.read(globals::map_layer_width + std::uint32_t(layer) * jump_layer_descriptor_bytes));
    cell.row_offset = cell.map_width * cell.tile_y;
    cell.layer_base = layer * jump_layer_cells;
    cell.index = cell.layer_base + cell.tile_x + cell.row_offset;
    cell.attributes = attributes_of(cell.index);

    const auto state = number(actor_offset::motion_state);

    // --- turn on the spot -------------------------------------------------
    if (state == jump_state_turning) {
        if (field(actor_offset::target_facing) == field(actor_offset::facing)) {
            put(actor_offset::motion_state, std::uint32_t(jump_state_settled));
        } else {
            const auto stepped =
                step_turn_arc(memory.read(direction_to_arc + field(actor_offset::facing) * 4),
                              memory.read(direction_to_arc + field(actor_offset::target_facing) * 4));
            put(actor_offset::facing,
                memory.read(arc_to_direction + (stepped % turn_arc_steps) * 4));
        }
    } else if (state == jump_state_hop || state == jump_state_side_step) {
        // --- the sixteen-frame hop and the sixteen-frame side-step --------
        // They differ only in who owns the actor afterwards.
        const auto frame = (number(actor_offset::motion_frame) + 1) % jump_hop_frames;
        put(actor_offset::motion_frame, std::uint32_t(frame));
        if (frame == 0) {
            put(actor_offset::frame_group, (field(actor_offset::frame_group) - 1) & 1);
            add(actor_offset::elevation, -signed32(memory.read(jump_hop_rise_table)));
            if (state == jump_state_hop) {
                put(actor_offset::motion_state, std::uint32_t(jump_state_settled));
            } else {
                put(actor_offset::motion_state, std::uint32_t(jump_state_normal));
                put(actor_offset::callback, routines::field_actor_movement);
            }
        }
        // Both of them drift sideways every frame, in whichever of the two
        // sideways directions the actor is facing.
        if (field(actor_offset::facing) == jump_continue_left)
            add(actor_offset::tile_x_q16, -jump_nudge);
        else if (field(actor_offset::facing) == jump_continue_right)
            add(actor_offset::tile_x_q16, jump_nudge);
    } else if (state == jump_state_arc) {
        // --- the arc the tile chose ---------------------------------------
        std::int32_t step = 0;
        put(actor_offset::elevation, 0);
        const auto frame = number(actor_offset::motion_frame);
        if (frame < jump_arc_table_frames) {
            // Still on the fixed run-up; nothing else is decided yet.
            step = step_speed[frame] << 12;
        } else if (frame == number(actor_offset::frame_group)) {
            // The run-up has landed on the tile that decides what happens next.
            auto target = number(actor_offset::frame_group);
            // Restart the flat arc, three frames long, without touching facing.
            const auto restart_flat_fast = [&] {
                step = jump_third_step_up;
                set_submode(jump_arc_flat_fast);
                put(actor_offset::frame_group, std::uint32_t(target + 3));
            };
            switch ((cell.attributes >> jump_tile_arc_shift) & jump_tile_arc_mask) {
            case jump_tile_arc_swap_or_flat: {
                // The tile names an entry and an exit side. An actor arriving
                // on the entry side leaves on the exit side; anything else just
                // keeps going.
                const auto facing = number(actor_offset::facing);
                const auto entry = (cell.attributes >> jump_tile_entry_dir_shift) & 7;
                const auto exit = cell.attributes >> jump_tile_exit_dir_shift;
                bool swapped = false;
                if (facing < 4) {
                    if (std::uint32_t(facing) == entry) {
                        put(actor_offset::facing, exit);
                        swapped = true;
                    }
                } else if (memory.read(tables::opposite_directions + std::uint32_t(facing) * 4) == exit) {
                    // A diagonal arrival is matched through the same table the
                    // field code turns a facing around with.
                    put(actor_offset::facing, memory.read(tables::opposite_directions + entry * 4));
                    restart_flat_fast();
                    break;
                } else {
                    swapped = true;
                }
                if (!swapped) { restart_flat_fast(); break; }
                // Four more frames at a quarter tile each: one whole tile.
                set_submode(jump_arc_flat);
                step = jump_quarter_step;
                put(actor_offset::frame_group, std::uint32_t(target + 4));
                break;
            }
            case jump_tile_arc_small_up:
                if (sound.stop) sound.stop(jump_cue_step);
                cue(jump_cue_land_small);
                add(actor_offset::frame_group, 0x1e);
                set_submode(jump_arc_small_up);
                step = jump_third_step_small;
                break;
            case jump_tile_arc_large_up:
                if (sound.stop) sound.stop(jump_cue_step);
                cue(jump_cue_land_large);
                add(actor_offset::frame_group, 0x21);
                set_submode(jump_arc_large_up);
                step = jump_third_step_up;
                // While the actor is still on the way up it also climbs.
                if (frame + 9 < number(actor_offset::frame_group))
                    add(actor_offset::tile_y_q16, jump_third_step_rise);
                break;
            case jump_tile_arc_short_table:
                if (sound.stop) sound.stop(jump_cue_step);
                cue(jump_cue_land_large);
                add(actor_offset::frame_group, 10);
                // Only an actor that came in on the tile's own entry side gets
                // the short hop; anyone else stops dead here.
                if (field(actor_offset::facing) ==
                    ((cell.attributes >> jump_tile_entry_dir_shift) & 7))
                    set_submode(jump_arc_short_table);
                break;
            case jump_tile_arc_flat_fast:
                restart_flat_fast();
                break;
            default:
                break;
            }
        } else {
            // Somewhere in the middle of an arc that has already been chosen.
            const auto target = number(actor_offset::frame_group);
            switch (submode()) {
            case jump_arc_flat:
                step = jump_quarter_step;
                break;
            case jump_arc_small_up:
            case jump_arc_large_up:
                if (target == frame + 9) cue(jump_cue_mid_air);
                if (target == frame + jump_first_frame) cue(jump_cue_step);
                step = jump_third_step;
                if (submode() == jump_arc_large_up && frame + 9 < target)
                    add(actor_offset::tile_y_q16, jump_third_step);
                break;
            case jump_arc_short_table: {
                const auto left = target - frame;
                step = short_hop_forward[left];
                put(actor_offset::elevation, std::uint32_t((short_hop_height[left] << 16) / 2));
                // The last frame of the short hop ends the whole movement.
                if (target == frame + jump_first_frame) put(actor_offset::motion_frame, 0);
                break;
            }
            case jump_arc_flat_fast:
                step = jump_third_step;
                break;
            default:
                break;
            }
        }
        // Spend this frame's step on whichever axes the facing names. The four
        // diagonals move on both.
        const auto moving = number(actor_offset::motion_frame);
        if (moving == 0) {
            put(actor_offset::motion_state, std::uint32_t(jump_state_settled));
        } else {
            switch (number(actor_offset::facing)) {
            case 4: add(actor_offset::tile_x_q16, step); [[fallthrough]];
            case 0: add(actor_offset::tile_y_q16, -step); break;
            case 5: add(actor_offset::tile_x_q16, -step); [[fallthrough]];
            case 1: add(actor_offset::tile_y_q16, step); break;
            case 6: add(actor_offset::tile_y_q16, -step); [[fallthrough]];
            case 2: add(actor_offset::tile_x_q16, -step); break;
            case 7: add(actor_offset::tile_y_q16, step); [[fallthrough]];
            case 3: add(actor_offset::tile_x_q16, step); break;
            default: break;
            }
        }
        if (moving > 0) put(actor_offset::motion_frame, std::uint32_t(moving + 1));
    }

    // --- tell the field loop whether the actor is between tiles -----------
    const auto settled = number(actor_offset::motion_state) == jump_state_settled;
    if (settled) memory.write(globals::field_transition_phase,
                              memory.read(globals::field_transition_phase) + 1);
    else memory.write(globals::field_transition_phase, 0xffffffffu);

    // --- offer a continuation ---------------------------------------------
    // A settled actor that is still marked as continuing looks at the input and
    // at the four tiles around it, and starts the next arc without ever handing
    // control back to field movement.
    if (settled && (field(actor_offset::flags) & jump_continues_bit) &&
        memory.read(field_event_block) == 0) {
        put(actor_offset::tile_x, std::uint32_t(cell.tile_x));
        put(actor_offset::tile_y, std::uint32_t(cell.tile_y));
        const auto gate_slot = memory.read(jump_active_gate) * jump_gate_slot_bytes;
        // Where the actor is already facing, in the same encoding as the four
        // continuation directions. Nothing asked for means "carry on".
        const auto facing_as_direction =
            memory.read(tables::direction_to_cardinal + field(actor_offset::facing) * 4);
        auto asked_for = facing_as_direction;
        auto direction = jump_continue_none;
        if (memory.read(globals::control_key_state) == 0) {
            for (unsigned which = 0; which < 4; ++which) {
                if (memory.read(jump_direction_keys[which].held) == 0 &&
                    memory.read(jump_direction_keys[which].pressed) == 0)
                    continue;
                direction = which;
                asked_for = which;
                break;
            }
        }
        if (asked_for != facing_as_direction && memory.read(globals::battle_command_result_phase) == 0) {
            // A new direction is a turn first, and the continuation is offered
            // again once the actor is facing the right way.
            put(actor_offset::motion_state, std::uint32_t(jump_state_turning));
            put(actor_offset::target_facing, asked_for);
        } else {
            // The two vertical directions step onto the neighbouring row; the
            // two sideways ones step through a gate the actor reserves.
            const auto step_onto = [&](std::int32_t neighbour, std::uint32_t no_exit_bit,
                                       unsigned axis, std::int32_t delta, unsigned facing) {
                if (attributes_of(neighbour) & jump_tile_block) return;
                if (cell.attributes & no_exit_bit) return;
                if (occupancy_of(neighbour) & jump_cell_occupied) return;
                if ((attributes_of(neighbour) & jump_tile_arc_lane) == jump_tile_arc_forbidden) return;
                cue(jump_cue_step);
                add(axis, delta);
                put(actor_offset::facing, facing);
                add(actor_offset::tile_y, delta < 0 ? -1 : 1);
                put(actor_offset::motion_state, std::uint32_t(jump_state_arc));
                put(actor_offset::motion_frame, std::uint32_t(jump_first_frame));
                put(actor_offset::frame_group, std::uint32_t(jump_arc_table_frames));
                set_submode(jump_arc_flat_fast);
            };
            // A gate is only entered when neither its own block bit nor the
            // occupancy of the tile behind it is set; the gate slot then
            // remembers the tile the actor left.
            const auto step_through_gate = [&](std::int32_t neighbour, std::int32_t delta,
                                               unsigned facing) {
                if (attributes_of(neighbour) & jump_tile_block) return;
                if (occupancy_of(neighbour) & jump_cell_occupied) return;
                memory.write(jump_gate_tile_x + gate_slot, std::uint32_t(cell.tile_x));
                memory.write(jump_gate_tile_y + gate_slot, field(actor_offset::tile_y));
                add(actor_offset::tile_x_q16, delta);
                add(actor_offset::tile_x, delta < 0 ? -1 : 1);
                put(actor_offset::motion_state, std::uint32_t(jump_state_side_step));
                put(actor_offset::facing, facing);
                put(actor_offset::motion_frame, std::uint32_t(jump_first_frame));
            };
            const auto row_above = cell.layer_base + cell.tile_x + (cell.tile_y - 1) * cell.map_width;
            const auto row_below = cell.layer_base + cell.tile_x + (cell.tile_y + 1) * cell.map_width;
            switch (direction) {
            case jump_continue_up:
                step_onto(row_above, jump_tile_no_exit_up, actor_offset::tile_y_q16,
                          -jump_nudge, jump_continue_up);
                break;
            case jump_continue_down:
                step_onto(row_below, jump_tile_no_exit_down, actor_offset::tile_y_q16,
                          jump_nudge, jump_continue_down);
                break;
            case jump_continue_left:
                step_through_gate(cell.index - 1, -jump_nudge, jump_continue_left);
                break;
            case jump_continue_right:
                step_through_gate(cell.index + 1, jump_nudge, jump_continue_right);
                break;
            default:
                break;
            }
        }
    }

    // --- publish the position and choose the frame ------------------------
    const auto pixels = tile_to_pixels(number(actor_offset::tile_x_q16),
                                       number(actor_offset::tile_y_q16));
    put(actor_offset::world_x, std::uint32_t(pixels.x));
    put(actor_offset::world_y, std::uint32_t(pixels.y));
    put(actor_offset::tile_x, std::uint32_t(number(actor_offset::tile_x_q16) / jump_fixed_one));
    put(actor_offset::tile_y, std::uint32_t(number(actor_offset::tile_y_q16) / jump_fixed_one));

    const auto drawn = number(actor_offset::motion_state);
    // The plain pose the actor holds whenever it is not visibly in the air.
    const auto draw_base_pose = [&] {
        put(actor_offset::sprite_selector, animated_sprite_selector);
        const auto row = memory.read(tables::direction_to_cardinal + field(actor_offset::facing) * 4);
        put(actor_offset::sprite_frame, row * jump_frames_per_direction);
        // An upward arc still lifts the sprite even though the pose is the
        // standing one; how high depends on how much of the arc is left.
        const auto left = number(actor_offset::frame_group) - number(actor_offset::motion_frame);
        if (submode() == jump_arc_small_up)
            put(actor_offset::elevation, std::uint32_t(high_arc[left] << 16));
        if (submode() == jump_arc_large_up)
            put(actor_offset::elevation, std::uint32_t(low_arc[left] << 17));
    };
    // The hop and the side-step run their own frame table and sink the sprite
    // by the matching amount.
    const auto draw_hop = [&] {
        put(actor_offset::sprite_selector, animated_sprite_selector);
        const auto frame = field(actor_offset::motion_frame);
        const auto row = memory.read(tables::direction_to_cardinal + field(actor_offset::facing) * 4);
        const auto index = frame + field(actor_offset::frame_group) * jump_hop_frame_stride;
        put(actor_offset::sprite_frame,
            memory.read(jump_hop_frame_table + index * 4) + row * jump_frames_per_direction);
        add(actor_offset::elevation, -signed32(memory.read(jump_hop_rise_table + frame * 4)));
    };
    // Standing still, but facing any of the eight directions rather than only
    // the four the walk frames cover.
    const auto draw_facing_pose = [&] {
        put(actor_offset::sprite_selector, animated_sprite_selector);
        const auto facing = number(actor_offset::facing);
        if (facing < 0 || facing > 7) return;
        const auto row = facing < 4
            ? memory.read(tables::direction_to_cardinal + std::uint32_t(facing) * 4) *
                  jump_frames_per_direction
            : memory.read(jump_diagonal_to_cardinal + std::uint32_t(facing) * 4) *
                      jump_frames_per_direction + jump_diagonal_frame;
        put(actor_offset::sprite_frame, row);
    };

    if (drawn == jump_state_normal) {
        draw_base_pose();
    } else if (drawn == jump_state_turning || drawn == jump_state_static_pose) {
        draw_facing_pose();
    } else if (drawn == jump_state_hop) {
        draw_hop();
    } else if (drawn == jump_state_side_step) {
        draw_hop();
        // Keep the gate the actor is passing through blocked for as long as it
        // is standing in it; field movement clears it again on the far side.
        const auto slot = memory.read(jump_active_gate) * jump_gate_slot_bytes;
        const auto gate = signed32(memory.read(jump_gate_tile_y + slot)) *
                              signed32(memory.read(globals::grid_row_stride)) +
                          signed32(memory.read(jump_gate_tile_x + slot)) + cell.layer_base;
        const auto at = globals::tile_attributes + std::uint32_t(gate) * 4;
        memory.write(at, memory.read(at) | jump_tile_block);
    } else if (drawn > jump_state_side_step && drawn <= jump_state_arc) {
        draw_base_pose();
    }
}

} // namespace fsb::core::actor_core
