#pragma once
#include "sprites.hpp"
#include "map.hpp"

namespace fsb::core {
// Original shared record pool with four pass starts and a global pointer queue. Sorting preserves
// the original selection-sort swaps, including the order of equal-depth items.
class DrawQueue {
public:
    DrawQueue(Memory& memory, Surfaces& surfaces, Sprites& sprites) : memory_(memory), surfaces_(surfaces), sprites_(sprites) {}
    void reset();
    Address actor(unsigned slot);
    unsigned tile(unsigned owner, Address object, std::uint32_t key, std::uint32_t raw_pass,
                  std::uint32_t x, std::uint32_t y, Address sheet, std::uint32_t frame, std::uint32_t flags);
    void background(const std::vector<TileCommand>& commands, Address sheet);
    void shadows(unsigned pass, Address sheet);
    void foreground(unsigned layer,bool battle=false); // Original4577d6 normal/checker foreground.
    void enqueue(unsigned pass, unsigned index);
    void flush(unsigned pass);
    void sort(unsigned pass, unsigned first);
    void execute();
    static Address record(unsigned pass, unsigned index);
private:
    Memory& memory_; Surfaces& surfaces_; Sprites& sprites_;
};
} // namespace fsb::core
