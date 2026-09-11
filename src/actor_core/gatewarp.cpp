#include "fsb_core/actor_core/gatewarp.hpp"
#include "fsb_core/actor_core/actor_geometry.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
// -1 means the sprite is hidden for that bucket.
constexpr std::int32_t attached_a[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 6, 7, 8, 6, 7, 8, 6, 7, 8, 6, 7, 8, 6, 7, 8,
    7, 6, 5, 4, 3, 2, 1, 0, -1, -1, -1};
constexpr std::int32_t attached_b[] = {
    -1, -1, -1, -1, 9, 10, 11, 12, 13, 11, 12, 13, 11, 12, 13, 11, 12, 13,
    11, 12, 13, 11, 12, 13, 12, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1};
constexpr std::int32_t attached_c[] = {
    -1, -1, -1, -1, 14, 15, 16, 17, 18, 16, 17, 18, 16, 17, 18, 16, 17, 18,
    16, 17, 18, 16, 17, 18, 17, 16, 15, 14, -1, -1, -1, -1, -1, -1, -1};
constexpr std::int32_t screen_a[] = {
    19, 20, 21, 22, 23, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    -1, -1, -1, -1, -1};
constexpr std::int32_t screen_b[] = {
    24, 25, 26, 27, 28, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6,
    5, 4, 3, 2, 1, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    28, 27, 26, 25, 24, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1};

// Each attached sprite sits at its own offset from the actor it follows.
struct AttachedOffset { std::int32_t world_y, elevation; };
constexpr AttachedOffset attached_offsets[] = {
    {-0x100000, -0xc0000}, {0x100000, 0x120000}, {-0x240000, -0x100000}};

// The screen sprites spawn on this tile and split above and below it.
constexpr std::uint32_t screen_tile_x = 0xb, screen_tile_y = 4, screen_tile_layer = 0;
constexpr std::int32_t screen_split = 0x100000;
constexpr std::uint32_t screen_start_frame = 0x13;
// Attached groups live in a stride-three pointer table.
constexpr Address attached_group_table = 0x8021e0;
constexpr unsigned attached_group_stride = 3;
// The screen sprites occupy the first four slots of the same table.
constexpr Address screen_object_slots[] = {0x8021e0, 0x8021e4, 0x8021ec, 0x8021f0};

std::span<const std::int32_t> frame_table(GatewarpSprite sprite) {
    switch (sprite) {
    case GatewarpSprite::AttachedA: return attached_a;
    case GatewarpSprite::AttachedB: return attached_b;
    case GatewarpSprite::AttachedC: return attached_c;
    case GatewarpSprite::ScreenA: return screen_a;
    default: return screen_b;
    }
}

// The frame counter advances in whole buckets; the division is signed.
std::int32_t current_bucket(const Memory& memory) {
    return signed32(memory.read(gatewarp_frame_counter)) / std::int32_t(gatewarp_frames_per_bucket);
}

bool sprite_is_live(const GuestWords& words, Address effect) {
    return signed32(words.read(effect + actor_offset::callback_state)) >= 0;
}

// Publish the bucket's frame, or the hidden pose when the table says so.
void publish_frame(const GuestWords& words, Address effect, GatewarpSprite sprite,
                   std::int32_t bucket, std::uint32_t visible_selector) {
    const auto table = frame_table(sprite);
    if (bucket < 0 || std::size_t(bucket) >= table.size())
        throw Fault(gatewarp_frame_counter, "gatewarp frame bucket outside its table");
    const auto frame = table[std::size_t(bucket)];
    if (frame < 0) {
        words.write(effect + actor_offset::sprite_selector, gatewarp_hidden_selector);
        words.write(effect + actor_offset::sprite_frame, gatewarp_hidden_frame);
    } else {
        words.write(effect + actor_offset::sprite_selector, visible_selector);
        words.write(effect + actor_offset::sprite_frame, std::uint32_t(frame));
    }
}

