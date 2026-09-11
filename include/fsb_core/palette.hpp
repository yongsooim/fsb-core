#pragma once
#include "raster.hpp"

namespace fsb::core {
using PaletteEntries = std::array<std::uint32_t, 256>;

// Portable replacement for the game's logical DirectDraw palette. DWORDs keep
// PALETTEENTRY byte order (R,G,B,flags), including source-buffer mutations.
// The initial active entries are explicit startup data, not a system lookup.
class Palette {
public:
    Palette(Memory& memory, PaletteEntries initial) : memory_(memory), active_(initial) {}
    void upload(Address source, unsigned start = 0, unsigned count = 256, bool copy_first = false);
    void capture(Address destination, unsigned start = 0, unsigned count = 256) const;
    void copy(Address destination, Address source, unsigned count = 256);
    void scale(Address destination, Address source, std::uint32_t percent);
    void add_delta(Address destination, std::uint32_t delta);
    // Original404e4c operation, independent of registers, addresses and call stacks.
    // Source/destination may overlap; entry flags are copied before RGB bytes.
    static std::uint32_t adjust_rgb(std::span<std::uint8_t> destination,std::span<const std::uint8_t> source,std::uint32_t delta);
    static std::uint8_t grayscale(std::span<std::uint8_t> destination,std::span<const std::uint8_t> source);
    static void copy_words(std::span<std::uint8_t> destination,std::span<const std::uint8_t> source);
    std::uint32_t gradient(Address destination,std::uint32_t start_rgb,std::uint32_t end_rgb,std::uint32_t start,std::uint32_t count);
    void begin(Address final, Address initial, std::uint32_t clamp, std::uint32_t duration);
    void advance(unsigned jobs);
    bool busy() const { return memory_.read(0x4a57c8) != 0xffffffffu; }
    const PaletteEntries& raw_entries() const { return active_; }
    std::array<Rgb, 256> colors() const;
    std::uint8_t index(std::uint32_t color) const; // PALETTEINDEX or nearest logical RGB entry.
private:
    Memory& memory_;
    PaletteEntries active_;
    bool windowed_mode() const { return (memory_.read(0x6d9ebc) & 8) != 0; }
};
} // namespace fsb::core
