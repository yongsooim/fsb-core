#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>
#include <optional>

namespace fsb::core::combat {
namespace attached_effects {
inline constexpr unsigned owner_capacity=32, kinds_per_owner=10, word_bytes=4;
// Kinds1..8 are identified by the battle-result registration branches.
// Cleanup44c954 ties kinds9/10 to the Power Samba/Hiphop status bits.
enum class Kind : std::int32_t { Poison=1, Sleep=2, Silence=3, Paralysis=4, Curse=5,
    PartySpecial=6, Guard=7, Reflect=8, PowerSamba=9, Hiphop=10 };
inline constexpr Address owners=0x805880, counts=0x805588;
inline constexpr Address objects=0x805a78, kinds=0x805f78, callbacks=0x5d26a0;
inline constexpr Address object_owner=0x160, invalid_kind_message=0x5d27f4;
}
struct AttachedEffectServices {
    std::function<Address(Address callback)> spawn;
    std::function<void(Address object)> release;
    std::function<void(Address message)> report;
};
// Groups of runtime effects keyed by their owner. Battle result propagation
// passes actor addresses here; no second copy of the registry is maintained.
class AttachedEffects {
public:
    AttachedEffects(Memory& memory,const AttachedEffectServices& services):memory_(memory),services_(services){}
    bool attach(Address owner,std::int32_t kind);
    bool detach(Address owner,std::int32_t kind);
    bool clear_owner(Address owner);
    void clear_all();
private:
    Memory& memory_;
    const AttachedEffectServices& services_;
    std::optional<unsigned> find_owner(Address owner)const;
    void clear_row(unsigned row);
};
} // namespace fsb::core::combat
