#pragma once
#include "raster.hpp"
#include <map>

namespace fsb::core {
// Opaque32-bit surface IDs with CPU indexed pixels. Descriptors live in guest
// memory; there are no COM pointers, OS windows or device calls in this bank.
class Surfaces {
public:
    explicit Surfaces(Memory& memory) : memory_(memory) {}
    Address create(unsigned width, unsigned height);
    Address insert(Image8 image);
    void release(Address id);
    Image8& get(Address id);
    const Image8& get(Address id) const;
    void blit(Address destination, int x, int y, Address source, Rect rect, bool color_key=false,std::optional<Rect> clip=std::nullopt);
    void stretch(Address destination, Rect rect, Address source, Rect source_rect, bool color_key=false,std::optional<Rect> clip=std::nullopt);
    void clear(Address destination, Rect rect, std::uint8_t color=0);
private:
    Memory& memory_;
    std::map<Address,Image8> images_;
};
} // namespace fsb::core
