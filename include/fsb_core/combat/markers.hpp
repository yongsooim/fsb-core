#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>
namespace fsb::core::combat {
namespace markers {
inline constexpr Address slots=0x77ec60, callback=0x44c313, slot_index=0x108;
inline constexpr unsigned capacity=30, sprite=0xd0, battle_mode=9;
}
struct MarkerServices {
    std::function<Address(Address callback)> spawn;
    std::function<void(Address)> release;
    std::function<void(Address,std::uint32_t,std::uint32_t,std::uint32_t)> place_actor;
};
class Markers {
public:
    Markers(Memory& memory,const MarkerServices& services):memory_(memory),services_(services){}
    Address spawn(std::uint32_t x,std::uint32_t y,std::uint32_t grid);
    void release_slot(unsigned index);
    void tick(Address object);
private:
    Memory& memory_;
    const MarkerServices& services_;
};
}
