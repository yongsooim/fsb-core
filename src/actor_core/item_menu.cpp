#include "fsb_core/actor_core/item_menu.hpp"
#include "fsb_core/actor_core/party_status.hpp"
#include "fsb_core/actor_core/actor_runtime.hpp"

namespace fsb::core::actor_core {
namespace {
void put_row(Memory& memory, std::uint32_t row, std::uint32_t item) {
    memory.write(item_row_ids + row * item_row_bytes, item);
    memory.write(item_row_quantities + row * item_row_bytes, 0);
}
} // namespace

std::uint32_t build_shop_item_list(Memory& memory) {
    const auto stock = shop_stock_table + memory.read(active_shop) * shop_stock_bytes;
    std::uint32_t rows = 0;
    for (unsigned slot = 0; slot < shop_stock_slots; ++slot) {
        const auto item = signed32(memory.read(stock + slot * 4));
        if (item < 0) continue;
        put_row(memory, rows++, std::uint32_t(item));
    }
    return rows;
}

std::uint32_t build_owned_item_list(Memory& memory) {
    const auto items = (owned_item_counts_end - owned_item_counts) / 4;
    std::uint32_t rows = 0;
    for (std::uint32_t item = 0; item < items; ++item) {
        if (signed32(memory.read(owned_item_counts + item * 4)) <= 0) continue;
        if (signed32(memory.read(item_sell_price + item * item_definition_bytes)) <= 0) continue;
        put_row(memory, rows++, item);
    }
    return rows;
}

std::uint32_t sum_selected_item_values(const Memory& memory, std::uint32_t buy_prices) {
    const auto price_table = (buy_prices & 0xff) ? item_buy_price : item_sell_price;
    const auto rows = signed32(memory.read(item_row_count));
    std::uint32_t total = 0;
    for (std::int32_t row = 0; row < rows; ++row) {
        const auto item = memory.read(item_row_ids + std::uint32_t(row) * item_row_bytes);
        const auto price = memory.read(price_table + item * item_definition_bytes);
        total += price * memory.read(item_row_quantities + std::uint32_t(row) * item_row_bytes);
    }
    return total;
}

namespace {
std::uint32_t item_field(const Memory& memory, Address table, std::uint32_t item) {
    return memory.read(table + item * item_definition_bytes);
}
std::int32_t equipped_in_slot(const Memory& memory, std::uint32_t member, unsigned slot) {
    return signed32(memory.read(character_equipment + member * character_record_bytes + slot * 4));
}
// An item with no id contributes nothing to the accessory comparison.
std::int32_t combined_bonus(const Memory& memory, std::int32_t item) {
    if (item == -1) return 0;
    return signed32(item_field(memory, item_attack_bonus, std::uint32_t(item))) +
           signed32(item_field(memory, item_defence_bonus, std::uint32_t(item)));
}
// The weaker of the two accessory slots is the one a new accessory replaces.
std::int32_t weaker_accessory(const Memory& memory, std::uint32_t member) {
    const auto first = equipped_in_slot(memory, member, 3);
    const auto second = equipped_in_slot(memory, member, 4);
    return combined_bonus(memory, first) < combined_bonus(memory, second) ? first : second;
}
// A candidate carrying a low subtype bit is paired against whichever accessory
// slot already carries subtype bit0, if either does.
bool paired_accessory(const Memory& memory, std::uint32_t member, std::int32_t& equipped) {
    for (unsigned slot : {3u, 4u}) {
        equipped = equipped_in_slot(memory, member, slot);
        if (item_field(memory, item_kind_flags, std::uint32_t(equipped)) & 1) return true;
    }
    return false;
}
std::int32_t equipped_for_kind(const Memory& memory, std::uint32_t member,
                               std::uint32_t kind, std::int32_t fallback) {
    switch (kind & equip_kind_high_nibble) {
    case equip_kind_head: return equipped_in_slot(memory, member, 0);
    case equip_kind_body: return equipped_in_slot(memory, member, 1);
    case equip_kind_hand: return equipped_in_slot(memory, member, 2);
    case equip_kind_accessory_pair: return weaker_accessory(memory, member);
    // No caller evidence maps any other kind, so the original's argument is
    // passed straight through rather than guessed at.
    default: return fallback;
    }
}
} // namespace

void draw_item_menu_rows(const Memory& memory, bool show_cursor, bool buy_prices,
                         const MenuPainter& painter) {
    const auto anchor_x = signed32(memory.read(menu_anchor_x));
    const auto anchor_y = signed32(memory.read(menu_anchor_y));
    const auto scroll = signed32(memory.read(item_list_scroll));
    const auto selected = signed32(memory.read(item_selected_row));
    const auto rows = signed32(memory.read(item_row_count));
    const auto affordable = signed32(memory.read(party_gold)) - signed32(memory.read(shop_pending_spend));
    for (std::int32_t offset = 0; std::uint32_t(offset) < item_rows_height;
         offset += item_row_height) {
        const auto row = scroll + offset / std::int32_t(item_row_height);
        if (row >= rows) continue;
        const auto item = memory.read(item_row_ids + std::uint32_t(row) * item_row_bytes);
        // The sheet is passed by address, not by value.
        painter.sheet_frame(anchor_x + 0x24, anchor_y + 0x22 + offset, common_sprite_sheet,
                            item_field(memory, item_icon_frames, item));
        auto colour = item_colour_normal;
        if (buy_prices) {
            if (affordable < signed32(item_field(memory, item_buy_price, item)))
                colour = item_colour_unaffordable;
            else if (selected == row && show_cursor)
                colour = item_colour_selected;
        }
        const auto text_y = anchor_y + 0x24 + offset;
        painter.text(anchor_x + 0x40, text_y, colour, item_field(memory, item_names, item), 0, false);
        painter.text(anchor_x + 0xf0, text_y, colour, number_format,
                     item_field(memory, buy_prices ? item_buy_price : item_sell_price, item), true);
        painter.overlay(anchor_x + 0x134, text_y, 0x11);
        painter.text(anchor_x + 0x170, text_y, colour, quantity_format,
                     memory.read(item_row_quantities + std::uint32_t(row) * item_row_bytes), true);
        painter.text(anchor_x + 0x19c, text_y, colour, quantity_format,
                     memory.read(owned_item_counts + item * 4), true);
    }
    if (show_cursor)
        painter.overlay(anchor_x + 0x10,
                        (selected - scroll) * std::int32_t(item_row_height) + 0x24 + anchor_y, 0x12);
}

void draw_equip_stat_arrows(const Memory& memory, std::int32_t x, std::int32_t y,
                            std::uint32_t member, std::uint32_t item,
                            const MenuPainter& painter) {
    painter.overlay(x, y, member + equip_slot_icon_base);
    // The member mask counts down from the top bit, one bit per party position.
    if (!(item_field(memory, item_equippable_members, item) &
          (equip_member_mask_top >> (member & 0x1f))))
        return;
    painter.overlay(x, y, equip_valid_icon);
    const auto kind = item_field(memory, item_kind_flags, item);
    std::int32_t equipped = 0;
    const bool has_subtype = kind != 0 && (kind & equip_kind_low_nibble) != 0;
    if (!(has_subtype && paired_accessory(memory, member, equipped)))
        // The original passes the screen y here as the unhandled-kind fallback;
        // no caller or table shows that as a live item id, so it is preserved
        // rather than reinterpreted.
        equipped = equipped_for_kind(memory, member, kind, y);
    // A slot with nothing in it compares as zero attack and zero defence.
    const auto equipped_bonus = [&](Address table) {
        return equipped < 0 ? 0 : signed32(item_field(memory, table, std::uint32_t(equipped)));
    };
    const auto attack = compare_three_way(equipped_bonus(item_attack_bonus),
                                          signed32(item_field(memory, item_attack_bonus, item)));
    const auto defence = compare_three_way(equipped_bonus(item_defence_bonus),
                                           signed32(item_field(memory, item_defence_bonus, item)));
    painter.overlay(x + 4, y + 0x1b, attack + equip_arrow_icon_base);
    painter.overlay(x + 0x12, y + 0x1b, defence + equip_arrow_icon_base);
}
} // namespace fsb::core::actor_core
