#pragma once
#include "fsb_core/primitives.hpp"

namespace fsb::core::combat {
// Read a cell by byte index and publish result words by word index. Neither
// operation assumes where the caller stores its buffers. Immediate writes
// preserve aliases between input and output, including the original span layout.
template<class ReadCell, class WriteWord>
unsigned extract_mask_offsets(const ReadCell& cell, const WriteWord& output) {
    unsigned found = 0;
    for (unsigned row = 0; row < 11; ++row)
        for (unsigned column = 0; column < 11; ++column)
            if (cell(row * 11 + column) & 4) {
                output(found * 2, column - 5u);
                output(found * 2 + 1, row - 5u);
                ++found;
            }
    return found;
}

template<class ReadCell, class WriteWord>
std::int32_t extract_mask_spans(std::int32_t width, std::int32_t height,
                              std::int32_t origin_x, std::int32_t origin_y,
                              unsigned bit, const ReadCell& cell, const WriteWord& output) {
    std::uint32_t found = 0, cursor = 1;
    for (std::int32_t row = 0; row < height; ++row) {
        bool previous = false;
        for (std::int32_t column = 0; column < width; ++column) {
            const bool set = (cell(std::uint32_t(row) * std::uint32_t(width) + std::uint32_t(column)) & bit) != 0;
            if (set) {
                // The original advances the cursor when a run opens; preserve
                // its subsequent end-column writes into the following record.
                output(cursor + 1, std::uint32_t(column) - std::uint32_t(origin_x));
                if (!previous) {
                    output(cursor - 1, std::uint32_t(row) - std::uint32_t(origin_y));
                    output(cursor, std::uint32_t(column) - std::uint32_t(origin_x));
                    ++found;
                    cursor += 3;
                }
            }
            previous = set;
        }
    }
    return signed32(found);
}
} // namespace fsb::core::combat
