#pragma once
#include "../primitives.hpp"

namespace fsb::core::map_logic {
// Shared progress bitmap at globals::event_flag_bits. One bit per event id;
// the map patch table reuses the same ids, so a patch id doubles as a flag id.
// Original 0x496fe8/0x49700e/0x497036 divide the id with idiv, so a negative id
// truncates toward zero and keeps the dividend's sign in the remainder.
class EventFlags {
public:
    explicit EventFlags(Memory& memory):memory_(memory){}
    bool test(std::int32_t id)const;   // 0x497036; the original returns 0/1 in EAX.
    void set(std::int32_t id);         // 0x496fe8
    void clear(std::int32_t id);       // 0x49700e
    static constexpr unsigned bits_per_word=32;
private:
    Memory& memory_;
    Address word_of(std::int32_t id)const;
    static std::uint32_t mask_of(std::int32_t id);
};
} // namespace fsb::core::map_logic
