#pragma once
#include "actor_slots.hpp"

// The row list the item and shop menus work from.
//
// A row is a pair of words: the item id at0x802cc0 + row *8 and the quantity
// the player has selected at0x802cc4 + row *8. 0x802cb8 holds how many rows
// are filled. Both builders rewrite the list from scratch and return the count.
namespace fsb::core::actor_core {

inline constexpr Address item_row_ids = 0x802cc0;
inline constexpr Address item_row_quantities = 0x802cc4;
inline constexpr unsigned item_row_bytes = 8;
inline constexpr Address item_row_count = 0x802cb8;

// Item definitions are0x4c bytes at0x613144; +0x4c is the sell price and
// +0x50 the buy price, so those land on these two bases.
inline constexpr Address item_sell_price = 0x613190;
inline constexpr Address item_buy_price = 0x613194;
inline constexpr unsigned item_definition_bytes = 0x4c;

// The shop stock table:0x14 item ids per shop,0x50 bytes per shop.
inline constexpr Address shop_stock_table = 0x5d0ae0;
inline constexpr unsigned shop_stock_slots = 0x14;
inline constexpr unsigned shop_stock_bytes = 0x50;
inline constexpr Address active_shop = 0x80382c;

// The owned-item counters, one word per item id.
inline constexpr Address owned_item_counts = 0x806e30;
inline constexpr Address owned_item_counts_end = 0x8073d0;

// 45e167: fill the list from the active shop's stock, skipping empty slots.
std::uint32_t build_shop_item_list(Memory& memory);
// 45e1a1: fill the list from what the player owns, skipping items with no
// sell price. Both quantities start at zero.
std::uint32_t build_owned_item_list(Memory& memory);
// 45e1dc: total the selected quantities. A zero low byte prices the rows to
// sell; anything else prices them to buy.
std::uint32_t sum_selected_item_values(const Memory& memory, std::uint32_t buy_prices);

// --- drawing -------------------------------------------------------------
// Both routines below only decide layout; every mark they make leaves through
// one of these three sinks, which the caller wires to the original services.
struct MenuPainter {
    // 40587f: one frame of a sprite sheet, colour-keyed.
    std::function<void(std::int32_t x, std::int32_t y, Address sheet, std::uint32_t frame)> sheet_frame;
    // 4067ec: formatted text. `argument` is only meaningful when `formatted`
    // is set; a plain string is drawn with the format pointer alone.
    std::function<void(std::int32_t x, std::int32_t y, std::uint32_t colour, Address format,
                       std::uint32_t argument, bool formatted)> text;
    // 45dc18: one entry of the special overlay table.
    std::function<void(std::int32_t x, std::int32_t y, std::uint32_t overlay)> overlay;
};

// The item record is0x4c bytes at a base that puts these fields here.
inline constexpr Address item_icon_frames = 0x613188;
inline constexpr Address item_names = 0x61318c;
inline constexpr Address item_kind_flags = 0x6131a0;
inline constexpr Address item_equippable_members = 0x6131a4;
inline constexpr Address item_attack_bonus = 0x6131b8;
inline constexpr Address item_defence_bonus = 0x6131bc;

// Where the shop menu is anchored, and what it is showing.
inline constexpr Address menu_anchor_x = 0x803810;
inline constexpr Address menu_anchor_y = 0x803814;
inline constexpr Address item_list_scroll = 0x803804;
inline constexpr Address item_selected_row = 0x802cb0;
inline constexpr Address party_gold = 0x803a18;
inline constexpr Address shop_pending_spend = 0x803838;
// Passed to the sheet blit by address; the blit reads the record itself.
inline constexpr Address common_sprite_sheet = 0x804ddc;
inline constexpr Address number_format = 0x5b3504;
inline constexpr Address quantity_format = 0x5d2250;

// 45e231: the nine visible rows of the item list, and the cursor.
// A row is dimmed when the player cannot afford it and highlighted when it is
// the selected row; only the buy column makes either decision.
inline constexpr unsigned item_row_height = 0x15;
inline constexpr unsigned item_rows_height = 0xbd;
inline constexpr std::uint32_t item_colour_normal = 0x10000ff;
inline constexpr std::uint32_t item_colour_unaffordable = 0x10000d0;
inline constexpr std::uint32_t item_colour_selected = 0x10000df;
void draw_item_menu_rows(const Memory& memory, bool show_cursor, bool buy_prices,
                         const MenuPainter& painter);

// 45e421: the two arrows that preview what equipping an item would do to one
// party member's attack and defence. Nothing is previewed when the member
// cannot wear the item. Which equipped item it compares against depends on the
// candidate's kind: head, body and hand each have their own slot, an accessory
// pair compares against whichever of its two slots is worth less, and a
// candidate carrying any low-nibble subtype bit compares against whichever
// accessory slot already carries subtype bit0.
inline constexpr std::uint32_t equip_kind_high_nibble = 0xf0;
inline constexpr std::uint32_t equip_kind_low_nibble = 0x0f;
inline constexpr std::uint32_t equip_kind_head = 0x10;
inline constexpr std::uint32_t equip_kind_body = 0x20;
inline constexpr std::uint32_t equip_kind_hand = 0x30;
inline constexpr std::uint32_t equip_kind_accessory_pair = 0x40;
inline constexpr std::uint32_t equip_member_mask_top = 0x8000;
inline constexpr std::uint32_t equip_slot_icon_base = 0x1a;
inline constexpr std::uint32_t equip_valid_icon = 2;
inline constexpr std::uint32_t equip_arrow_icon_base = 0xe;
void draw_equip_stat_arrows(const Memory& memory, std::int32_t x, std::int32_t y,
                            std::uint32_t member, std::uint32_t item,
                            const MenuPainter& painter);

} // namespace fsb::core::actor_core
