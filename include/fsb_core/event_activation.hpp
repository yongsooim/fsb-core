#pragma once
#include "primitives.hpp"
#include <functional>

namespace fsb::core {
struct EventActivationServices {
    std::function<Handle(Address,unsigned)> clone_definition;
    std::function<Handle()> player_actor;
    std::function<void(Handle,unsigned)> set_actor_state;
    std::function<Address(Handle)> actor_object;
    std::function<Address(Handle)> player_actor_object;
    std::function<void(Address,unsigned)> set_object_state;
    std::function<Address(Handle)> runtime_object;
    std::function<Handle(Handle,Address,std::int32_t)> inline_dialog;
    std::function<void(unsigned)> missing_definition;
    std::function<Handle(unsigned,std::uint32_t)> request_event;
    std::function<void()> alternate_visuals;
};

class EventActivation {
public:
    EventActivation(Memory& memory,const EventActivationServices& services)
        :memory_(memory),services_(services){}
    Handle spawn_object(unsigned id,Handle attached_actor,std::uint32_t trigger);
    Handle spawn_player_dialog(Address markup);
    bool resume_pending();
private:
    Memory& memory_;
    const EventActivationServices& services_;
};
} // namespace fsb::core
