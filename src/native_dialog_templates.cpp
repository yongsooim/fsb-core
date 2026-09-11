#include "fsb_core/dialog_templates.hpp"
#include "fsb_core/recovered_battle.hpp"
namespace fsb::core {
bool RecoveredBattle::dispatch_static_dialog_template(Address entry) {
    switch(entry){case 0x413da7:break;default:return false;}
    const auto row=DialogTemplates::find_static(argument(0));
    if(!row)return false; // Explicit partial migration: complex IDs use the retained original body.
    const auto slot=argument(1);
    DialogTemplateServices s;
    s.observe_player=[this]{callback(0x457e56,{});};
    s.read_substate=[this]{return read(argument(5));};
    s.create=[this](unsigned id,Address blob,std::uint32_t slot){return callback(0x4139b6,{id,blob,slot});};
    s.facing=[this](std::uint32_t v){if(const auto out=argument(2))write(out,v);};
    s.menu_context=[this](std::uint32_t v){if(const auto out=argument(3))write(out,v);};
    s.side_effect=[this](std::uint32_t v){if(const auto out=argument(4))write(out,v);};
    result(DialogTemplates(memory_).expand_static(*row,slot,s),24);return true;
}
}
