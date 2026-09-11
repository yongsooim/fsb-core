#include "fsb_core/object_pump.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
void ObjectPump::update_object(Address object, unsigned jobs) {
    const auto before = memory_.read(object + compact_offset::lifecycle);
    auto flags = memory_.read(object + compact_offset::flags);
    if (before != 0xffffffffu && (flags & compact_flag::skip_or_standby)) return;
    if (flags & unsigned(compact_flag::delayed_script_message)) {
        const auto event = memory_.read(globals::input_message), key = memory_.read(globals::input_key);
        if (event == input_message::script_message && (!key || key == memory_.read(object + compact_offset::argument))) {
            const auto range = memory_.read(globals::input_flags);
            if (range == 0xffffffffu) throw Fault(object, "script-message delay divisor overflow");
            memory_.write(object + compact_offset::delayed_message_countdown, range ? crt_rand(memory_) % (range + 1) + 1 : 1);
        }
        const auto remaining = memory_.read(object + compact_offset::delayed_message_countdown);
        if (remaining) {
            memory_.write(object + compact_offset::delayed_message_countdown, remaining - 1);
            if (remaining == 1) {
                const auto target = memory_.read(object + vm_offset::delayed_message_target);
                if (!target) { arena_.release(compact_handle(memory_, object)); return; }
                memory_.write(object + vm_offset::pc, target);
            }
        }
    }
    memory_.write(object + compact_offset::tick_count, memory_.read(object + compact_offset::tick_count) + 1);
    if (memory_.read(object + compact_offset::lifecycle) == 0xffffffffu && (memory_.read(object + compact_offset::flags) & 0x20000u))
        memory_.write(object + compact_offset::lifecycle, 0);
    else {
        const auto callback = memory_.read(object + compact_offset::callback);
        if (!dispatch_) throw Fault(callback, "missing core object callback dispatcher");
        dispatch_(callback, object, jobs);
    }
    flags = memory_.read(object + compact_offset::flags);
    if (!before || memory_.read(object + compact_offset::lifecycle) != 0) {
        const auto source = memory_.read(object + 0x188);
        if (!(flags & unsigned(compact_flag::draw_surface)) || !source) return;
        const auto anchor = memory_.read(object + 0x164);
        const auto x = anchor ? memory_.read(anchor) : memory_.read(object + 0x168);
        const auto y = anchor ? memory_.read(anchor + 4) : memory_.read(object + 0x16c);
        if (!draw_) throw Fault(object, "missing core surface renderer for live object");
        draw_({object, memory_.read(globals::render_target_surface), source,
            signed32(x + memory_.read(object + 0x180) - memory_.read(object + 0x178)),
            signed32(y + memory_.read(object + 0x184) - memory_.read(object + 0x17c)),
            signed32(memory_.read(object + 0x18c)), signed32(memory_.read(object + 0x190)),
            signed32(memory_.read(object + 0x194)), signed32(memory_.read(object + 0x198)), memory_.read(object + 0x19c)});
        return;
    }
    if (!memory_.read(globals::shutdown_drain_requested)) {
        if (flags & compact_flag::exclusive) {
            const auto successor = memory_.read(object + compact_offset::exclusive_wake_target);
            if (successor) {
                arena_.standby(object);
                if (successor != 0xffffffffu) arena_.wake(successor);
                memory_.write(object + compact_offset::exclusive_wake_target, 0);
            }
        }
        if (flags & unsigned(compact_flag::standby_on_finish)) {
            if (flags & unsigned(compact_flag::skip_constructor)) memory_.write(object + compact_offset::lifecycle, 1);
            arena_.standby(object); return;
        }
    }
    arena_.release(compact_handle(memory_, object));
}
void ObjectPump::update_group(unsigned group, unsigned jobs, const std::function<bool()>& stop) {
    auto object = memory_.read(Arena::head(group) + 4);
    unsigned iterations = 0;
    while (object != Arena::tail(group)) {
        if(stop&&stop())return; // Host-requested observation boundary; no object is modified or force-finished.
        if (!object || ++iterations > 100000) throw Fault(object, "invalid object-group traversal");
        update_object(object, jobs);
        // Read AFTER the callback: appended children can run in this traversal,
        // and release preserves the next link used here. No copied/sorted list.
        object = memory_.read(object + compact_offset::next);
    }
    memory_.write(globals::next_object_group, group + 1);
}
void ObjectPump::latch_frame_time(std::uint32_t now) {
    const auto previous = memory_.read(globals::frame_time_ms);
    memory_.write(globals::next_object_group, 0); memory_.write(globals::previous_frame_time_ms, previous);
    memory_.write(globals::frame_time_ms, now); memory_.write(globals::frame_delta_ms, now - previous);
    if (previous / 1000 != now / 1000) memory_.write(globals::second_boundary_crossed, 1);
    if (memory_.read(0x6d9d28) || memory_.read(0x6d9d2c)) memory_.write(0x6d4b38, 1);
    const auto event = memory_.read(globals::input_message);
    if (event == input_message::key_down || event == input_message::system_key_down) memory_.write(globals::key_pressed_this_frame, 1);
    else if (event == input_message::key_up || event == input_message::system_key_up) memory_.write(globals::key_released_this_frame, 1);
    else if (event == input_message::mouse_button) memory_.write(globals::mouse_message_this_frame, 1);
}
} // namespace fsb::core
