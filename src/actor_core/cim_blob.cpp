#include "fsb_core/actor_core/cim_blob.hpp"
#include "fsb_core/actor_core/session_state.hpp"

namespace fsb::core::actor_core {
namespace {
// Section sizes are derived from the executable's own counts, so both halves
// compute them the same way and the loader can compare them with the header.
struct CimLayout {
    std::uint32_t object_effect_bytes, world_bytes, event_head_bytes, total;
};
CimLayout layout_of(const Memory& memory) {
    CimLayout layout{};
    layout.object_effect_bytes = memory.read(cim_object_effect_words) * 4;
    layout.world_bytes = memory.read(cim_world_record_count) * cim_world_record_bytes;
    layout.event_head_bytes = memory.read(cim_event_row_count) * 4;
    layout.total = layout.event_head_bytes + layout.world_bytes +
                   cim_header_and_bitmap_bytes + layout.object_effect_bytes;
    return layout;
}
void copy_words(Memory& memory, Address destination, Address source, std::uint32_t bytes) {
    for (std::uint32_t at = 0; at < bytes; at += 4)
        memory.write(destination + at, memory.read(source + at));
}
// The whole blob, header included, is XORed a word at a time.
void obfuscate(Memory& memory, Address payload, std::uint32_t bytes, std::uint32_t key) {
    for (std::int32_t at = 0; at < signed32(bytes); at += 4)
        memory.write(payload + std::uint32_t(at), memory.read(payload + std::uint32_t(at)) ^ key);
}
// A world row keeps its city flags two words before the destination map word.
constexpr int cim_world_flags_word = -2;
} // namespace

bool serialize_cim_blob(Memory& memory, CimBlob& blob, const AllocateBlob& allocate,
                        const TickSource& tick, const CimReport& report) {
    if (memory.read(cim_blob_pointer)) {
        if (report) report(CimFailure::AlreadyHeld);
        return false;
    }
    // The key is read before the allocation, exactly as the original does.
    const auto key = tick ? tick() : 0;
    const auto layout = layout_of(memory);
    memory.write(cim_blob_size, layout.total);
    const auto payload = allocate ? allocate(layout.total) : 0;
    memory.write(cim_blob_pointer, payload);
    if (!payload) {
        if (report) report(CimFailure::AllocationFailed);
        memory.write(cim_blob_pointer, 0);
        return false;
    }
    memory.write(payload, layout.total);
    memory.write(payload + 4, cim_magic);
    memory.write(payload + 8, layout.object_effect_bytes);
    memory.write(payload + 12, layout.world_bytes);
    memory.write(payload + 16, layout.event_head_bytes);
    memory.write(payload + 20, 0); // Replaced by the key sentinel after the XOR.
    copy_words(memory, payload + cim_header_words * 4, cim_event_bitmap, cim_event_bitmap_bytes);
    copy_words(memory, payload + cim_section_offset_words * 4, cim_object_effect_bits,
               layout.object_effect_bytes);
    auto at = payload + layout.object_effect_bytes + cim_header_and_bitmap_bytes;
    const auto worlds = signed32(memory.read(cim_world_record_count));
    for (std::int32_t index = 0; index < worlds; ++index) {
        const auto row = cim_world_rows + std::uint32_t(index) * cim_world_row_words * 4;
        memory.write(at, memory.read(row + std::uint32_t(cim_world_flags_word * 4)));
        memory.write(at + 4, memory.read(row));
        memory.write(at + 8, memory.read(row + 4));
        at += cim_world_record_bytes;
    }
    const auto events = signed32(memory.read(cim_event_row_count));
    for (std::int32_t index = 0; index < events; ++index) {
        memory.write(at, memory.read(cim_event_rows + std::uint32_t(index) * cim_event_row_words * 4));
        at += 4;
    }
    obfuscate(memory, payload, layout.total, key);
    memory.write(payload + 20, key ^ cim_key_sentinel);
    blob = {payload, layout.total};
    return true;
}

bool deserialize_cim_blob(Memory& memory, Address payload, const CimReport& report) {
    const auto fail = [&](CimFailure reason) {
        if (report) report(reason);
        return false;
    };
    if (!payload) return fail(CimFailure::NullBlob);
    // Header[5] carries the key, so it has to be read before anything else is
    // decoded; the total size is the first thing the key unlocks.
    const auto key = memory.read(payload + 20) ^ cim_key_sentinel;
    const auto layout = layout_of(memory);
    if ((memory.read(payload) ^ key) != layout.total) return fail(CimFailure::TotalSize);
    obfuscate(memory, payload, layout.total, key);
    if (memory.read(payload + 4) != cim_magic) return fail(CimFailure::Magic);
    const auto object_effect_bytes = memory.read(payload + 8);
    if (object_effect_bytes != layout.object_effect_bytes) return fail(CimFailure::ObjectEffectSize);
    if (memory.read(payload + 12) != layout.world_bytes) return fail(CimFailure::WorldRecordSize);
    if (memory.read(payload + 16) != layout.event_head_bytes) return fail(CimFailure::EventFlagSize);
    copy_words(memory, cim_event_bitmap, payload + cim_header_words * 4, cim_event_bitmap_bytes);
    copy_words(memory, cim_object_effect_bits, payload + cim_section_offset_words * 4,
               object_effect_bytes);
    auto at = payload + object_effect_bytes + cim_header_and_bitmap_bytes;
    const auto worlds = signed32(memory.read(cim_world_record_count));
    for (std::int32_t index = 0; index < worlds; ++index) {
        const auto row = cim_world_rows + std::uint32_t(index) * cim_world_row_words * 4;
        memory.write(row + std::uint32_t(cim_world_flags_word * 4), memory.read(at));
        memory.write(row, memory.read(at + 4));
        memory.write(row + 4, memory.read(at + 8));
        at += cim_world_record_bytes;
    }
    const auto events = signed32(memory.read(cim_event_row_count));
    for (std::int32_t index = 0; index < events; ++index) {
        memory.write(cim_event_rows + std::uint32_t(index) * cim_event_row_words * 4, memory.read(at));
        at += 4;
    }
    return true;
}
} // namespace fsb::core::actor_core
