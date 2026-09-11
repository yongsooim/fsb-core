#include "fsb_core/vm.hpp"
#include "fsb_core/object_pump.hpp"
#include "fsb_core/raster.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace fsb::core;
namespace {
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    const auto size = file.tellg(); file.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size)); file.read(reinterpret_cast<char*>(data.data()), size); return data;
}
void bmp(const Image8& image, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write " + path.string());
    const auto u16 = [&](unsigned v) { file.put(static_cast<char>(v)); file.put(static_cast<char>(v >> 8)); };
    const auto u32 = [&](unsigned v) { for (unsigned i = 0; i < 4; ++i) file.put(static_cast<char>(v >> (i * 8))); };
    const auto pitch = (image.width + 3) & ~3u;
    file.write("BM", 2); u32(1078 + pitch * image.height); u32(0); u32(1078);
    u32(40); u32(image.width); u32(image.height); u16(1); u16(8); u32(0); u32(pitch * image.height);
    u32(2835); u32(2835); u32(256); u32(256);
    for (auto color : image.palette) u32((unsigned(color.r) << 16) | (unsigned(color.g) << 8) | color.b);
    for (unsigned row = image.height; row-- > 0;) {
        file.write(reinterpret_cast<const char*>(image.pixels.data() + row * image.width), image.width);
        for (auto x = image.width; x < pitch; ++x) file.put(0);
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) { std::cerr << "Usage: fsb_event0_organ ASSETS OUTPUT_DIR\n"; return 2; }
        const std::filesystem::path assets(argv[1]), output(argv[2]); std::filesystem::create_directories(output);
        auto memory = Memory::from_pe32(read(assets / "FLYINGSB.EXE")); Arena arena(memory); arena.initialize();
        memory.map(0x21000000, std::vector<std::uint8_t>(0x1ac, 0), true);
        memory.write(0x6d0405, 9); memory.write(0x5b35a0 + 9 * 0x44, 0x21000000);
        const auto parent = arena.clone_event(0x61f478, 0), parent_obj = *resolve_compact(memory, parent);
        VmEnvironment env; HsmQueue queue; Vm parent_vm(memory, queue, env, parent_obj); parent_vm.step();
        const auto sheet = Image8::pcx(read(assets / "ASE_FM/CSONA_E0.pcx"));
        std::ofstream trace(output / "organ-trace.tsv"); trace << "job\tms_at_16ms_jobs\tpc\tselector\tpose\twait_remaining\n";
        unsigned job = 0, count = 0; bool initial = false;
        env.trace = [&](Address obj, const Instruction& ins, bool after) {
            if (ins.opcode != 0x61) return;
            if (!after) initial = memory.read(obj + 0x40) == 0;
            else if (initial) {
                const auto pose = memory.read(0x21000138);
                trace << job << '\t' << job * 16 << "\t0x" << std::hex << ins.pc << std::dec << '\t'
                      << memory.read(0x21000134) << '\t' << pose << '\t' << memory.read(obj + 0x40) << '\n';
                if (pose >= 7) throw Fault(ins.pc, "organ pose outside original frame family");
                Image8 frame; frame.width = 140; frame.height = 145; frame.pixels.resize(140 * 145);
                frame.palette = sheet.palette; frame.palette[0] = {12, 16, 32}; // Diagnostic canvas background only.
                draw_sprite(frame, sheet, SpriteFrame::read(memory, 0x51fed8 + pose * 64), 70, 90);
                std::ostringstream name; name << "pose-" << std::setw(2) << std::setfill('0') << count++ << ".bmp";
                bmp(frame, output / name.str());
            }
        };
        ObjectPump pump(memory, arena, [&](Address callback, Address obj, unsigned jobs) {
            if (callback != 0x41a042) throw Fault(callback, "unimplemented callback in isolated organ harness");
            Vm vm(memory, queue, env, obj);
            if (vm.run_frame(jobs).finished) arena.release(compact_handle(memory, obj));
        });
        for (; job < 170; ++job) pump.update_group(1, 1);
        std::cout << "original_organ_pose_writes=" << count << " simulated_jobs=" << job << '\n';
        std::cout << "Isolated actor-child fixture, not a complete Event 0 run or visual-parity verdict.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
