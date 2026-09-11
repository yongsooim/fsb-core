#pragma once

// Symbols for the inspected FLYINGSB.EXE. These are guest addresses/offsets,
// never host pointers. Values and original literal types are preserved.
// Source roles and replacement audit: reports/symbolization-catalog.json.
// Ambiguous aliases are left numeric until their caller context is known.
namespace fsb::core {

namespace globals {
inline constexpr auto overlay_sprite_surfaces = 0x804d0c; //454cb5: surface field of each68-byte overlay sheet.
inline constexpr auto crt_math_errno = 0x8577fc; //Original CRT math error tail4a11b0.
inline constexpr auto text_overlay_count = 0x74b6c4; //406576/40664a producers,407e14 consumer.
//407ffd starts compact-object draining before posting the final WM_CLOSE.
inline constexpr auto shutdown_drain_requested = 0x6e0e0c;
inline constexpr auto shutdown_idle_frames = 0x6d6b2c;
// 431739/435ffa and 4604df: city navigation bits and saved party-panel mask.
inline constexpr auto worldmap_city_flags = 0x5aff98;
inline constexpr auto party_panel_visible_mask = 0x803a40;
inline constexpr auto sparkle_definitions = 0x5cd438;
inline constexpr auto sparkle_active_definitions = 0x7ab2d8;
inline constexpr auto sparkle_active_objects = 0x7ab2dc;
inline constexpr auto collected_sparkle_bits = 0x7ab4d8;
// Original input40758d, scheduler407ded/407ffd, actor/map/render and dialogue routines; verified read/write roles in current core.
inline constexpr auto frame_time_ms = 0x6da2d8;
inline constexpr auto previous_frame_time_ms = 0x6da2cc;
inline constexpr auto frame_delta_ms = 0x6d9e74;
inline constexpr auto presented_frame_counter = 0x6da2d4;
inline constexpr auto input_message = 0x6da2dc;
inline constexpr auto input_key = 0x6d66b0;
inline constexpr auto input_flags = 0x6d6688;
inline constexpr auto shift_key_state = 0x6d66dc;
inline constexpr auto control_key_state = 0x74b474;
inline constexpr auto alt_context_state = 0x6e12b8;
inline constexpr auto no_modifiers_held = 0x4a53c0;
inline constexpr auto last_key_held = 0x6da2c8;
inline constexpr auto scan_key_states = 0x6da568;
inline constexpr auto virtual_key_states = 0x6dad68;
inline constexpr auto last_scan_code = 0x6d9d24;
inline constexpr auto last_virtual_key = 0x6d9d44;
inline constexpr auto enter_held = 0x6da5d8;
inline constexpr auto space_held = 0x6da64c;
inline constexpr auto x_held = 0x6da61c;
inline constexpr auto numpad5_held = 0x6da698;
inline constexpr auto numpad1_held = 0x6da6a4;
inline constexpr auto up_held = 0x6da888;
inline constexpr auto down_held = 0x6da8a8;
inline constexpr auto left_held = 0x6da894;
inline constexpr auto right_held = 0x6da89c;
inline constexpr auto left_mouse_held = 0x74b4a0;
inline constexpr auto right_mouse_held = 0x74b4a1;
inline constexpr auto key_pressed_this_frame = 0x6e1444;
inline constexpr auto key_released_this_frame = 0x6d4b44;
inline constexpr auto mouse_message_this_frame = 0x74b6b8;
inline constexpr auto second_boundary_crossed = 0x6d6b04;
inline constexpr auto runtime_mode_flags = 0x6d9eb8;
inline constexpr auto phase_interval_ms = 0x4a5054;
inline constexpr auto fast_animation_gate = 0x6d4b40;
inline constexpr auto phase_clock_base_ms = 0x6db990;
inline constexpr auto job_clock_base_ms = 0x6d4b48;
inline constexpr auto frame_job_count = 0x4a5040;
inline constexpr auto next_object_group = 0x6d9e7c;
inline constexpr auto compact_free_head = 0x6e1454;
inline constexpr auto compact_active_count = 0x6d9e60;
inline constexpr auto compact_visible_count = 0x6e12ac;
inline constexpr auto compact_runtime_references = 0x6d9ea4;
inline constexpr auto exclusive_active_count = 0x6d9e64;
inline constexpr auto object_group_sentinels = 0x6d4b50;
inline constexpr auto compact_objects = 0x6e1470;
inline constexpr auto group0_append_link = 0x6d4cf8;
inline constexpr auto group1_append_link = 0x6d5048;
inline constexpr auto group5_append_link = 0x6d5d88;
inline constexpr auto current_event_id = 0x57fd1c;
inline constexpr auto previous_event_id = 0x57fd20;
inline constexpr auto pending_event_id = 0x57fd24;
inline constexpr auto resume_event_id = 0x57fd28;
inline constexpr auto event_activation_counts = 0x768688;
inline constexpr auto game_mode = 0x80465c;
inline constexpr auto party_actor_ids = 0x5d2258;
inline constexpr auto active_party_index = 0x803a1c;
inline constexpr auto party_count = 0x803a20;
inline constexpr auto character_index = 0x803a24;
inline constexpr auto camera_focus_actor_index = 0x787478;
inline constexpr auto actor_objects = 0x8073d8;
inline constexpr auto actor_active_count = 0x8073d4;
inline constexpr auto collected_actor_count = 0x8577d8;
inline constexpr auto party_selection_mask = 0x7693a8;
inline constexpr auto party_iterator = 0x7693ac;
inline constexpr auto debug_osd_handle = 0x769430;
inline constexpr auto field_transition_phase = 0x776478;
inline constexpr auto framebuffer_width = 0x4a504c;
inline constexpr auto framebuffer_height = 0x4a5050;
inline constexpr auto framebuffer_rect = 0x4a50a8;
inline constexpr auto primary_surface = 0x6e144c;
inline constexpr auto back_surface = 0x6d9d00;
inline constexpr auto render_target_surface = 0x6db16c;
inline constexpr auto presentation_page_count = 0x6e146c;
inline constexpr auto video_mode_flags = 0x6d9ebc;
inline constexpr auto surface_state_flags = 0x6d9ec0;
inline constexpr auto focus_palette_state = 0x6db98c;
inline constexpr auto inactive_window = 0x6db588;
inline constexpr auto focus_palette_backup = 0x6db58c;
inline constexpr auto clip_left = 0x6da548;
inline constexpr auto clip_top = 0x6da54c;
inline constexpr auto clip_right = 0x6da550;
inline constexpr auto clip_bottom = 0x6da554;
inline constexpr auto requested_viewport_rect = 0x6d6690;
inline constexpr auto viewport_left = 0x6d9d30;
inline constexpr auto viewport_top = 0x6d9d34;
inline constexpr auto viewport_right = 0x6d9d38;
inline constexpr auto viewport_bottom = 0x6d9d3c;
inline constexpr auto viewport_width = 0x6e12b0;
inline constexpr auto viewport_height = 0x6e1440;
inline constexpr auto viewport_origin_x = 0x6e1468;
inline constexpr auto viewport_origin_y = 0x74b470;
inline constexpr auto viewport_far_x = 0x74b4ac;
inline constexpr auto viewport_far_y = 0x74b6c0;
inline constexpr auto viewport_center_x = 0x6d9e6c;
inline constexpr auto viewport_center_y = 0x6d9e78;
inline constexpr auto camera_x = 0x7873c0;
inline constexpr auto camera_y = 0x7873c4;
inline constexpr auto camera_focus_x_q16 = 0x857634;
inline constexpr auto camera_focus_y_q16 = 0x857638;
inline constexpr auto camera_scroll_min_x = 0x7873b4;
inline constexpr auto camera_scroll_min_y = 0x7873b0;
inline constexpr auto camera_scroll_max_x = 0x7873a8;
inline constexpr auto camera_scroll_max_y = 0x7873ac;
inline constexpr auto grid_row_stride = 0x7760c4;
inline constexpr auto grid_height = 0x7760c8;
inline constexpr auto move_cost_grid = 0x77a570;
inline constexpr auto path_grid_id = 0x77e598;
inline constexpr auto path_target_x = 0x77e58c;
inline constexpr auto path_target_y = 0x77e590;
inline constexpr auto move_cost_grid_valid = 0x77ec48;
inline constexpr auto path_frontier_counts = 0x775958;
inline constexpr auto path_frontier_x = 0x77e5b8;
inline constexpr auto path_frontier_y = 0x77e5bc;
inline constexpr auto tile_attributes = 0x7cbca0;
inline constexpr auto tile_occupancy = 0x7abca0;
inline constexpr auto map_layers = 0x7e0d08;
inline constexpr auto map_raw_attributes = 0x7e4d30;
inline constexpr auto map_layer_width = 0x7e0d20;
inline constexpr auto map_layer_height = 0x7e0d24;
inline constexpr auto map_tile_codes = 0x7e0d30;
inline constexpr auto map_scroll_descriptors = 0x7e0ca0;
inline constexpr auto map_extent_x_q16 = 0x7e0ca8;
inline constexpr auto tile_animation_lookup = 0x7b3ca0;
inline constexpr auto tile_animations = 0x7d3ca0;
inline constexpr auto tile_animation_count = 0x800db8;
inline constexpr auto background_layer_count = 0x800dbc;
inline constexpr auto active_map_layer_count = 0x800dc0;
inline constexpr auto parallax_layer_index = 0x78745c;
inline constexpr auto current_map_id = 0x5d229c;
inline constexpr auto camera_bounds_enabled = 0x5cfc30;
inline constexpr auto overlay_effects = 0x800dc8;
inline constexpr auto overlay_effects_end = 0x8019c8;
inline constexpr auto current_overlay_effect = 0x806618;
inline constexpr auto sparkle_count = 0x7ab558;
inline constexpr auto event_flag_bits = 0x806b28;
inline constexpr auto draw_queue_count = 0x787480;
inline constexpr auto draw_pass_counts = 0x787488;
inline constexpr auto draw_queue = 0x787498;
inline constexpr auto draw_records = 0x788118;
inline constexpr auto foreground_occlusion = 0x7764e0;
inline constexpr auto battle_grid_flags = 0x7764e0; // Low byte: move/attack mask; bit8: per-pass foreground occlusion.
inline constexpr auto battle_cursor_effect_flags = 0x77ec4c;
inline constexpr auto battle_command_substate = 0x775cac;
inline constexpr auto battle_command_result_phase = 0x77ecdc;
inline constexpr auto battle_range_palette = 0x5d2280;
inline constexpr auto foreground_occlusion_end = 0x77a4e0;
inline constexpr auto palette_target = 0x804660;
inline constexpr auto overlay_palette_cache = 0x803e50;
inline constexpr auto decoded_image_palette = 0x803a50;
inline constexpr auto palette_work = 0x6d6288;
inline constexpr auto palette_fade_start = 0x6db9f0;
inline constexpr auto palette_fade_target = 0x6d9ec8;
inline constexpr auto palette_fade_step = 0x4a57c8;
inline constexpr auto palette_fade_duration = 0x6d6b00;
inline constexpr auto palette_fade_clamp = 0x6dc5fc;
inline constexpr auto frame_idle_callback = 0x6d9e98;
inline constexpr auto rect_effect_busy = 0x6d9e70;
inline constexpr auto transition_wait_phase = 0x768a88;
inline constexpr auto rect_effect_timer = 0x6da4e8;
inline constexpr auto rect_effect_remaining = 0x6da4ec;
inline constexpr auto rect_effect_counts_up = 0x6da500;
inline constexpr auto rect_effect_cached_mode = 0x6da504;
inline constexpr auto rect_effect_uses_target = 0x6da508;
inline constexpr auto rect_effect_target_rect = 0x6da50c;
inline constexpr auto rect_effect_source_rect = 0x6da51c;
inline constexpr auto rect_effect_cell_width = 0x6da52c;
inline constexpr auto rect_effect_row_height = 0x6da530;
inline constexpr auto rect_effect_anchor_pointer = 0x6da564;
inline constexpr auto rect_effect_anchor_x = 0x74b490;
inline constexpr auto rect_effect_anchor_y = 0x74b494;
inline constexpr auto rect_effect_max_radius = 0x6e0e00;
inline constexpr auto rect_effect_target_surface = 0x6d6b08;
inline constexpr auto rect_effect_sub_surface = 0x6d6284;
inline constexpr auto disable_zoom = 0x6d66a4;
inline constexpr auto live_dialogue_count = 0x768684;
inline constexpr auto dialogue_abort_requested = 0x7683d8;
inline constexpr auto dialogue_input_corner_flags = 0x7683d0;
inline constexpr auto dialogue_turn_counter = 0x7683dc;
inline constexpr auto occupied_dialogue_count = 0x768460;
inline constexpr auto occupied_dialogue_rects = 0x768480;
inline constexpr auto event_text_wait_scale = 0x768464;
inline constexpr auto free_text_wait_scale = 0x76847c;
inline constexpr auto event_dialogue_auto_delay = 0x768478;
inline constexpr auto dialogue_timing_percentages = 0x772fe0;
inline constexpr auto dialogue_view_left = 0x768468;
inline constexpr auto dialogue_view_top = 0x76846c;
inline constexpr auto dialogue_view_right = 0x768470;
inline constexpr auto dialogue_view_bottom = 0x768474;
inline constexpr auto animation_frame_bias = 0x769424;
inline constexpr auto dialogue_arrow_surface = 0x769428;
inline constexpr auto dialogue_skin_surface = 0x76942c;
inline constexpr auto character_family_first_frame = 0x7664e8;
inline constexpr auto character_family_frame_counts = 0x766a28;
inline constexpr auto reaction_family_first_frame = 0x766810;
inline constexpr auto deferred_sprite_count = 0x766f6c;
inline constexpr auto deferred_sprite_entries = 0x767370;
inline constexpr auto extended_palette_family = 0x766f74;
inline constexpr auto extended_palette_frame = 0x757dbc;
inline constexpr auto extended_palette_loaded = 0x74b6d8;
inline constexpr auto extended_palette_dirty = 0x74b6f4;
}

namespace tables {
inline constexpr auto overlay_sprite_frames = 0x5d1da0; //454cb5: sheet,x,y,width,height in24-byte records.
inline constexpr auto text_overlays = 0x6dc600; //512 original36-byte transient drawing records.
// Original PE tables and field aliases consumed by matching loader/render/input routines; aliases remain distinct.
inline constexpr auto event_definitions = 0x6d0144;
inline constexpr auto party_member_ids = 0x5aaf20;
inline constexpr auto actor_alias_ids = 0x5b3560;
inline constexpr auto actor_slot_ids = 0x5b3568;
inline constexpr auto actor_callback_kinds = 0x5b3570;
inline constexpr auto actor_dialogue_styles = 0x5b359c;
inline constexpr auto actor_object_pointers = 0x5b35a0;
inline constexpr auto map_patches = 0x604040;
inline constexpr auto tile_attribute_templates = 0x5cfc38;
inline constexpr auto opposite_directions = 0x5bf498;
inline constexpr auto direction_to_cardinal = 0x5d0588;
inline constexpr auto sine_quarter_wave = 0x4a77b8;
inline constexpr auto sine_negative_quarter_alias = 0x4a57b8;
inline constexpr auto motion_negative_reciprocal = 0x4a2618;
inline constexpr auto rect_progress_reciprocal = 0x4a2470;
inline constexpr auto black_palette = 0x4a2000;
inline constexpr auto logfonts = 0x4a57d0;
inline constexpr auto worldmap_logfont = 0x5aff40; //435877/CreateFontIndirectA:16px GulimChe,weight900.
inline constexpr auto glyph_outline_mask = 0x4a2580;
inline constexpr auto dialogue_actor_exclusion_rect = 0x4a2518;
inline constexpr auto dialogue_tail_frames = 0x4a2528;
inline constexpr auto dialogue_atlas_frames = 0x5ab278;
inline constexpr auto dialogue_width_profiles = 0x57f968;
inline constexpr auto dialogue_open_duration = 0x57f948;
inline constexpr auto dialogue_tail_duration = 0x57f94c;
inline constexpr auto dialogue_close_duration = 0x57f950;
inline constexpr auto character_frames = 0x514b58;
inline constexpr auto character_frame_family = 0x514b78;
inline constexpr auto character_frame_surface = 0x514b94;
inline constexpr auto character_sheets = 0x4a90a0;
inline constexpr auto character_sheet_surface = 0x4a94c8;
inline constexpr auto indexed_sheets = 0x500638;
inline constexpr auto indexed_sheet_surface = 0x500678;
inline constexpr auto bgm_resources = 0x5abbc0;
inline constexpr auto sound_cue_resources = 0x5ad388;
}

namespace routines {
inline constexpr auto finish_runtime_frame = 0x407ffd;
// Actual4874a7..4874dc: remember current map at init, release on map change.
inline constexpr auto map_marker_tick = 0x4874a7;
// Original function entry points. Naming an address does not claim every branch of that routine is ported.
inline constexpr auto event_vm_tick = 0x41a042;
inline constexpr auto tick_effect_scripts = 0x4478ba; // Global actor scan, once per game frame.
inline constexpr auto dialogue_tick = 0x40dd27;
inline constexpr auto reset_dialogue_rectangles = 0x40c149;
inline constexpr auto camera_follow_tick = 0x430fd0;
inline constexpr auto tile_tween_tick = 0x431273;
inline constexpr auto raw_tween_tick = 0x4314c1;
inline constexpr auto audio_fade_tick = 0x42c69e;
inline constexpr auto viewport_tween_tick = 0x406de8;
inline constexpr auto viewport_border_tick = 0x406be1;
inline constexpr auto blit_border_tick = 0x4069e9;
inline constexpr auto point_zoom_tick = 0x401e55;
inline constexpr auto radial_reveal_tick = 0x402569;
inline constexpr auto palette_gradient = 0x404b0c;
inline constexpr auto begin_palette_fade = 0x40536b;
inline constexpr auto upload_palette = 0x404bb0;
inline constexpr auto upload_palette_copy = 0x404c1c;
inline constexpr auto field_actor_movement = 0x458ed7;
inline constexpr auto empty_map_callback = 0x496bee;
inline constexpr auto palace_trigger_setup = 0x4967c6;
inline constexpr auto map_trigger_tick = 0x486dfb;
inline constexpr auto flood_move_cost = 0x4490c7;
inline constexpr auto trace_move_path = 0x448d90;
inline constexpr auto spawn_path_followup = 0x430e02;
inline constexpr auto start_walk_child = 0x430d8c;
inline constexpr auto start_turn_child = 0x430d46;
inline constexpr auto initialize_object_groups = 0x4070e7;
inline constexpr auto allocate_compact_object = 0x40277a;
inline constexpr auto release_compact_object = 0x402a02;
inline constexpr auto lookup_actor = 0x457ec9;
}

namespace scripts {
// Actual executable script entry points used by the current dispatcher and original callers.
inline constexpr auto event0_terminator = 0x61f96a;
inline constexpr auto turn = 0x6caf88;
inline constexpr auto walk_pose = 0x6cb7ee;
inline constexpr auto alternate_walk_pose = 0x6cc696;
inline constexpr auto walk = 0x6cdfd3;
inline constexpr auto alternate_walk = 0x6ce02a;
inline constexpr auto path_followup = 0x6ce081;
inline constexpr auto alternate_path_followup = 0x6ce1b8;
inline constexpr auto sweat_left = 0x6ce76c;
inline constexpr auto sweat_right = 0x6ce8b3;
}

namespace units {
// Original fixed-point and timer domains:408de1/408e69,406ee7/406f73,4301e1/449005.
inline constexpr auto q16_one = 65536;
inline constexpr auto tile_width_q16 = 0x400000;
inline constexpr auto tile_height_q16 = 0x300000;
inline constexpr auto progress_complete = 30030;
}

namespace layout {
inline constexpr auto overlay_sprite_frame_size = 24;
inline constexpr auto sprite_sheet_size = 68;
inline constexpr auto worldmap_city_stride = 0x54; // 21 DWORDs, not21 bytes.
// Original storage strides; distinct equal-valued roles retain separate names.
inline constexpr auto actor_size = 0x1ac;
inline constexpr auto compact_size = 0x1a8;
inline constexpr auto group_sentinel_stride = 0x350;
inline constexpr auto map_layer_stride = 0x8028;
inline constexpr auto draw_record_size = 84;
inline constexpr auto draw_pass_stride = 428; //454cb5/4577d6: starts in one shared pool.
}

namespace capacity {
inline constexpr auto actor_slots = 768; //4478ba scans8073d8..857127 with1ac-byte stride.
inline constexpr auto speaker_name_bytes = 32; //40dd27 bounds its <ids_NAME> copy.
// Original array/pool capacities checked by this implementation.
inline constexpr auto usable_compact_slots = 1023;
inline constexpr auto object_groups = 7;
inline constexpr auto map_cells = 4096;
inline constexpr auto tile_animation_records = 256; //7d3ca0..7d8c9f, 80-byte descriptors.
inline constexpr auto draw_passes = 4;
inline constexpr auto draw_records = draw_passes * layout::draw_pass_stride;
inline constexpr auto generic_draw_records_per_pass = 428; //4549ef saturation, not an actor/foreground limit.
inline constexpr auto draw_queue = 800;
}

namespace timer_offset {
// 406ee7/406f73/406fc8 timer layout; byte offsets.
inline constexpr auto remaining = 4;
inline constexpr auto frame_count = 8;
inline constexpr auto frame_step = 12;
inline constexpr auto deadline_ms = 16;
inline constexpr auto duration_ms = 20;
inline constexpr auto counts_up = 24;
}

namespace operand_flag {
// Operand::location/get/set and original VM operand descriptor decoding; width is in bytes.
inline constexpr auto absolute_address = 0x80;
inline constexpr auto indirect = 0x40;
inline constexpr auto width_mask = 0x3f;
}

namespace compact_flag {
// 40277a allocation,402a02 release,407ded/407ffd object pump. Flags of compact objects only.
inline constexpr auto standby = 2;
inline constexpr auto skip_or_standby = 3;
inline constexpr auto skip_constructor = 0x10000;
inline constexpr auto immediate_exclusive_handoff = 0x40000; //402abb: current owner may enter standby immediately.
inline constexpr auto standby_on_finish = 0x80000;
inline constexpr auto exclusive = 0x100000;
inline constexpr auto count_visible = 0x200000;
inline constexpr auto count_runtime_reference = 0x400000;
inline constexpr auto draw_surface = 0x1000000;
inline constexpr auto delayed_script_message = 0x2000000;
}

namespace draw_kind {
// Original4546f4 command table. Odd/even pair names describe clipping, not an implicit color key.
inline constexpr auto surface_if_dirty=0,clipped_surface=1,sheet=2,clipped_sheet=3,checker_tile=4;
inline constexpr auto stretch_sheet=5,clipped_stretch_sheet=6,stretch_frame=7,clipped_stretch_frame=8;
inline constexpr auto cached_surface=9,clipped_cached_surface=10,stretch_cached_surface=11,clipped_stretch_cached_surface=12;
}
namespace blit_flags {
//405db3/405dd6 and405eab/405ed1 use two different DirectDraw flag encodings.
inline constexpr auto fast_source_key=0x1u,fast_wait=0x10u;
inline constexpr auto rectangle_source_key=0x8000u,rectangle_wait=0x1000000u;
}
namespace sprite_bank_kind {
//High word of the actor/compact selector.454cb5 alone adds the overlay bank.
inline constexpr auto character=0,indexed=1,active_effect=2,overlay=3;
}
namespace image_format {
inline constexpr auto automatic=0,bitmap=1,pcx=2; //4050fa/406044, selected by402baa when zero.
}

namespace input_message {
inline constexpr auto close = 0x10; //407ffd ->40110f; host-independent quit request.
// 40758d keyboard hook and original normalized input packets; mouse/script messages are game-specific.
inline constexpr auto key_down = 0x100;
inline constexpr auto key_up = 0x101;
inline constexpr auto system_key_down = 0x104;
inline constexpr auto system_key_up = 0x105;
inline constexpr auto mouse_button = 0x403;
inline constexpr auto script_message = 0x406;
}

namespace keyboard_flag {
// 40758d WH_KEYBOARD bit layout. Full high-bit literals remain unsigned in the existing code.
inline constexpr auto extended_key = 0x1000000;
inline constexpr auto alt_context = 0x20000000;
inline constexpr auto previously_down = 0x40000000;
}

namespace video_flag {
// 401dcf target-surface selection and /windowed parsing; bit8 is windowed, not color depth.
inline constexpr auto windowed = 8;
inline constexpr auto default_probe = 0x20;
inline constexpr auto system_safe = 0x40;
}

namespace surface_flag {
// 401dcf chooses target surface using DirectDraw surface-state bit2.
inline constexpr auto blit_backbuffer_path = 4;
}

namespace runtime_profile {
// Existing portable Runtime initialization profile; names current policy without changing it.
inline constexpr auto fullscreen_video = 0x21;
}

namespace alu {
// 41a126 sequence ALU. Signed comparison aliases2..5 remain numeric; opcode29/09 logical shift is distinct.
inline constexpr auto equal = 0;
inline constexpr auto not_equal = 1;
inline constexpr auto logical_and = 6;
inline constexpr auto logical_or = 7;
inline constexpr auto add = 8;
inline constexpr auto subtract = 9;
inline constexpr auto multiply = 10;
inline constexpr auto signed_divide = 11;
inline constexpr auto signed_remainder = 12;
inline constexpr auto bit_and = 13;
inline constexpr auto bit_and_not = 14;
inline constexpr auto bit_or = 15;
inline constexpr auto bit_xor = 16;
inline constexpr auto shift_left = 17;
inline constexpr auto arithmetic_shift_right = 18;
}

namespace compact_offset {
// 40277a/402a02 and compact object pump; byte offsets in424-byte objects.
inline constexpr auto next = 4;
inline constexpr auto next_free = 8;
inline constexpr auto exclusive_wake_target = 0xc; //402abb/402832 only: pointer, zero, or -1 (no successor).
inline constexpr auto generation = 0x10;
inline constexpr auto callback = 0x14;
inline constexpr auto flags = 0x18;
inline constexpr auto flags_high_byte = 0x1b;
inline constexpr auto argument = 0x1c;
inline constexpr auto lifecycle = 0x20;
inline constexpr auto tick_count = 0x24;
inline constexpr auto delayed_message_countdown = 0x28;
inline constexpr auto state_pointer = 0x1a4;
}

namespace vm_offset {
// Original VM opcodes, HSM cache and actor-attach contract. Byte offsets, not actor fields.
inline constexpr auto pc = 0x30;
inline constexpr auto previous_pc = 0x34;
inline constexpr auto same_pc_count = 0x38;
inline constexpr auto delayed_message_target = 0x3c;
inline constexpr auto wait_count = 0x40;
inline constexpr auto result = 0x44;
inline constexpr auto block_heads = 0x48;
inline constexpr auto block_exits = 0x68;
inline constexpr auto block_counts = 0x88;
inline constexpr auto block_depth = 0xa8;
inline constexpr auto block_auxiliaries = 0xac;
inline constexpr auto blocking_child = 0xcc;
inline constexpr auto async_child = 0xd0;
inline constexpr auto auxiliary_child = 0xdc;
inline constexpr auto message_channel = 0xf4;
inline constexpr auto actor_id = 0xf8;
inline constexpr auto actor_object = 0xfc;
inline constexpr auto progress_count = 0x100;
inline constexpr auto cached_step_flags = 0x104;
inline constexpr auto frame_bias = 0x108;
inline constexpr auto cached_message_target = 0x10c;
inline constexpr auto cached_message_channel = 0x110;
inline constexpr auto cached_message_code = 0x114;
inline constexpr auto cached_message_sender = 0x118;
}

namespace actor_offset {
// Original actor record (1ac),4301e1/430059/45f1dd/45d91d. Byte offsets.
inline constexpr auto flags = 4;
inline constexpr auto world_x = 8;
inline constexpr auto world_y = 12;
inline constexpr auto elevation = 0x10;
inline constexpr auto tile_x_q16 = 0x14;
inline constexpr auto tile_y_q16 = 0x18;
inline constexpr auto layer_q16 = 0x1c;
inline constexpr auto draw_offset_x = 0x20;
inline constexpr auto draw_offset_y = 0x24;
inline constexpr auto path_cost = 0x30;
inline constexpr auto path_count = 0x34;
inline constexpr auto path_cursor = 0x38;
inline constexpr auto path_commands = 0x3c;
inline constexpr auto motion_state = 0x104;
inline constexpr auto motion_frame = 0x108;
inline constexpr auto frame_group = 0x10c;
inline constexpr auto facing = 0x110;
inline constexpr auto target_facing = 0x114;
inline constexpr auto template_link = 0x11c;
inline constexpr auto screen_anchor_x = 0x120;
inline constexpr auto screen_anchor_y = 0x124;
inline constexpr auto tile_x = 0x128;
inline constexpr auto tile_y = 0x12c;
inline constexpr auto sprite_base = 0x130;
inline constexpr auto sprite_selector = 0x134;
inline constexpr auto sprite_frame = 0x138;
inline constexpr auto dialogue_handle = 0x13c;
inline constexpr auto sequence_handle = 0x140;
inline constexpr auto callback_tick_count = 0x144;
inline constexpr auto callback = 0x148;
inline constexpr auto callback_state = 0x14c;
inline constexpr auto callback_argument = 0x1a8;
}

namespace draw_offset {
// Original84-byte draw records, checked by actor and shadow/foreground x86 fixtures.
inline constexpr auto type = 4;
inline constexpr auto layer_state = 8;
inline constexpr auto sort_key = 12;
inline constexpr auto actor = 0x10;
inline constexpr auto screen_x = 0x14;
inline constexpr auto screen_y = 0x18;
inline constexpr auto source_left = 0x1c;
inline constexpr auto source_top = 0x20;
inline constexpr auto source_right = 0x24;
inline constexpr auto source_bottom = 0x28;
inline constexpr auto world_left = 0x2c;
inline constexpr auto world_top = 0x30;
inline constexpr auto world_right = 0x34;
inline constexpr auto world_bottom = 0x38;
inline constexpr auto sheet = 0x3c;
inline constexpr auto frame = 0x40;
inline constexpr auto blit_flags = 0x44;
inline constexpr auto checker_fill = 0x48;
inline constexpr auto surface = 0x50;
}

namespace dialog_offset {
// 40dd27 and layout/position helpers. Byte offsets into the dialogue state allocation.
inline constexpr auto anchor_y_pointer = 4;
inline constexpr auto style_variant = 12;
inline constexpr auto width_profile = 16;
inline constexpr auto speaker_name = 0x20; //40dd27 <ids_NAME>: at most32 source bytes, no added terminator.
inline constexpr auto speaker_name_length = 0x40;
inline constexpr auto box_style = 0x44;
inline constexpr auto text_style = 0x4c;
inline constexpr auto foreground = 0x54;
inline constexpr auto outline = 0x58;
inline constexpr auto line_capacity = 0x5c;
inline constexpr auto column_capacity = 0x60;
inline constexpr auto first_indent = 0x64;
inline constexpr auto continuation_indent = 0x68;
inline constexpr auto trimmed_columns = 0x6c;
inline constexpr auto width = 0x70;
inline constexpr auto height = 0x74;
inline constexpr auto source_left = 0x78;
inline constexpr auto source_top = 0x7c;
inline constexpr auto source_right = 0x80;
inline constexpr auto source_bottom = 0x84;
inline constexpr auto text_surface = 0x88;
inline constexpr auto skin_surface = 0x8c;
inline constexpr auto reveal_surface = 0x90;
inline constexpr auto anchor_x = 0x94;
inline constexpr auto anchor_y = 0x98;
inline constexpr auto position_order = 0xb0;
inline constexpr auto target_rect = 0xb4;
inline constexpr auto current_rect = 0xc4;
inline constexpr auto tail_frame = 0x11c;
inline constexpr auto previous_tail_frame = 0x120;
inline constexpr auto tail_x = 0x124;
inline constexpr auto tail_y = 0x128;
inline constexpr auto saved_tail_x = 0x12c;
inline constexpr auto saved_tail_y = 0x130;
inline constexpr auto saved_tail_frame = 0x134;
inline constexpr auto wait_scale = 0x138;
inline constexpr auto wait_time = 0x13c;
inline constexpr auto line = 0x144;
inline constexpr auto explicit_line_start = 0x148;
inline constexpr auto column = 0x14c;
inline constexpr auto flags_a = 0x154;
inline constexpr auto flags_b = 0x158;
inline constexpr auto flags_c = 0x15c;
inline constexpr auto pause_count = 0x160;
inline constexpr auto default_message_target = 0x164;
inline constexpr auto waiting_message_channel = 0x168;
inline constexpr auto choice_scroll = 0x16c;
inline constexpr auto choice_cursor = 0x170;
inline constexpr auto box_timer = 0x174;
inline constexpr auto tail_timer = 0x190;
inline constexpr auto delay_timer = 0x1ac;
}

namespace dialog_word {
// Same dialogue field in DWORD-indexed get/put/r/w calls; not a byte offset.
inline constexpr auto anchor_x_pointer = 0x0;
inline constexpr auto anchor_y_pointer = 0x1;
inline constexpr auto text = 0x2;
inline constexpr auto style_variant = 0x3;
inline constexpr auto width_profile = 0x4;
inline constexpr auto min_lines = 0x5;
inline constexpr auto max_lines = 0x6;
inline constexpr auto forced_lines = 0x7;
inline constexpr auto box_style = 0x11;
inline constexpr auto position = 0x12;
inline constexpr auto text_style = 0x13;
inline constexpr auto saved_text_style = 0x14;
inline constexpr auto foreground = 0x15;
inline constexpr auto outline = 0x16;
inline constexpr auto line_capacity = 0x17;
inline constexpr auto column_capacity = 0x18;
inline constexpr auto first_indent = 0x19;
inline constexpr auto continuation_indent = 0x1a;
inline constexpr auto trimmed_columns = 0x1b;
inline constexpr auto width = 0x1c;
inline constexpr auto height = 0x1d;
inline constexpr auto source_left = 0x1e;
inline constexpr auto source_top = 0x1f;
inline constexpr auto source_right = 0x20;
inline constexpr auto source_bottom = 0x21;
inline constexpr auto text_surface = 0x22;
inline constexpr auto anchor_x = 0x25;
inline constexpr auto anchor_y = 0x26;
inline constexpr auto position_order = 0x2c;
inline constexpr auto target_rect = 0x2d;
inline constexpr auto current_rect = 0x31;
inline constexpr auto tail_frame = 0x47;
inline constexpr auto tail_x = 0x49;
inline constexpr auto tail_y = 0x4a;
inline constexpr auto wait_scale = 0x4e;
inline constexpr auto wait_time = 0x4f;
inline constexpr auto glyph_time_ms = 0x50;
inline constexpr auto line = 0x51;
inline constexpr auto explicit_line_start = 0x52;
inline constexpr auto column = 0x53;
inline constexpr auto cursor = 0x54;
inline constexpr auto flags_a = 0x55;
inline constexpr auto flags_b = 0x56;
inline constexpr auto flags_c = 0x57;
inline constexpr auto pause_count = 0x58;
inline constexpr auto default_message_target = 0x59;
inline constexpr auto waiting_message_channel = 0x5a;
inline constexpr auto choice_scroll = 0x5b;
}

namespace opcode {
inline constexpr auto block_count = 0x14;
inline constexpr auto frame_bias = 0x6f;
inline constexpr auto actor_hop_child = 0xd0;
inline constexpr auto actor_arc_child = 0xd1;
inline constexpr auto stage_actor = 0x4c;
inline constexpr auto restore_actor_stage = 0x4d;
inline constexpr auto refresh_actor_position = 0x52;
inline constexpr auto break_block = 0x0f;
inline constexpr auto worldmap_slot = 0xe6;
// Original dispatcher/implemented opcode family; subop meanings are intentionally not generalized.
inline constexpr auto end = 0x00;
inline constexpr auto yield = 0x01;
inline constexpr auto wait = 0x02;
inline constexpr auto jump = 0x03;
inline constexpr auto conditional_jump = 0x04;
inline constexpr auto branch_table = 0x05;
inline constexpr auto call_table = 0x06;
inline constexpr auto call = 0x07;
inline constexpr auto repeat_call = 0x08;
inline constexpr auto repeat_begin = 0x0b;
inline constexpr auto repeat_end = 0x0c;
inline constexpr auto while_begin = 0x0d;
inline constexpr auto while_end = 0x0e;
inline constexpr auto if_begin = 0x10;
inline constexpr auto else_branch = 0x11;
inline constexpr auto if_end = 0x12;
inline constexpr auto random = 0x19;
inline constexpr auto native_callback = 0x1f;
inline constexpr auto object_field = 0x20;
inline constexpr auto actor_vector = 0x25;
inline constexpr auto copy_vector = 0x26;
inline constexpr auto advance_vector = 0x27;
inline constexpr auto unary_alu = 0x28;
inline constexpr auto binary_alu = 0x29;
inline constexpr auto assign = 0x2c;
inline constexpr auto evaluate_alu = 0x2d;
inline constexpr auto query_step = 0x40;
inline constexpr auto apply_step_height = 0x41;
inline constexpr auto place_actor = 0x42;
inline constexpr auto turn_actor = 0x44;
inline constexpr auto walk_actor = 0x45;
inline constexpr auto follow_path = 0x46;
inline constexpr auto tween_actor = 0x47;
inline constexpr auto actor_visibility = 0x4a;
inline constexpr auto actor_sequence = 0x4f;
inline constexpr auto read_path_command = 0x50;
inline constexpr auto next_path_command = 0x51;
inline constexpr auto effect_lifecycle = 0x58;
inline constexpr auto release_actor = 0x5c;
inline constexpr auto allocate_actor = 0x5d;
inline constexpr auto bind_actor = 0x60;
inline constexpr auto timed_sprite_frame = 0x61;
inline constexpr auto sprite_frame = 0x62;
inline constexpr auto progress_counter = 0x65;
inline constexpr auto current_actor_direction = 0x66;
inline constexpr auto timed_displacement = 0x67;
inline constexpr auto reset_actor_frame = 0x68;
inline constexpr auto message = 0x71;
inline constexpr auto standby_object = 0x80;
inline constexpr auto release_object = 0x81;
inline constexpr auto dialogue_spawn_or_wait = 0x88;
inline constexpr auto dialogue_control = 0x89;
inline constexpr auto viewport = 0x90;
inline constexpr auto event_count = 0x91;
inline constexpr auto event_lifecycle = 0x92;
inline constexpr auto actor_on_screen = 0x9c;
inline constexpr auto compact_sprite = 0xa0;
inline constexpr auto compact_sprite_offset = 0xa1;
inline constexpr auto camera = 0xb1;
inline constexpr auto bgm = 0xc1;
inline constexpr auto audio_control = 0xc2;
inline constexpr auto sound_cue = 0xc8;
inline constexpr auto load_map = 0xe0;
inline constexpr auto current_map = 0xe1;
inline constexpr auto map_patch_or_special = 0xe2;
inline constexpr auto party = 0xe8;
inline constexpr auto wait_transition = 0xec;
inline constexpr auto palette_buffer = 0xed;
inline constexpr auto palette_fade = 0xee;
inline constexpr auto rectangle_effect = 0xef;
}

} // namespace fsb::core