// Group0 rides the party member the player drives; group1 rides the paired
// actor a two-ended gatewarp set up.
Address followed_actor(const Memory& memory, const GuestWords& words, Address effect) {
    const auto slot = words.read(effect + gatewarp_payload) == 0
                          ? memory.read(globals::active_party_index)
                          : memory.read(gatewarp_paired_actor_slot);
    return globals::actor_objects + slot * layout::actor_size;
}

// Shared record setup for every gatewarp sprite.
void prepare_sprite(Memory& memory, Address effect, std::uint32_t selector,
                    std::uint32_t frame, std::uint32_t payload) {
    const auto words = memory_words(memory);
    words.write(effect + actor_offset::flags, words.read(effect + actor_offset::flags) | gatewarp_draw_bit);
    words.write(effect + actor_offset::sprite_base, gatewarp_sprite_sheet);
    words.write(effect + actor_offset::sprite_selector, selector);
    words.write(effect + actor_offset::sprite_frame, frame);
    words.write(effect + actor_offset::motion_frame, 0);
    words.write(effect + actor_offset::frame_group, 0);
    words.write(effect + actor_offset::facing, 0);
    words.write(effect + actor_offset::motion_state, 0);
    words.write(effect + gatewarp_payload, payload);
}
} // namespace

void tick_attached_gatewarp(Memory& memory, const GuestWords& words, Address effect,
                            GatewarpSprite sprite) {
    const auto actor = followed_actor(memory, words, effect);
    if (!sprite_is_live(words, effect)) return;
    publish_frame(words, effect, sprite, current_bucket(memory), gatewarp_attached_selector);
    place_record_on_tile(words, effect,
                         words.read(actor + actor_offset::tile_x),
                         words.read(actor + actor_offset::tile_y),
                         std::uint32_t(signed32(words.read(actor + actor_offset::layer_q16)) / 0x10000));
    const auto& offset = attached_offsets[unsigned(sprite)];
    words.write(effect + actor_offset::world_y,
                std::uint32_t(signed32(words.read(actor + actor_offset::world_y)) + offset.world_y));
    words.write(effect + actor_offset::elevation,
                std::uint32_t(signed32(words.read(actor + actor_offset::elevation)) + offset.elevation));
    words.write(effect + actor_offset::tile_x,
                std::uint32_t(signed32(words.read(actor + actor_offset::tile_x_q16)) / 0x10000));
    words.write(effect + actor_offset::tile_y,
                std::uint32_t(signed32(words.read(actor + actor_offset::tile_y_q16)) / 0x10000));
}

void tick_screen_gatewarp(Memory& memory, const GuestWords& words, Address effect) {
    if (!sprite_is_live(words, effect)) return;
    publish_frame(words, effect, GatewarpSprite::ScreenA, current_bucket(memory), gatewarp_screen_selector);
}

void tick_gatewarp_timeline(Memory& memory, const GuestWords& words, Address effect) {
    if (!sprite_is_live(words, effect)) return;
    const auto bucket = current_bucket(memory);
    // The loop ends the next time the counter comes back round to its restart.
    if (memory.read(gatewarp_state) == gatewarp_state_leaving_loop &&
        bucket == gatewarp_loop_restart_bucket)
        memory.write(gatewarp_state, gatewarp_state_timeline);
    const auto table = frame_table(GatewarpSprite::ScreenB);
    if (bucket < 0 || std::size_t(bucket) >= table.size())
        throw Fault(gatewarp_frame_counter, "gatewarp frame bucket outside its table");
    const auto frame = table[std::size_t(bucket)];
    if (memory.read(gatewarp_state) < gatewarp_state_timeline || frame < 0) {
        words.write(effect + actor_offset::sprite_selector, gatewarp_hidden_selector);
        words.write(effect + actor_offset::sprite_frame, gatewarp_hidden_frame);
    } else {
        words.write(effect + actor_offset::sprite_selector, gatewarp_screen_selector);
        words.write(effect + actor_offset::sprite_frame, std::uint32_t(frame));
    }
    // Only the sprite holding the advancer flag moves the shared counter, and
    // only while the opening loop is still running.
    if (memory.read(gatewarp_state) != gatewarp_state_looping) return;
    if (words.read(effect + gatewarp_frame_advancer) != 1) return;
    const auto advanced = signed32(memory.read(gatewarp_frame_counter)) + 1;
    memory.write(gatewarp_frame_counter, std::uint32_t(advanced));
    if (advanced > gatewarp_loop_last_frame)
        memory.write(gatewarp_frame_counter, std::uint32_t(gatewarp_loop_restart_bucket));
}

