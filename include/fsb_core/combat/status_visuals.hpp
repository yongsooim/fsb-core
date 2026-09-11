#pragma once
#include "fsb_core/combat/attached_effects.hpp"
namespace fsb::core::combat {
class StatusVisuals {
public:
    using Detach=std::function<void(Address,attached_effects::Kind)>;
    StatusVisuals(Memory& memory,const Detach& detach):memory_(memory),detach_(detach){}
    void remove_expired(); //44c954
private:
    Memory& memory_;
    const Detach& detach_;
    void remove_row(Address actor,Address status,bool enemy);
};
}
