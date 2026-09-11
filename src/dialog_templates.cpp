#include "fsb_core/dialog_templates.hpp"
#include <algorithm>
#include <iterator>
namespace fsb::core {
namespace {
constexpr StaticDialogTemplate rows[]={
#include "dialog_template_rows.inc"
};
constexpr Address language_selector=0x7686bc;
constexpr unsigned no_menu_context=0xffffffffu, no_side_effect=0;
}
const StaticDialogTemplate* DialogTemplates::find_static(unsigned id) {
    const auto it=std::lower_bound(std::begin(rows),std::end(rows),id,[](const auto& row,unsigned id){return row.id<id;});
    return it!=std::end(rows)&&it->id==id?it:nullptr;
}
Address DialogTemplates::expand_static(const StaticDialogTemplate& row,std::uint32_t slot,const DialogTemplateServices& services) {
    // Even literal arms execute the common original prefix, including the
    // otherwise-unused progress read. Failure must occur before creation.
    services.observe_player();
    services.read_substate();
    const auto blob=row.localized&&signed32(memory_.read(language_selector))>=0?row.other:row.korean;
    const auto handle=services.create(row.id,blob,slot);
    if(handle) {
        services.facing(slot);
        services.menu_context(no_menu_context);
        services.side_effect(no_side_effect);
    }
    return handle;
}
}
