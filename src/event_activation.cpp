#include "fsb_core/event_activation.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
namespace {
constexpr Address object_definitions=0x6d08cc;
constexpr std::uint32_t no_actor=0xffffffffu, no_event=0xffffffffu;
constexpr std::uint32_t event_object_tag=0x10000u, event_object_id_mask=0xffffu;
constexpr Address trigger_field=0xe4, dialog_actor_registered=0x19c;
template<class Service>
const Service& required(const Service& service,Address entry) {
    if(!service)throw Fault(entry,"event activation service is not connected");
    return service;
}
}
Handle EventActivation::spawn_object(unsigned id,Handle attached_actor,std::uint32_t trigger) {
    const auto definition=memory_.read(object_definitions+id*4u);
    Handle handle=0;
    if(definition) {
        handle=required(services_.clone_definition,0x41a00b)(definition,1);
        const auto player=required(services_.player_actor,0x457e56)();
        // The original locks the player even when cloning returned zero.
        required(services_.set_actor_state,0x4300c7)(player,0);
    } else required(services_.missing_definition,0x401a02)(id);
    if(handle) {
        const auto object=required(services_.runtime_object,0x4026c1)(handle);
        memory_.write(object+vm_offset::actor_id,attached_actor);
        if(attached_actor!=no_actor)
            memory_.write(object+vm_offset::actor_object,required(services_.actor_object,0x42feb9)(attached_actor));
        memory_.write(object+trigger_field,trigger);
        memory_.scene_state().current_event=(id&event_object_id_mask)|event_object_tag;
    }
    return handle;
}
Handle EventActivation::spawn_player_dialog(Address markup) {
    const auto player=required(services_.player_actor,0x457e56)();
    const auto actor=required(services_.player_actor_object,0x457ec9)(player);
    required(services_.set_object_state,0x430059)(actor,0);
    const auto handle=required(services_.inline_dialog,0x4139b6)(player,markup,-1);
    // No early success/failure shortcut: the original resolves even a zero handle.
    const auto object=required(services_.runtime_object,0x4026c1)(handle);
    memory_.write(object+dialog_actor_registered,1);
    return handle;
}
bool EventActivation::resume_pending() {
    const auto event=memory_.scene_state().resume_event;
    const bool started=event!=no_event && required(services_.request_event,0x412017)(event,no_event)!=0;
    if(started)required(services_.alternate_visuals,0x44e085)();
    // Consume after callbacks, including a failed activation. Exceptions retain
    // the point of failure instead of pretending this final write occurred.
    memory_.scene_state().resume_event=no_event;
    return started;
}
} // namespace fsb::core
