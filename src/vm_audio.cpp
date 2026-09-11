#include "fsb_core/vm.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/audio.hpp"

namespace fsb::core {
Yield Vm::audio_command(const Instruction& ins) {
    if (!env_.audio) throw Fault(ins.pc, "audio subsystem is not attached");
    auto& audio = *env_.audio;
    const auto get = [&](unsigned i) { return ins.operand(memory_, i).get(memory_, object_); };
    const auto immediate = [&](std::uint32_t duration) { return !duration || env_.input_pause || env_.dialog_busy; };
    if(ins.opcode==0xc0){
        switch(ins.subop){
        case 0:memory_.write(object_+0x44,memory_.read(0x5c4f58+memory_.read(globals::current_map_id)*68));break;
        case 3:memory_.write(object_+0x44,memory_.read(0x76e918));break;
        case 4:audio.subvolume(get(0));break;
        default:return original_command(ins); // Original C0/1–2 now use the connected loop/seek ABI.
        }
    }else if (ins.opcode == opcode::sound_cue) {
        const auto id = get(0);
        if (ins.subop == 0) audio.play_cue(id);
        else if (ins.subop == 1) audio.stop_cue(id);
        else if (!audio.cue_finished(id)) return Yield::Forced;
    } else if (ins.opcode == opcode::bgm) {
        switch (ins.subop) {
        case 0: audio.cancel_fade(); audio.stop_bgm(); audio.load_bgm(get(0)); audio.subvolume(100); break;
        case 1: audio.play_bgm(); break;
        case 2: audio.cancel_fade(); audio.apply_bgm(get(0)); break;
        case 3: audio.stop_bgm(); break;
        case 4: {
            audio.cancel_fade(); audio.stop_bgm(); audio.load_bgm(get(0)); audio.subvolume(0);
            const auto duration = get(1);
            if (immediate(duration)) audio.subvolume(100); else audio.fade(duration, 0, 100, true);
            audio.play_bgm(); break;
        }
        }
    } else {
        switch (ins.subop) {
        case 0: {
            const auto target = get(0), duration = get(1);
            if (immediate(duration)) audio.subvolume(target); else audio.fade(duration, memory_.read(0x76e918), target, true);
            break;
        }
        case 1: {
            const auto duration = get(0);
            if (immediate(duration)) { audio.subvolume(0); audio.stop_bgm(); }
            else audio.fade(duration, memory_.read(0x76e918), 0, false);
            break;
        }
        case 2: audio.cancel_fade(); break;
        case 3: if (memory_.read(0x768a9c)) return Yield::Forced; break;
        case 4:
            if (env_.input_pause || env_.dialog_busy) audio.stop_bgm();
            else if (audio.bgm_playing()) return Yield::Forced;
            break;
        }
    }
    next(ins); return Yield::Continue;
}
} // namespace fsb::core
