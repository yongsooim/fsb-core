#pragma once
#include "primitives.hpp"
#include <functional>
namespace fsb::core {
struct StaticDialogTemplate {
    unsigned id;
    Address korean,other;
    bool localized;
};
struct DialogTemplateServices {
    std::function<void()> observe_player;
    std::function<std::uint32_t()> read_substate;
    std::function<Address(unsigned id,Address blob,std::uint32_t slot)> create;
    // ABI adapters implement nullable outputs and preserve publication order.
    std::function<void(std::uint32_t)> facing,menu_context,side_effect;
};
class DialogTemplates {
public:
    explicit DialogTemplates(Memory& memory):memory_(memory){}
    static const StaticDialogTemplate* find_static(unsigned id);
    Address expand_static(const StaticDialogTemplate& row,std::uint32_t slot,const DialogTemplateServices& services);
private:
    Memory& memory_;
};
}
