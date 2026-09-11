#pragma once
#include "primitives.hpp"

namespace fsb::core {
struct MarkupToken {
    unsigned code = 0, bytes = 1;
    std::uint32_t argument = 0, auxiliary = 0;
};
// CP949 byte cursor. Unknown tags retain the original one-byte literal '<'
// fallback; compact message IDs preserve case and leading ASCII zeroes.
MarkupToken tokenize_markup(const Memory&, Address cursor);
MarkupToken tokenize_markup(Memory&, Address cursor); // Includes the original EventTitle scratch-buffer write.
struct DialogMeasure {
    int columns, lines, trim;
};
DialogMeasure measure_dialog(Memory&, Address text, int columns, int indent,
                             int next_indent, int hmax = 0, int hmin = 0, int height = 0);
} // namespace fsb::core
