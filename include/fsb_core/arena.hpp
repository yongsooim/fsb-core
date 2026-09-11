#pragma once
#include "primitives.hpp"

namespace fsb::core {
// Compact 0x1a8-byte objects, distinct from the actor/effect 0x1ac-byte pool.
class Arena {
public:
    explicit Arena(Memory& memory) : memory_(memory) {}
    void initialize(); // Original 0x4070e7; call on a fresh PE/BSS state.
    Handle allocate_after(Address predecessor, Address callback, std::uint32_t flags, std::uint32_t argument);
    Handle clone_event(Address entry, unsigned pool_selector);
    Handle activate_event(unsigned id, std::uint32_t trigger = 0xffffffffu);
    void release(Handle handle);
    void standby(Address object); //402a6f.
    void wake(Address object); //402a8f, including deferred exclusive handoff.
    bool activate_exclusive(Address object); //402abb; zero relinquishes the current owner.
    Address active_exclusive(Address excluded = 0) const; //402b67.
    static Address head(unsigned group);
    static Address tail(unsigned group);
    std::vector<Handle> members(unsigned group) const;
private:
    Memory& memory_;
};
} // namespace fsb::core