void spawn_attached_gatewarp_group(Memory& memory, std::uint32_t group, Address source_actor,
                                   const InvokeActorCallback& invoke) {
    const auto words = memory_words(memory);
    const std::uint32_t callbacks[] = {0x4580f7, 0x45826c, 0x4583cd};
    const auto tile_x = memory.read(source_actor + actor_offset::tile_x);
    const auto tile_y = memory.read(source_actor + actor_offset::tile_y);
    const auto layer = std::uint32_t(signed32(memory.read(source_actor + actor_offset::layer_q16)) / 0x10000);
    for (unsigned index = 0; index < 3; ++index) {
        const auto effect = spawn_callback_object(memory, callbacks[index], invoke);
        memory.write(attached_group_table + (group * attached_group_stride + index) * 4, effect);
        // Only the first sprite starts visible; the other two wait for a bucket
        // whose table entry is not negative.
        prepare_sprite(memory, effect,
                       index == 0 ? gatewarp_attached_selector : gatewarp_hidden_selector,
                       index == 0 ? 0 : gatewarp_hidden_frame, group);
        place_record_on_tile(words, effect, tile_x, tile_y, layer);
    }
}

void spawn_screen_gatewarp(Memory& memory, const InvokeActorCallback& invoke,
                           const GatewarpPresentation& present) {
    if (present) present();
    const auto words = memory_words(memory);
    const auto map = memory.read(globals::current_map_id);
    struct Spawn { std::uint32_t callback; bool upper; };
    // The pair is spawned twice: one above and one below the anchor tile.
    constexpr Spawn spawns[] = {{0x458691, true}, {0x4588ee, false},
                                {0x458691, true}, {0x4588ee, false}};
    Address last = 0;
    for (unsigned index = 0; index < 4; ++index) {
        const auto effect = spawn_callback_object(memory, spawns[index].callback, invoke);
        memory.write(screen_object_slots[index], effect);
        prepare_sprite(memory, effect,
                       spawns[index].upper ? gatewarp_screen_selector : gatewarp_hidden_selector,
                       spawns[index].upper ? screen_start_frame : gatewarp_hidden_frame, map);
        // The lower sprite of the first pair starts with its advancer off.
        if (index == 1) memory.write(effect + gatewarp_frame_advancer, 0);
        place_record_on_tile(words, effect, screen_tile_x, screen_tile_y, screen_tile_layer);
        const auto shift = spawns[index].upper ? -screen_split : screen_split;
        memory.write(effect + actor_offset::world_y,
                     std::uint32_t(signed32(memory.read(effect + actor_offset::world_y)) + shift));
        memory.write(effect + actor_offset::elevation,
                     std::uint32_t(signed32(memory.read(effect + actor_offset::elevation)) + shift));
        last = effect;
    }
    // Exactly one sprite drives the shared counter forward.
    memory.write(last + gatewarp_frame_advancer, 1);
    memory.write(gatewarp_frame_counter, 0);
    memory.write(gatewarp_state, 0);
    memory.write(gatewarp_sequence_mode, 2);
}
} // namespace fsb::core::actor_core
