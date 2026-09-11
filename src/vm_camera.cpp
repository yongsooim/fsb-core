#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/camera.hpp"
#include "fsb_core/actor_fields.hpp"

namespace fsb::core {
Yield Vm::camera_command(const Instruction& ins) {
    const auto slot = ins.operand(memory_, 0);
    const auto get = [&](unsigned n) { return ins.operand(memory_, n).get(memory_, object_); };
    const bool indirect = (slot.descriptor & operand_flag::indirect) != 0;
    if (!indirect && slot.payload == 1 && memory_.read(object_ + vm_offset::blocking_child)) {
        if (alive(memory_.read(object_ + vm_offset::blocking_child))) return Yield::Forced;
        memory_.write(object_ + vm_offset::blocking_child, 0); next(ins); return Yield::Continue;
    }
    Camera camera(memory_);
    if (memory_.read(globals::camera_focus_actor_index) != 0x2ff) camera.enter_tile_focus();
    auto duration = get(1);
    if (env_.input_pause || env_.dialog_busy) duration = 0;
    Handle child = 0;
    if (ins.subop == 0) {
        const auto actor_id = get(2);
        if (duration) child = camera.spawn_followup(duration, 0, 0, actor_id);
        else camera.focus_actor(lookup_actor(memory_, actor_id));
    } else {
        auto x = get(2), y = get(3);
        if (ins.subop == 2) {
            x += sequence_alu(alu::arithmetic_shift_right, std::uint32_t(signed32(memory_.read(globals::camera_focus_x_q16)) / 64), 16);
            y += sequence_alu(alu::arithmetic_shift_right, std::uint32_t(signed32(memory_.read(globals::camera_focus_y_q16)) / 48), 16);
        }
        if (duration) child = camera.spawn_followup(duration, x, y, 0xffffffff);
        else set_actor_tile_position(memory_, 0x85762c, signed32(x), signed32(y), -1);
    }
    if (child) {
        if (indirect) slot.set(memory_, object_, child);
        else if (slot.payload == 0) memory_.write(object_ + vm_offset::async_child, child);
        else if (slot.payload == 1) { memory_.write(object_ + vm_offset::blocking_child, child); return Yield::Continue; }
        else if (slot.payload != 0xffffffffu) throw Fault(ins.pc, "invalid camera result-slot selector");
    }
    next(ins); return Yield::Continue;
}
} // namespace fsb::core
