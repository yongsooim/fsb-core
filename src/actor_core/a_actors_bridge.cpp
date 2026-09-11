#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actor_core/actor_slots.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/actor_core/actor_runtime.hpp"
#include "fsb_core/actor_core/actor_lifecycle.hpp"
#include "fsb_core/actor_core/ifc_stage.hpp"
#include "fsb_core/actor_core/gatewarp.hpp"
#include "fsb_core/actor_core/party_status.hpp"
#include "fsb_core/actor_core/session_state.hpp"
#include "fsb_core/actor_core/cim_blob.hpp"
#include "fsb_core/actor_core/item_menu.hpp"
#include "fsb_core/actor_core/motion_callbacks.hpp"
#include "fsb_core/actor_core/player_input.hpp"
#include "fsb_core/actor_core/field_interaction.hpp"
#include "fsb_core/actor_core/jump_motion.hpp"
#include "fsb_core/actor_core/map_transition.hpp"
#include "fsb_core/actor_core/save_slot.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/dialogue.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
// Temporary ABI boundary for the original callers that still run on the old
// execution model. Reading arguments and unwinding the return stack lives here;
// nothing in fsb::core::actor_core touches registers or the guest stack.
//
// Two original call edges are deliberately preserved rather than inlined:
//  * 42feb9, the selector-to-record boundary the integration session owns and
//    that hosts may intercept.
//  * 401a02, the shared error log an unresolved selector reports through.
// Word access also goes through this object so that callers which hand in
// frame locals keep addressing their own stack instead of guest memory.
bool RecoveredBattle::dispatch_a_actors(Address entry) {
    using namespace actor_core;
    const auto arg = [&](unsigned index) { return argument(index); };
    const ResolveActor resolve = [&](std::uint32_t selector) { return callback(0x42feb9, {selector}); };
    const MissingActorReport report = [&](std::uint32_t selector) {
        callback(0x401a02, {0x5aaf78, selector});
    };
    const GuestWords words{[&](Address at) { return read(at); },
                           [&](Address at, std::uint32_t value) { write(at, value); }};
    // The +0x148 callback is an indirect call in the original; keep it that way.
    const InvokeActorCallback invoke_callback = [&](Address target, Address object) {
        callback(target, {object});
    };
    switch (entry) {
    // --- selector and identity ------------------------------------------
    case 0x42feaa: result(is_script_selector(arg(0)) ? 1 : 0, 4); return true;
    case 0x457ec9: result(resolve_slot(memory_, arg(0)), 4); return true;
    case 0x42fecd: result(resolve_selector(memory_, arg(0), arg(1)), 8); return true;
    case 0x4302d2: result(resolve_or_report(arg(0), resolve, report), 4); return true;
    case 0x42fef2: result(attach_sequence_actor(memory_, arg(0), resolve), 4); return true;
    case 0x457e56: result(player_character(memory_)); return true;
    case 0x45f528: result(character_active(memory_, arg(0)) ? 1 : 0, 4); return true;
    case 0x4306cb: result(actor_exists(memory_, arg(0)) ? 1 : 0, 4); return true;

    // --- flags, visibility and pose -------------------------------------
    case 0x42ff47: result(visible(words, arg(0)) ? 1 : 0, 4); return true;
    case 0x42ff81: result(visible_by_selector(words, arg(0), resolve) ? 1 : 0, 4); return true;
    case 0x43001f: result(tile_state(words, arg(0)) ? 1 : 0, 4); return true;
    case 0x430005: {
        // The original forwards to42ff9b, which the integration session owns.
        if (const auto object = resolve(arg(0))) callback(0x42ff9b, {object, arg(1)});
        result(0, 8); return true;
    }
    case 0x4300e1: reset_pose_row(words, arg(0)); result(0, 4); return true;
    case 0x4301a8: { const auto value=arg(1); set_facing(words, arg(0), value); result(value, 8); return true; }
    case 0x4301b9: { const auto value=arg(1); set_target_facing(words, arg(0), value); result(value, 8); return true; }
    case 0x4301ca: { const auto value=arg(1); set_both_facings(words, arg(0), value); result(value, 8); return true; }
    case 0x43036f: turn_by_selector(words, arg(0), arg(1), resolve, report); result(0, 8); return true;
    case 0x42ff0c: {
        // 42ff0c is the actor half of the compact sequence release the dialogue
        // module already owns; its message queue is unused on this path.
        HsmQueue unused;
        Dialogue(memory_, unused).clear_sequence(arg(0));
        result(0, 4); return true;
    }

    // --- placement and derived coordinates -------------------------------
    case 0x43029b: refresh_tile_from_pixels(words, arg(0)); result(0, 4); return true;
    case 0x4301e1: place_at_pixels(words, arg(0), arg(1), arg(2)); result(0, 12); return true;
    case 0x45d774: place_record_on_tile(words, arg(0), arg(1), arg(2), arg(3)); result(0, 16); return true;
    case 0x45d82d: {
        const auto destination = arg(0), source = arg(1);
        const auto pixels = tile_to_pixels(signed32(read(source)), signed32(read(source + 4)));
        write(destination, std::uint32_t(pixels.x));
        write(destination + 4, std::uint32_t(pixels.y));
        result(0, 8); return true;
    }
    case 0x4303e5:
        place_at_tile(words, arg(0), arg(1), arg(2), arg(3), arg(4), resolve, report);
        result(0, 20); return true;
    case 0x430385: copy_placement(words, arg(0), arg(1), resolve, report); result(0, 8); return true;
    case 0x43023e: refresh_screen_anchor(memory_, words, arg(0)); result(0, 4); return true;
    case 0x430aa6: read_tile_position(words, arg(0), arg(1), arg(2), arg(3), resolve); result(0, 16); return true;
    case 0x430ae2: read_pixel_position(words, arg(0), arg(1), arg(2), arg(3), resolve); result(0, 16); return true;
    case 0x45dc7f: refresh_view_anchor_records(memory_); result(0); return true;

    // --- runtime object state and table resets ---------------------------
    case 0x45d889: write(arg(0), 0); result(arg(0), 4); return true; // memset(p,0,4) returns p.
    case 0x45db3d: set_visual_control_bits(words, arg(0)); result(arg(0), 4); return true;
    case 0x45db48: clear_visual_control_bits(words, arg(0)); result(arg(0), 4); return true;
    case 0x45db53: save_motion_block(words, arg(0)); result(0, 4); return true;
    case 0x45db6c: restore_motion_block(words, arg(0)); result(0, 4); return true;
    case 0x45d6ab: {
        const OpenItemMenu open_menu = [&](std::uint32_t menu) { callback(0x45e139, {menu}); };
        clear_pending_move_target(words, memory_, arg(0), arg(1), arg(2), open_menu);
        result(0, 12); return true;
    }
    case 0x45d9c8: clear_all_pending_moves(memory_); result(0); return true;
    case 0x45f47e: reset_party_slots(memory_); result(0); return true;
    case 0x460848: release_unfocused_party_visuals(memory_); result(0, 4); return true;
    case 0x461841: clear_tileset_slot_heads(memory_); result(0); return true;
    case 0x45da86: reset_battle_track_state(memory_); result(0); return true;
    case 0x458d78:
        release_gatewarp_objects(memory_, [&](Address object) { callback(0x45d91d, {object}); });
        result(0); return true;
    case 0x458da9:
        prepare_special_map_transition(memory_, [&](Address object) { callback(0x45d91d, {object}); });
        result(0); return true;

    // --- script-facing words ---------------------------------------------
    case 0x431700: result(event_flag_word(memory_, arg(0)), 4); return true;
    case 0x431710: set_event_flag_bit(memory_, arg(0), arg(1)); result(0, 8); return true;
    case 0x4604df: show_party_panel(memory_, arg(0)); result(0, 4); return true;
    case 0x460528: request_slot_map_transition(memory_, arg(0)); result(0, 4); return true;
    case 0x45e404: result(compare_three_way(signed32(arg(0)), signed32(arg(1))), 8); return true;
    case 0x431749: result(callback(0x436100, {worldmap_mode_for_selector(arg(0))}), 4); return true;
    case 0x43172b:
        // The retail build only logs that INTER_BATTLE_AreaSet is unimplemented.
        // Keep the log rather than inventing behaviour for the four operands.
        callback(0x401a02, {0x5ab1e8});
        result(0, 16); return true;
    case 0x431739: result(callback(0x435ffa, {arg(0), arg(1)}), 8); return true;
    case 0x4317e8: result(callback(0x4360be, {arg(0)}), 4); return true;
    case 0x45ef2d:
        // Tail jump into the shop body once the frame is ready;6 means "keep
        // the menu open" while it is not.
        result(read(0x803850) == 1 ? callback(0x45e5f9) : 6); return true;

    // --- party roster and the shared iterator ----------------------------
    case 0x43070b: result(live_party_count(memory_)); return true;
    case 0x43075b: rewind_party_iterator(memory_); result(0); return true;
    case 0x4307ac: result(next_live_party_member(memory_)); return true;
    case 0x430a7f: hide_all_party(memory_, words, resolve); result(0); return true;

    // --- object lifetime --------------------------------------------------
    case 0x43033d: {
        if (const auto object = resolve(arg(0))) {
            callback(0x42ff0c, {object + actor_offset::sequence_handle});
            callback(0x40275a, {object + actor_offset::dialogue_handle});
            callback(0x476765, {object});
        }
        result(0, 4); return true;
    }
    case 0x45d7cb: case 0x45d7fc:
        // The second argument only selects the assert text; the original never
        // reads it. Both entries store the same three words.
        write_vector3(words, arg(0), arg(2), arg(3), arg(4));
        result(0, 20); return true;
    case 0x45d91d: finalize_object(memory_, arg(0), invoke_callback); result(0, 4); return true;
    case 0x45d84b: reset_slot_range(memory_, arg(0), arg(1), invoke_callback); result(0, 8); return true;
    case 0x45d89c:
        result(spawn_callback_object(memory_, arg(0), invoke_callback,
                                     [&] { callback(0x401ad8, {0x5d0a9c}); }), 4);
        return true;
    case 0x45d9e7: copy_render_state(words, arg(0), arg(1)); result(0, 8); return true;
    case 0x45da95: init_global_motion_template(memory_, arg(0), arg(1), arg(2)); result(0, 12); return true;
    case 0x45db85:
        invoke_callback_substate(memory_, arg(0), arg(1), arg(2), invoke_callback);
        result(0, 12); return true;

    // --- active character and its snapshots -------------------------------
    case 0x45e139: open_item_use_menu(memory_, arg(0)); result(0, 4); return true;
    case 0x45f495: focus_next_party_slot(memory_, words, arg(0)); result(0, 4); return true;
    case 0x45f554: result(focus_party_character(memory_, words, arg(0)) ? 1 : 0, 4); return true;
    case 0x45ef3f: Actors(memory_).push_snapshot(arg(0)); result(0, 4); return true;
    case 0x45f0e9: result(std::uint32_t(Actors(memory_).spawn_party(arg(0))), 4); return true;
    case 0x45df01: Actors(memory_).refresh_stats(arg(0)); result(0, 4); return true;

    // --- the IFC stage table ----------------------------------------------
    case 0x430b1e: result(ifc_register(words, arg(0), arg(1), resolve), 8); return true;
    case 0x430cc3: result(ifc_live_sequence(memory_, arg(0)), 4); return true;
    case 0x430ce8:
        ifc_bind_sequence(memory_, arg(0), arg(1), [&](std::uint32_t slot, std::uint32_t handle) {
            callback(0x401a02, {0x5ab13c, slot, handle});
        });
        result(0, 8); return true;
    case 0x430ba9: {
        // Restore an actor that existed before the scene, or release one the
        // scene created. 42ff9b and430059 stay integration-owned boundaries.
        const auto record = ifc_stage_record(arg(0));
        if (const auto object = read(record + ifc_field::object)) {
            callback(0x42ff9b, {object, read(record + ifc_field::saved_visible)});
            callback(0x430059, {object, read(record + ifc_field::saved_tile_state)});
        } else {
            callback(0x43033d, {read(record + ifc_field::selector)});
        }
        result(0, 4); return true;
    }
    case 0x458dc4: Actors(memory_).apply_entry_direction(arg(0), arg(1)); result(0, 8); return true;

    // --- party membership swaps -------------------------------------------
    case 0x430434: {
        if (!is_script_selector(arg(0))) throw Fault(entry, "party replacement needs a script actor id");
        Actors(memory_).replace_with_party_actor(arg(0));
        result(0, 4); return true;
    }
    case 0x430527: {
        if (!is_script_selector(arg(0))) throw Fault(entry, "player swap needs a script actor id");
        result(Actors(memory_).swap_player(arg(0)), 4); return true;
    }

    // --- gatewarp sprites --------------------------------------------------
    case 0x4580f7: tick_attached_gatewarp(memory_, words, arg(0), GatewarpSprite::AttachedA); result(0, 4); return true;
    case 0x45826c: tick_attached_gatewarp(memory_, words, arg(0), GatewarpSprite::AttachedB); result(0, 4); return true;
    case 0x4583cd: tick_attached_gatewarp(memory_, words, arg(0), GatewarpSprite::AttachedC); result(0, 4); return true;
    case 0x458691: tick_screen_gatewarp(memory_, words, arg(0)); result(0, 4); return true;
    case 0x4588ee: tick_gatewarp_timeline(memory_, words, arg(0)); result(0, 4); return true;
    case 0x45852e:
        spawn_attached_gatewarp_group(memory_, arg(0), arg(1), invoke_callback);
        result(0, 8); return true;
    case 0x458b79:
        spawn_screen_gatewarp(memory_, invoke_callback, [&] {
            // Palette decode, palette copy and the cue are platform services.
            callback(0x4050fa, {0x5d095c, 0x803a50, 0});
            callback(0x404ed0, {0x804660, 0x803a50, 0xe0, 0x18});
            callback(0x435373, {0x154});
        });
        result(0); return true;

    // --- equipment status --------------------------------------------------
    case 0x45dd8b: result(collect_status_mask(memory_, arg(0), arg(1)), 8); return true;
    case 0x45de29: result(status_gate_allows(memory_, arg(0), arg(1)) & 0xff, 8); return true;

    // --- save slots and the CIM blob ---------------------------------------
    case 0x460deb: case 0x460e35: {
        // The slot files are a platform service; keep the original CRT calls.
        const ReadSlotHeader read_header = [&](std::uint32_t slot, Address destination,
                                               unsigned bytes) {
            const auto file = callback(0x498670, {memory_.read(save_slot_names + slot * 4), 0x4a6b48});
            if (!file) return false;
            callback(0x498690, {destination, 1, bytes, file});
            callback(0x4985c0, {file});
            return true;
        };
        if (entry == 0x460deb) { result(probe_save_slot(read_header, arg(0)) ? 1 : 0, 4); return true; }
        result(refresh_save_slot_presence(memory_, read_header)); return true;
    }
    case 0x460e58: case 0x46118e: {
        // The CRT stream, the heap and the two codecs are all services. The
        // scratch block stands in for the original's own stack frame, which is
        // where the few pass-through values it never keeps used to live.
        const auto slot = arg(0), caller_stack = r[4];
        r[4] -= 0x60;
        const auto scratch = r[4];
        for(unsigned at=0;at<0x60;at+=4)write(scratch+at,0);
        const auto slot_name = [&](std::uint32_t slot) {
            return memory_.read(save_slot_names + slot * 4);
        };
        SaveFile io;
        io.scratch = scratch;
        io.read_scratch = [&](unsigned offset){return read(scratch+offset);};
        io.write_scratch = [&](unsigned offset,std::uint32_t value){write(scratch+offset,value);};
        io.open = [&](std::uint32_t slot, bool for_writing) {
            return callback(0x498670, {slot_name(slot),
                                       for_writing ? save_mode_write : save_mode_read});
        };
        io.close = [&](Address file) { callback(0x4985c0, {file}); };
        io.write = [&](Address file, Address at, std::uint32_t count, std::uint32_t bytes) {
            callback(0x498870, {at, count, bytes, file});
        };
        io.read = [&](Address file, Address at, std::uint32_t count, std::uint32_t bytes) {
            callback(0x498690, {at, count, bytes, file});
        };
        io.rename = [&](std::uint32_t slot, Address to) {
            return callback(0x499340, {slot_name(slot), to});
        };
        // DeleteFileA, reached through its import slot as the original does.
        io.remove = [&](std::uint32_t slot) {
            callback(memory_.read(0x8593a4), {slot_name(slot)});
        };
        io.allocate = [&](std::uint32_t bytes) { return callback(0x40112a, {bytes}); };
        io.release = [&](Address buffer) { callback(0x4972b0, {buffer}); };

        SaveSlotHooks hooks;
        hooks.slot_readable = [&](std::uint32_t slot) { return callback(0x460deb, {slot}) != 0; };
        hooks.serialize_cim = [&](CimBlob& blob) {
            if (!callback(0x461549, {scratch + 0x40, scratch + 0x48})) return false;
            blob.payload = read(scratch + 0x40);
            blob.bytes = read(scratch + 0x48);
            return true;
        };
        hooks.release_cim = [&] { callback(0x4616e2); };
        hooks.deserialize_cim = [&](Address payload) { callback(0x461712, {payload}); };
        hooks.event_data = [&](std::uint32_t& bytes) {
            const auto payload = callback(0x431e95, {scratch + 0x50});
            bytes = read(scratch + 0x50);
            return payload;
        };
        hooks.store_event_data = [&](Address payload, std::uint32_t bytes) {
            callback(0x431f06, {payload, bytes});
        };
        hooks.reset_character_objects = [&] { callback(0x45f47e); };
        hooks.spawn_character_object = [&](std::uint32_t member) { callback(0x45f0e9, {member}); };
        hooks.prepare_map_transition = [&] { callback(0x458da9); };

        std::uint32_t answer = 0;
        try {
            answer = (entry == 0x460e58 ? write_save_slot(memory_, slot, io, hooks)
                                        : load_save_slot(memory_, slot, io, hooks)) ? 1 : 0;
        } catch (...) {
            r[4] = caller_stack;
            throw;
        }
        r[4] = caller_stack;
        result(answer, 4);
        return true;
    }
    case 0x4616e2:
        result(release_cim_blob(memory_, [&](Address buffer) { callback(0x4972b0, {buffer}); },
                                [&] { callback(0x401ad8, {0x5d25cc}); }));
        return true;

    // --- transition overlay ------------------------------------------------
    case 0x45dc18: {
        const auto x = arg(0), y = arg(1);
        const auto blit = overlay_sprite_blit(memory_, arg(2));
        // The blit reads its rectangle through a pointer, and the original
        // hands it a frame local; borrow the same guest stack for it.
        r[4] -= 16;
        const auto rect = r[4];
        write(rect, std::uint32_t(blit.left));
        write(rect + 4, std::uint32_t(blit.top));
        write(rect + 8, std::uint32_t(blit.right));
        write(rect + 12, std::uint32_t(blit.bottom));
        callback(0x405dd6, {read(0x6db16c), x, y, blit.surface, rect, blit.flags});
        r[4] += 16;
        result(0, 12); return true;
    }

    // --- item and shop menu rows -------------------------------------------
    case 0x45e167: result(build_shop_item_list(memory_), 4); return true;
    case 0x45e1a1: result(build_owned_item_list(memory_)); return true;
    case 0x45e1dc: result(sum_selected_item_values(memory_, arg(0)), 4); return true;

    // --- character snapshots and the passive tick ---------------------------
    case 0x45efb6:
        restore_character_snapshot(memory_, words, arg(0),
                                   [&](std::uint32_t member) { callback(0x45f0e9, {member}); });
        result(0, 4); return true;
    case 0x45f07e: {
        // Take the snapshot again, hand the still-live spawn object to the
        // map-specific hook when the current map wants it, then restore.
        const auto index = arg(0);
        callback(0x45ef3f, {memory_.read(character_snapshot_slot)});
        const auto spawn_slot = signed32(memory_.read(0x5d0a40));
        const auto map = signed32(memory_.read(globals::current_map_id));
        if (spawn_slot > -1 && map > 0x1c8 && map < 0x1d4) {
            const auto slot = memory_.read(character_snapshot_slot);
            const auto place = character_snapshot_place + slot * character_snapshot_place_bytes;
            callback(0x4948f7, {globals::actor_objects + std::uint32_t(spawn_slot) * layout::actor_size,
                                slot, read(place), read(place + 4), read(place + 8), read(place + 0xc)});
        }
        callback(0x45efb6, {index});
        result(0, 4); return true;
    }
    case 0x45c110:
        tick_passive_actor(words, arg(0), [&](Address object) { callback(0x45c55c, {object}); });
        result(0, 4); return true;

    // --- IFC stage restore, spawn and markup --------------------------------
    case 0x430be4: {
        // Put a staged actor back. A slot that had no actor before the scene is
        // released; otherwise the actor is either replaced where it stood or
        // walked back to it, depending on whether it was visible.
        const auto record = ifc_stage_record(arg(0));
        const auto object = read(record + ifc_field::object);
        if (!object) { callback(0x43033d, {read(record + ifc_field::selector)}); result(0, 4); return true; }
        const auto was_visible = read(record + ifc_field::saved_visible);
        if (was_visible > 1) throw Fault(entry, "IFC stage restore mode must be zero or one");
        callback(0x42ff9b, {object, was_visible});
        const auto selector = read(record + ifc_field::selector);
        const auto tile_x = read(record + ifc_field::saved_tile_x);
        const auto tile_y = read(record + ifc_field::saved_tile_y);
        const auto facing = read(record + ifc_field::saved_facing);
        const auto tile_state = read(record + ifc_field::saved_tile_state);
        if (!was_visible) {
            callback(0x430059, {object, tile_state});
            callback(0x4303e5, {selector, read(record + ifc_field::saved_layer), tile_x, tile_y, facing});
        } else {
            const auto handle = callback(0x430e02, {selector, tile_x, tile_y, facing, 0, 0, 0, 1});
            write(record + ifc_field::sequence_handle, handle);
            write(callback(0x4026c1, {handle}) + 0x1a0, tile_state);
        }
        result(0, 4); return true;
    }
    case 0x430d8c:
        result(Actors(memory_).start_walk(arg(0), arg(1), arg(2), arg(3), arg(4)), 20); return true;
    case 0x45802f:
        result(Actors(memory_).spawn_effect(signed32(arg(0)), signed32(arg(1)), signed32(arg(2)),
                                            arg(3), arg(4)), 20);
        return true;
    case 0x43156e: {
        HsmQueue unused; // 43156e never enqueues; the channel is only stored.
        result(Dialogue(memory_, unused).spawn_markup(arg(0), arg(1), arg(2)), 12); return true;
    }

    // --- NPC wander -------------------------------------------------------
    case 0x45b8a0: {
        // tick_npc reports the cue it wants rather than playing it, so the
        // audio call stays the original edge here.
        const auto step = Actors(memory_).tick_npc(arg(0));
        if (step.sound) callback(0x435373, {*step.sound});
        result(0, 4); return true;
    }

    // --- per-frame motion callbacks ----------------------------------------
    case 0x45c063: tick_entrance_dive(words, memory_, arg(0)); result(0, 4); return true;
    case 0x45d208:
        tick_oscillation(words, arg(0),
                         [&](std::uint32_t angle) { return fixed_sin(memory_, angle); },
                         [&](std::uint32_t angle) { return fixed_cos(memory_, angle); });
        result(0, 4); return true;
    case 0x45befc:
        tick_scripted_path(words, memory_, arg(0),
                           [&](Address object) { return callback(0x45c55c, {object}); },
                           [&](Address object) { callback(0x45cc1b, {object}); });
        result(0, 4); return true;

    case 0x45c3a9: tick_grid_cursor(memory_, words, arg(0)); result(0, 4); return true;
    case 0x45abf9: {
        // 435373/4353cb are the audio service the field actor asks for its
        // step, jump and landing cues; the audio session owns both.
        const JumpMotionSounds sound{
            [&](std::uint32_t which) { callback(0x435373, {which}); },
            [&](std::uint32_t which) { callback(0x4353cb, {which}); }};
        tick_jump_motion(memory_, words, arg(0), sound);
        result(0, 4); return true;
    }
    case 0x45dcc8: {
        // Measuring, letterboxing, copying and releasing the border are all
        // platform services; the two rectangles travel through the guest stack
        // the way the original's frame locals do.
        SurfaceCapture surface;
        surface.measure = [&](Address source) {
            r[4] -= 8;
            const auto extent = r[4];
            callback(0x405d43, {source, extent, extent + 4});
            const std::pair<std::int32_t, std::int32_t> size{signed32(read(extent)),
                                                             signed32(read(extent + 4))};
            r[4] += 8;
            return size;
        };
        surface.adjust = [&](std::int32_t& left, std::int32_t& top,
                             std::int32_t& right, std::int32_t& bottom) {
            r[4] -= 16;
            const auto rect = r[4];
            write(rect, std::uint32_t(left)); write(rect + 4, std::uint32_t(top));
            write(rect + 8, std::uint32_t(right)); write(rect + 12, std::uint32_t(bottom));
            callback(0x401c34, {rect});
            left = signed32(read(rect)); top = signed32(read(rect + 4));
            right = signed32(read(rect + 8)); bottom = signed32(read(rect + 12));
            r[4] += 16;
        };
        surface.copy = [&](Address destination, std::int32_t left, std::int32_t top,
                           std::int32_t right, std::int32_t bottom, Address source) {
            r[4] -= 32;
            const auto to = r[4], from = r[4] + 16;
            // The destination is always the whole surface the measure reported.
            const std::int32_t whole[] = {0, 0, right - left, bottom - top};
            for (unsigned i = 0; i < 4; ++i) write(to + i * 4, std::uint32_t(whole[i]));
            const std::int32_t taken[] = {left, top, right, bottom};
            for (unsigned i = 0; i < 4; ++i) write(from + i * 4, std::uint32_t(taken[i]));
            callback(0x405ed1, {destination, to, source, from, 0, 0});
            r[4] += 32;
        };
        surface.release_border = [&] { callback(0x406996, {0}); };
        result(capture_transition_surface(memory_, surface));
        return true;
    }

    // --- the CIMsave side blob ---------------------------------------------
    case 0x461549: case 0x461712: {
        // One debug line per refusal, using the original's own strings.
        const CimReport report = [&](CimFailure reason) {
            static const std::pair<CimFailure, Address> messages[] = {
                {CimFailure::AlreadyHeld, 0x5d2590}, {CimFailure::AllocationFailed, 0x5d257c},
                {CimFailure::NullBlob, 0x5d2678},    {CimFailure::TotalSize, 0x5d2660},
                {CimFailure::Magic, 0x5d2648},       {CimFailure::ObjectEffectSize, 0x5d2630},
                {CimFailure::WorldRecordSize, 0x5d2618}, {CimFailure::EventFlagSize, 0x5d2600},
            };
            for (const auto& [which, message] : messages)
                if (which == reason) { callback(0x401ad8, {message}); return; }
        };
        if (entry == 0x461712) {
            result(deserialize_cim_blob(memory_, arg(0), report) ? 1 : 0, 4);
            return true;
        }
        CimBlob blob;
        const auto built = serialize_cim_blob(
            memory_, blob,
            [&](std::uint32_t bytes) { return callback(0x4974a0, {bytes}); },
            [&] { return callback(0x8594a0); }, report);
        if (built) { write(arg(0), blob.payload); write(arg(1), blob.bytes); }
        result(built ? 1 : 0, 8); return true;
    }

    // --- item and shop menu drawing -----------------------------------------
    case 0x45e231: case 0x45e421: {
        MenuPainter painter;
        painter.sheet_frame = [&](std::int32_t x, std::int32_t y, Address sheet,
                                  std::uint32_t frame) {
            callback(0x40587f, {std::uint32_t(x), std::uint32_t(y), sheet, frame, 0});
        };
        painter.text = [&](std::int32_t x, std::int32_t y, std::uint32_t colour, Address format,
                           std::uint32_t argument, bool formatted) {
            std::vector<std::uint32_t> arguments{std::uint32_t(x), std::uint32_t(y), colour,
                                                 0xffffffffu, format};
            if (formatted) arguments.push_back(argument);
            callback(0x4067ec, arguments);
        };
        painter.overlay = [&](std::int32_t x, std::int32_t y, std::uint32_t overlay) {
            callback(0x45dc18, {std::uint32_t(x), std::uint32_t(y), overlay});
        };
        if (entry == 0x45e231) {
            draw_item_menu_rows(memory_, (arg(0) & 0xff) != 0, (arg(1) & 0xff) != 0, painter);
            result(0, 8); return true;
        }
        draw_equip_stat_arrows(memory_, signed32(arg(0)), signed32(arg(1)), arg(2), arg(3), painter);
        result(0, 16); return true;
    }

    // --- the battle command callback ----------------------------------------
    case 0x45c14f: {
        PlayerCommandHooks hooks;
        hooks.validate_placement = [&](std::uint32_t direction) { callback(0x451601, {direction}); };
        hooks.start_handler = [&](std::uint32_t facing) {
            callback(0x45149b, {read(0x77e588), facing});
        };
        hooks.select_followup = [&] { return (callback(0x44e7e5) & 0xff) != 0; };
        hooks.raise_cancel_effect = [&] {
            write(0x77ec4c, 1, 1);
            callback(0x4544cf, {read(globals::camera_focus_actor_index), read(0x7757e0), 5, 1});
        };
        hooks.resolve_frame = [&](Address record) { callback(0x45cc1b, {record}); };
        tick_player_command(memory_, words, arg(0), hooks);
        result(0, 4); return true;
    }

    // --- the field confirm probe --------------------------------------------
    case 0x45d395: {
        FieldInteractionHooks hooks;
        // Both callees take out parameters through the caller's frame, so the
        // guest stack lends them the same way the original's locals do.
        hooks.start_dialogue = [&](std::uint32_t target, std::uint32_t message,
                                   std::int32_t& actor_type) {
            r[4] -= 4;
            const auto slot = r[4];
            write(slot, std::uint32_t(actor_type));
            const auto answer = callback(0x413b74, {target, message, slot, field_event_block, 0});
            actor_type = signed32(read(slot));
            r[4] += 4;
            return answer;
        };
        hooks.collect_sparkle = [&](std::uint32_t object, std::int32_t& item,
                                    std::int32_t& quantity) {
            r[4] -= 8;
            const auto slot = r[4];
            write(slot, std::uint32_t(item));
            write(slot + 4, std::uint32_t(quantity));
            callback(0x455099, {object, slot, slot + 4});
            item = signed32(read(slot));
            quantity = signed32(read(slot + 4));
            r[4] += 8;
        };
        hooks.play_pickup_cue = [&] { callback(0x434584, {1, 0}); };
        result(probe_adjacent_trigger(memory_, words, arg(0), hooks), 4);
        return true;
    }

    // --- the map transition state machine ------------------------------------
    case 0x46018b: {
        MapTransitionHooks hooks;
        hooks.close_dialogue = [&] { callback(0x413afb); };
        hooks.capture_palette = [&](Address buffer, unsigned first, unsigned count) {
            callback(0x404c56, {buffer, first, count});
        };
        hooks.restore_palette = [&](Address buffer, unsigned first, unsigned count) {
            callback(0x404bb0, {buffer, first, count});
        };
        hooks.copy_palette = [&](Address destination, Address source, unsigned first,
                                 unsigned count) {
            callback(0x404ed0, {destination, source, first, count});
        };
        hooks.fade_music = [&](unsigned steps, unsigned volume) {
            callback(0x433794, {steps, volume});
        };
        hooks.play_music = [&](std::uint32_t track) { callback(0x4335c0, {track, 1, 1}); };
        hooks.music_fading = [&] { return callback(0x433788) != 0; };
        hooks.run_screen_fade = [&](std::int32_t mode_, unsigned direction, unsigned milliseconds,
                                    Address context) {
            return callback(0x4321ab, {std::uint32_t(mode_), direction, milliseconds, context}) != 0;
        };
        hooks.show_map_name = [&](Address name) { callback(0x4325b4, {name}); };
        hooks.invalidate_sheet_cache = [&] { callback(0x40bb53); };
        hooks.release_sparkles = [&] { callback(0x454f71); };
        hooks.load_save_slot = [&](std::uint32_t slot) { callback(0x46118e, {slot}); };
        hooks.route_event = [&](std::int32_t source, std::int32_t target) {
            return callback(0x412181, {std::uint32_t(source), std::uint32_t(target)});
        };
        hooks.worldmap_route_exists = [&](std::int32_t from, std::int32_t to) {
            return callback(0x436077, {std::uint32_t(from), std::uint32_t(to)}) != 0;
        };
        hooks.release_old_map = [&] {
            callback(0x457432);
            callback(0x457445);
            callback(0x45d84b, {memory_.read(globals::party_count), map_character_slots});
            callback(0x45d9c8);
            callback(0x457b8f);
        };
        hooks.load_map_resources = [&](std::uint32_t which) {
            callback(0x457458, {which});
            callback(0x448d72, {0});
        };
        hooks.spawn_collected_actors = [&] { callback(0x457e1c); };
        hooks.spawn_sparkles = [&](std::uint32_t which) { callback(0x454f9d, {which}); };
        hooks.rebuild_after_load = [&](std::uint32_t which) {
            callback(0x49711f, {which});
            // The map's own init callback is an indirect call through its record.
            if (const auto init = read(map_record_callback + which * map_record_bytes))
                callback(init);
            callback(0x457191);
            callback(0x454180);
            callback(0x454220);
        };
        hooks.apply_viewport = [&](bool resume) {
            if (resume) callback(0x4320f6, {1}); else callback(0x43208d, {0});
        };
        hooks.restore_actor_position = [&] { callback(0x457700); };
        result(advance_map_transition(memory_, arg(0), signed32(arg(1)), arg(2), hooks), 12);
        return true;
    }

    default: return false;
    }
}
} // namespace fsb::core
