#include "fsb_core/actor_core/party_status.hpp"

namespace fsb::core::actor_core {
namespace {
// Each equipment slot is gated by its own pair of bits: the item must carry
// `required`, and the rest of the equipment must carry `opposing`.
struct SlotGate { std::uint32_t required, opposing; };
constexpr SlotGate slot_gates[equipment_slots] = {
    {0, 1},        // Slot0 reads the whole-mask bit directly.
    {4, 0x18},
    {0x20, 0x40},
    {2, 4},
    {2, 4},        // Slot4 shares slot3's rule.
};

std::int32_t equipped_item(const Memory& memory, std::uint32_t character, unsigned slot) {
    return signed32(memory.read(character_equipment + character * character_record_bytes + slot * 4));
}
std::uint32_t item_flags(const Memory& memory, std::int32_t item) {
    return memory.read(item_status_flags + std::uint32_t(item) * item_record_bytes);
}
} // namespace

std::uint32_t collect_status_mask(Memory& memory, std::uint32_t character,
                                  std::uint32_t excluded_slot) {
    std::uint32_t mask = 0;
    for (unsigned slot = 0; slot < equipment_slots; ++slot) {
        const auto item = equipped_item(memory, character, slot);
        if (item < 0 || excluded_slot == slot) continue;
        mask |= item_flags(memory, item);
    }
    memory.write(party_status_masks + character * party_status_row_bytes, mask);
    return mask;
}

std::uint32_t status_gate_allows(Memory& memory, std::uint32_t character, std::uint32_t slot) {
    // The whole mask is taken first, then the mask without the slot in question.
    const auto whole = collect_status_mask(memory, character, no_excluded_slot);
    const auto without_slot = collect_status_mask(memory, character, slot);
    if (slot >= equipment_slots) return 1;
    const auto item = equipped_item(memory, character, slot);
    if (item < 0) return 1;
    const auto& gate = slot_gates[slot];
    if (slot == 0) return (whole & gate.opposing) ? 0 : 1;
    if ((item_flags(memory, item) & gate.required) != gate.required) return 1;
    return (without_slot & gate.opposing) ? 0 : 1;
}
} // namespace fsb::core::actor_core
