#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/viewport.hpp"

namespace fsb::core {
Yield Vm::palette_command(const Instruction& ins) {
    if (!env_.palette) throw Fault(ins.pc, "palette subsystem is not attached");
    auto& palette = *env_.palette;
    constexpr Address shared = globals::palette_target, scratch = 0x6db178, black = tables::black_palette, white = 0x4a53c8;
    const auto get = [&](unsigned i) { return ins.operand(memory_, i).get(memory_, object_); };
    if (ins.opcode == opcode::wait_transition) {
        const auto phase = memory_.read(globals::transition_wait_phase);
        if(ins.subop==0&&phase>1)throw Fault(ins.pc,"invalid transition wait phase");
        const bool rect_wait=ins.subop==2||(ins.subop==0&&phase==0);
        if(rect_wait?memory_.read(globals::rect_effect_busy)!=0:palette.busy())return Yield::Forced;
        if(phase==0&&!memory_.read(globals::frame_idle_callback)){
            if(!env_.viewport)throw Fault(ins.pc,"rectangle completion needs viewport service");
            env_.viewport->blit_borders();
        }
    } else if (ins.opcode == opcode::palette_buffer) {
        switch (ins.subop) {
        case 0: palette.upload(shared, 0, 256, true); break;
        case 1: palette.copy(scratch, shared); break;
        case 2: palette.copy(shared, scratch); break;
        case 3: palette.capture(shared); break;
        case 4: palette.upload(black, 0, 256, true); break;
        case 5: palette.copy(scratch, white); palette.capture(scratch, 0, 1); palette.upload(scratch); break;
        case 8: palette.scale(scratch, shared, get(0)); palette.add_delta(scratch, get(1)); break;
        case 10: palette.capture(shared + 4, 1, 15); palette.capture(shared + 0x380, 0xe0, 0x18); break;
        }
    } else {
        if (ins.subop == 0x20 || ins.subop == 0x21) {
            if (!memory_.read(object_ + vm_offset::same_pc_count)) {
                if (ins.subop == 0x20) {
                    palette.copy(scratch, white); palette.capture(scratch, 0, 1);
                    palette.begin(scratch, 0, 40, 0);
                } else palette.begin(shared, 0, 0, 12);
                return Yield::Continue; // Original first pass keeps PC but does not set yield.
            }
            if (palette.busy()) return Yield::Forced;
        } else {
            std::uint32_t clamp = 0, duration = 0;
            if ((ins.subop >= 3 && ins.subop <= 5) || ins.subop == 0x11) clamp = get(0);
            else {
                const auto operand = get(0);
                if (operand) {
                    duration = signed32(operand) < 0 ? 0u - operand : std::uint32_t(signed32(operand * 3) / 50);
                    if (signed32(duration) < 2) duration = 1;
                }
            }
            if (env_.input_pause || env_.dialog_busy) { if (clamp) clamp = 256; else if (duration) duration = 1; }
            auto target = shared;
            if (ins.subop == 0 || ins.subop == 3) target = black;
            if (ins.subop == 1 || ins.subop == 4) target = white;
            if (ins.subop == 2 || ins.subop == 5) {
                palette.copy(scratch, white); palette.capture(scratch, 0, 1); target = scratch;
            }
            palette.begin(target, 0, clamp, duration); memory_.write(globals::transition_wait_phase, 1);
        }
    }
    next(ins); return Yield::Continue;
}
} // namespace fsb::core
