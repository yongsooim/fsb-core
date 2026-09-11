#include "fsb_core/vm.hpp"
#include "fsb_core/actor_fields.hpp"
#include "fsb_core/object_pump.hpp"
#include "fsb_core/raster.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/camera.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <filesystem>

using namespace fsb::core;
namespace {
unsigned checks = 0, failed = 0;
void check(bool ok, const char* name) { ++checks; if (!ok) { ++failed; std::cerr << "FAIL " << name << '\n'; } }
template<class F> void fault(F&& fn, const char* name) {
    bool caught = false; try { fn(); } catch (const Fault&) { caught = true; } check(caught, name);
}
std::vector<std::uint8_t> read(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("EXE not found");
    const auto size = file.tellg(); file.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size)); file.read(reinterpret_cast<char*>(bytes.data()), size); return bytes;
}
struct Script {
    Address base;
    std::vector<std::uint8_t> bytes;
    Address at() const { return base + static_cast<Address>(bytes.size()); }
    Address op(unsigned op, unsigned sub, std::initializer_list<Operand> operands = {}) {
        const auto pc = at(); const unsigned length = 4 + 5 * operands.size();
        bytes.insert(bytes.end(), {static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(sub), static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(length >> 8)});
        for (auto operand : operands) { bytes.push_back(operand.descriptor); for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(operand.payload >> (8 * i))); }
        return pc;
    }
};
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        auto memory = Memory::from_pe32(read(argv[1])); Arena arena(memory); arena.initialize();
        HsmQueue queue; VmEnvironment env;
        const auto root = arena.clone_event(0x61f0ba, 0); const auto object = *resolve_compact(memory, root);
        Vm vm(memory, queue, env, object);

        memory.write(0x57fd1c,2);
        for(unsigned phase=0;phase<2;++phase){
            memory.write(0x768688+2*4,phase);memory.write(object+0x30,0x620176);vm.step();
            check(memory.read(object+0x44)==phase,"Event2 reads its original activation count");
            const auto expected=memory.read(0x62017f+14+phase*4);vm.step();
            check(vm.pc()==expected,"actual Event2 branch table selects the counted scene phase");
        }

        check(sequence_alu(2, 0xffffffffu, 1) == 1 && sequence_alu(4, 0xffffffffu, 1) == 1, "ALU signed compare aliases");
        check(sequence_alu(8, 0xffffffffu, 2) == 1 && sequence_alu(10, 0x80000000u, 2) == 0, "ALU wraps 32-bit add/multiply");
        check(sequence_alu(11, 0xfffffff9u, 2) == 0xfffffffdu && sequence_alu(12, 0xfffffff9u, 2) == 0xffffffffu, "signed divide and remainder truncate toward zero");
        check(sequence_alu(18, 0x80000000u, 33) == 0xc0000000u, "arithmetic right shift masks count");
        fault([&] { sequence_alu(11, 1, 0); }, "divide-by-zero is not silently zero");
        fault([&] { sequence_alu(11, 0x80000000u, 0xffffffffu); }, "x86 divide overflow is a fault");

        Script loops{0x10000000, {}};
        loops.op(0x2c, 0, {{0x44, 0xe8}, {0, 0}});
        loops.op(0x0b, 0, {{0, 3}});
        loops.op(0x28, 2, {{0x44, 0xe8}});
        loops.op(0x0d, 0, {{0x44, 0xe8}, {0, 2}, {0, 2}});
        loops.op(0x28, 2, {{0x44, 0xe8}});
        loops.op(0x0e, 0);
        loops.op(0x0c, 0);
        loops.op(0x10, 1, {{0x44, 0xe8}, {0, 0}, {0, 4}});
        loops.op(0x2c, 0, {{0x44, 0xec}, {0, 77}});
        loops.op(0x11, 3);
        loops.op(0x2c, 0, {{0x44, 0xec}, {0, 99}});
        loops.op(0x12, 0); loops.op(0, 1);
        memory.map(loops.base, loops.bytes, false); memory.write(object + 0x30, loops.base);
        check(vm.run_frame(1).yield == Yield::Forced && memory.read(object + 0x20) == 0, "nested loop/conditional program terminates normally");
        check(memory.read(object + 0xe8) == 4 && memory.read(object + 0xec) == 77, "counted repeat and conditional loop preserve arithmetic outcome");
        check(memory.read(object + 0xa8) == 0, "all control frames pop at completion");

        Script branch{0x10001000, {}};
        branch.op(0x10, 0, {{0, 0}});
        branch.op(0x10, 0, {{0, 1}}); branch.op(0x2c, 0, {{0x44, 0xec}, {0, 10}}); branch.op(0x12, 0);
        branch.op(0x11, 0, {{0, 0}}); branch.op(0x2c, 0, {{0x44, 0xec}, {0, 20}});
        branch.op(0x11, 3); branch.op(0x2c, 0, {{0x44, 0xec}, {0, 30}}); branch.op(0x12, 0); branch.op(0, 0);
        memory.map(branch.base, branch.bytes, false); memory.write(object + 0x30, branch.base); memory.write(object + 0x20, 1);
        vm.run_frame(1); check(memory.read(object + 0xec) == 30, "false arm skips nested if and selects else");

        Script called{0x10002000, {}};
        called.op(0x28, 2, {{0x44, 0xec}}); called.op(0, 1);
        Script caller{0x10003000, {}};
        caller.op(0x07, 0, {{0x80, called.base}}); caller.op(0x28, 2, {{0x44, 0xec}}); caller.op(0, 1);
        memory.map(called.base, called.bytes, false); memory.map(caller.base, caller.bytes, false);
        memory.write(object + 0x30, caller.base); memory.write(object + 0x20, 1);
        vm.run_frame(1); check(memory.read(object + 0xec) == 32 && memory.read(object + 0xa8) == 0, "call returns to exact saved PC");

        // Raw Event 0 flame child: 19/00 modulus 0x34, then 29/00 adds 8.
        memory.write(object + 0x30, 0x61fe8b); memory.write(object + 0x20, 1); memory.write(0x5aa408, 1);
        fault([&] { vm.run_frame(1); }, "actual child stops at unimplemented reaction opcode89");
        check(vm.pc() == 0x61fea6, "actual child control and arithmetic advance to reaction instruction");
        const std::uint32_t vm_random = 0x41c64e6du + 0x3039u;
        check(memory.read(0x5aa408) == vm_random && memory.read(object + 0x44) == vm_random % 0x34 + 8,
              "script RNG uses full-width ANSI-style LCG, not MSVC rand output");

        // Actor context is controlled test input, not a claim of a completed game bootstrap.
        memory.map(0x21000000, std::vector<std::uint8_t>(0x1ac, 0), true);
        memory.write(0x5d2258, 1); memory.write(0x5b35a0 + 0x44, 0x21000000);
        memory.write(0x21000004, 0x10000); set_actor_tile_position(memory, 0x21000000, 6, 37, 1);
        memory.write(object + 0x30, 0x61f0ba); memory.write(object + 0xa8, 0); memory.write(object + 0x20, 1);
        memory.write(0x6db57c, 0xffffffff); memory.write(0x768478, 0);
        check(vm.step() == Yield::Continue && vm.pc() == 0x61f0be, "Event 0 92/00 enters event mode");
        check(memory.read(0x4a5054) == 3 && memory.read(0x57fd24) == 0xffffffff && memory.read(0x768a98) == 0, "event mode updates original clock/pending globals");
        check((memory.read(0x21000004) & 0x10000) == 0 && memory.read(0x21000128) == 6, "event enter locks player and refreshes tile cache");
        vm.step(); check(vm.pc() == 0x61f0c7 && memory.read(0x768478) == 10 && memory.read(0x6db57c) == 0, "E2/04 selector1000 arms dialog delay");
        fault([&] { vm.step(); }, "preamble requires an attached palette subsystem");
        Palette palette(memory, {}); env.palette = &palette;
        memory.write(0x6d9ebc, 8);
        const auto original_black = memory.bytes(0x4a2000, 1024);
        vm.step(); check(vm.pc() == 0x61f0cb, "actual root ED/04 uploads the black palette");
        check(memory.bytes(0x4a2000, 1024) == original_black, "copy-first palette upload leaves original source unchanged");
        check(palette.raw_entries()[0] == 0x02000000 && palette.raw_entries()[255] == 0x020000ff,
              "8bit upload keeps original explicit-index pins");
        check(palette.colors()[0].r == 0 && palette.colors()[255].r == 255 && palette.colors()[255].g == 255,
              "explicit palette index255 renders white, not red");
        palette.capture(0x804660);
        check(memory.read(0x804660) == 0 && memory.read(0x804a5c) == 0x00ffffff, "palette capture normalizes fixed endpoints");
        fault([&] { vm.step(); }, "root next requires real BGM startup");

        // Actual Event0 EE/10 requests 1200ms => 72 interpolation intervals.
        // The original worker first emits step0 and finishes at step72 (73 jobs).
        memory.write(0x804664, 0x05030201); memory.write(object + 0x30, 0x61f2b8);
        vm.step(); check(memory.read(0x6d6b00) == 72 && memory.read(0x4a57c8) == 0 && vm.pc() == 0x61f2c1,
                         "actual Event0 duration fade arms without advancing its worker");
        check(vm.step() == Yield::Forced && vm.pc() == 0x61f2c1, "EC parks on active palette fade");
        palette.advance(1); check(palette.raw_entries()[1] == 0x05000000 && palette.busy(), "linear fade step0 emits the captured start");
        palette.advance(71); check(palette.busy() && memory.read(0x4a57c8) == 72 && (palette.raw_entries()[1] & 255) == 0,
                                  "duration72 still busy after72 jobs with integer truncation");
        palette.advance(1); check(!palette.busy() && palette.raw_entries()[1] == 0x05030201, "linear endpoint emits exact target on73rd job");
        check(vm.step() == Yield::Continue && vm.pc() == 0x61f2c5, "EC releases from real fade completion");

        // Actual white flash -> EE/11 clamp7. Clamp completion is a no-change
        // worker call, not the call that first puts RGB at its destination.
        memory.write(object + 0x30, 0x61f2ce); vm.step();
        check(palette.raw_entries()[1] == 0x05ffffff && memory.read(0x6db17c) == 0x05ffffff,
              "ED/05 uploads white and mutates staging flags in place");
        memory.write(object + 0x30, 0x61f2db); vm.step();
        check(memory.read(0x6dc5fc) == 7 && memory.read(0x6d6b00) == 0, "actual clamp fade uses7 per-channel, not duration7");
        palette.advance(37); check(palette.busy() && palette.raw_entries()[1] == 0x05030201, "clamp fade reaches color but remains busy");
        palette.advance(1); check(!palette.busy(), "clamp fade completes only on the next unchanged step");
        memory.write(0x804664, 0x057f80ff); palette.scale(0x6db178, 0x804660, 200); palette.add_delta(0x6db178, 0xfffffff6);
        check(memory.read(0x6db17c) == 0x05f4f5f5, "scale then delta clamps between operations and keeps flags");

        // Busy input collapses to the original fastest finite worker path.
        env.input_pause = true; memory.write(object + 0x30, 0x61f2b8); vm.step();
        check(memory.read(0x6d6b00) == 1 && palette.busy(), "blocked linear fade still requires worker completion");
        memory.write(object + 0x30, 0x61f2db); vm.step();
        check(memory.read(0x6dc5fc) == 256, "blocked clamp fade selects256"); env.input_pause = false;
        palette.begin(0x804660, 0, 0, 0);
        fault([&] { palette.advance(1); }, "zero-duration linear divide is not silently completed");
        memory.write(0x4a57c8, 0xffffffff);

        auto pump_memory = Memory::from_pe32(read(argv[1])); Arena pool(pump_memory); pool.initialize();
        std::vector<Address> seen;
        Handle spawned = 0;
        const auto a = pool.allocate_after(Arena::head(4), 1, 0x10000, 0);
        const auto b = pool.allocate_after(pump_memory.read(Arena::tail(4)), 2, 0x10000, 0);
        const auto a_obj = *resolve_compact(pump_memory, a), b_obj = *resolve_compact(pump_memory, b);
        ObjectPump pump(pump_memory, pool, [&](Address callback, Address node, unsigned jobs) {
            seen.push_back(callback); check(jobs == 3, "object receives original job budget");
            if (callback == 1) spawned = pool.allocate_after(pump_memory.read(Arena::tail(4)), 3, 0x10000, 0);
            pump_memory.write(node + 0x20, 0);
        });
        pump.update_group(4, 3);
        check(seen == std::vector<Address>{1, 2, 3}, "same-group append executes this frame after existing objects");
        check(pool.members(4).empty() && !resolve_compact(pump_memory, a) && !resolve_compact(pump_memory, b), "nonzero-to-zero callbacks release each object once");
        check(pump_memory.read(a_obj + 4) == b_obj, "self-release retains traversal link");
        check(pump_memory.read(0x6d9e7c) == 5, "group cursor advances after traversal");

        const auto skip = pool.allocate_after(Arena::head(1), 4, 0x30003, 0); const auto skip_obj = *resolve_compact(pump_memory, skip);
        const auto before_seen = seen.size(); pump.update_group(1, 3);
        check(seen.size() == before_seen && pump_memory.read(skip_obj + 0x24) == 0, "skip/standby suppresses tick counter and callback");
        pump_memory.write(skip_obj + 0x20, 0xffffffff); pump.update_group(1, 3);
        check(seen.size() == before_seen && !resolve_compact(pump_memory, skip), "forced release bypasses standby and skips callback when flagged");

        const auto retained = pool.allocate_after(Arena::head(1), 9, 0x90000, 0);
        pump.update_group(1, 3); const auto retained_obj = *resolve_compact(pump_memory, retained);
        check(pump_memory.read(retained_obj + 0x20) == 1 && (pump_memory.read(retained_obj + 0x18) & 2), "standby-on-finish re-arms object without freeing");

        pump_memory.write(0x6da2d8, 990); pump_memory.write(0x6da2dc, 0x104); pump.latch_frame_time(1005);
        check(pump_memory.read(0x6d9e74) == 15 && pump_memory.read(0x6d6b04) == 1 && pump_memory.read(0x6e1444) == 1, "frame latches clock delta/second/key flags without OS calls");
        auto music_memory = Memory::from_pe32(read(argv[1])); Arena music_pool(music_memory); music_pool.initialize();
        music_memory.map(0x21000000, std::vector<std::uint8_t>(0x1ac, 0), true);
        music_memory.write(0x6d0405, 9); music_memory.write(0x5b35a0 + 9 * 0x44, 0x21000000);
        const auto parent = music_pool.clone_event(0x61f478, 0); const auto parent_obj = *resolve_compact(music_memory, parent);
        VmEnvironment music_env; HsmQueue music_queue; Vm parent_vm(music_memory, music_queue, music_env, parent_obj);
        parent_vm.step();
        const auto organ = music_memory.read(0x6d0595), organ_obj = *resolve_compact(music_memory, organ);
        check(music_pool.members(1) == std::vector<Handle>{organ} && music_pool.members(4).empty(), "actual 4f/00 actor child is in early group1, not group4");
        check(music_memory.read(organ_obj + 0xf8) == 9 && music_memory.read(organ_obj + 0xfc) == 0x21000000, "actor binding preserves ID and resolved object separately");
        std::vector<unsigned> poses, starts; unsigned frame = 0; bool initializing_pose = false;
        music_env.trace = [&](Address obj, const Instruction& ins, bool after) {
            if (ins.opcode != 0x61) return;
            if (!after) initializing_pose = music_memory.read(obj + 0x40) == 0;
            else if (initializing_pose) { poses.push_back(music_memory.read(0x21000138)); starts.push_back(frame); }
        };
        ObjectPump music_pump(music_memory, music_pool, [&](Address callback, Address obj, unsigned jobs) {
            if (callback != 0x41a042) throw Fault(callback, "unknown callback in organ fixture");
            Vm child_vm(music_memory, music_queue, music_env, obj);
            if (child_vm.run_frame(jobs).finished) music_pool.release(compact_handle(music_memory, obj));
        });
        for (; frame < 170; ++frame) music_pump.update_group(1, 1);
        check(poses == std::vector<unsigned>{0,1,2,3,4,5,6,0,1,3,2,3,4,3,4,5,6}, "actual organ bytecode controls all 17 pose writes");
        check(starts == std::vector<unsigned>{0,10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160}, "actual timed selectors hold 10 jobs, last holds 9 plus yield");
        music_pump.update_group(1, 1);
        check(poses.size() == 18 && poses.back() == 0, "original loop restarts after 170 jobs");
        music_memory.write(parent_obj + 0x30, 0x61f815); parent_vm.step();
        check(music_memory.read(organ_obj + 0x20) == 0xffffffff && resolve_compact(music_memory, organ), "original release opcode schedules teardown rather than freeing immediately");
        const auto writes_before_release = poses.size(); music_pump.update_group(1, 1);
        check(!resolve_compact(music_memory, organ) && poses.size() == writes_before_release, "teardown removes child without another pose callback");
        const auto sheet_path = std::filesystem::path(argv[1]).parent_path() / "ASE_FM/CSONA_E0.pcx";
        const auto sheet_bytes = read(sheet_path.string().c_str()); const auto sheet = Image8::pcx(sheet_bytes);
        check(sheet.width == 448 && sheet.height == 448, "original CSONA_E0 PCX dimensions");
        const auto first_sprite = SpriteFrame::read(music_memory, 0x51fed8);
        check(first_sprite.bank == 26 && first_sprite.x == 5 && first_sprite.y == 7 && first_sprite.width == 101 && first_sprite.height == 115 && first_sprite.origin_x == 48 && first_sprite.origin_y == 77,
              "sprite crop and origin come from original EXE records");
        Image8 canvas; canvas.width = 140; canvas.height = 145; canvas.pixels.resize(140 * 145);
        draw_sprite(canvas, sheet, first_sprite, 70, 90);
        unsigned colored = 0; for (auto p : canvas.pixels) colored += p != 0;
        check(colored > 2000, "actual organ sprite renders into portable indexed surface");
        auto truncated_pcx = sheet_bytes; truncated_pcx.erase(truncated_pcx.begin() + 128, truncated_pcx.end() - 769);
        fault([&] { Image8::pcx(truncated_pcx); }, "truncated PCX cannot be zero-padded into a fake success");

        auto map_memory = Memory::from_pe32(read(argv[1])); MapAssets map_assets;
        const auto names = Map::asset_names(map_memory, 469);
        check(names[0] == "TCL0___P.pcx" && names[4] == "TCL0___P.map", "map asset identities come from original catalog");
        const auto assets_root = std::filesystem::path(argv[1]).parent_path();
        for (unsigned i = 0; i < 6; ++i) map_assets.files[i] = read((assets_root / "MAPSET" / names[i]).string().c_str());
        Map map(map_memory); map.load_assets(469, map_assets);
        check(map_memory.read(0x7760c4) == 12 && map_memory.read(0x7760c8) == 42 && map_memory.read(0x800dbc) == 2,
              "original map469 is12x42 with2 active background layers");
        check(map_memory.read(0x7ab9a0) == 5 && map_memory.read(0x7ab9a4) == 40 && map_memory.read(0x7ab9a8) == 1,
              "MFO pcpos0 retains original5,40,1 placement");
        check(map_memory.read(0x7e0cb4) == 0x180000 && map_memory.read(0x7e0cc4) == 0x5000 && map_memory.read(0x7e0cb8) == 0,
              "MFO layer0 retains24-tile Y anchor,5/16 scale,and bounded policy");
        check(map_memory.read(0x7d8ca0) == 0xffffffff && map_memory.read(0x7d8ca4) == 0 && map_memory.read(0x7d8ca0 + 11 * 4) == 1,
              "MAT separates skipped,opaque,and source-keyed tile materials");
        map_memory.write(0x6e12b0, 640); map_memory.write(0x6e1440, 360);
        map_memory.write(0x6d9d30, 0); map_memory.write(0x6d9d34, 60);
        map_memory.write(0x6da548, 0); map_memory.write(0x6da54c, 60); map_memory.write(0x6da550, 640); map_memory.write(0x6da554, 420);
        map_memory.write(0x7873c0, 384); map_memory.write(0x7873c4, 241); map.update_scroll();
        check(map_memory.read(0x7e0ca4) == 80433152 && map_memory.read(0x7e0cd8) == 241 * 65536,
              "parallax computes1227.3125px from241px camera plus24-tile anchor");
        auto tile_commands = map.background_commands(0);
        check(tile_commands.size() == 90 && tile_commands.front().x == 0 && tile_commands.front().y == 21,
              "source-shaped visible tile scan retains partial row above clip");
        Image8 map_frame; map_frame.width = 640; map_frame.height = 480; map_frame.pixels.assign(640 * 480, 250);
        map.draw_background(map_frame, tile_commands);
        check(map_frame.pixels[59 * 640] == 250 && map_frame.pixels[420 * 640] == 250 && map_frame.pixels[240 * 640] != 250,
              "software clip preserves cinematic top and bottom borders");
        map_memory.write(0x7e0ca0, 0); map_memory.write(0x7e0ca4, 0);
        tile_commands = map.background_commands(0);
        check(!tile_commands.empty() && tile_commands.front().x == 320 && tile_commands.front().y == 240,
              "negative scroll uses floor tile division and bounded clipping");
        auto broken_map = map_assets; broken_map.files[4].pop_back();
        fault([&] { map.load_assets(469, broken_map); }, "truncated original MAP cannot be accepted");

        auto camera_memory = Memory::from_pe32(read(argv[1])); Arena camera_pool(camera_memory); camera_pool.initialize();
        camera_memory.write(0x787478, 0x2ff); Camera camera(camera_memory);
        HsmQueue camera_queue; VmEnvironment camera_env;
        const auto camera_parent = camera_pool.clone_event(0x61f102, 0), camera_parent_obj = *resolve_compact(camera_memory, camera_parent);
        Vm camera_vm(camera_memory, camera_queue, camera_env, camera_parent_obj); camera_vm.step();
        check(camera_memory.read(0x857634) == 480 * 65536 && camera_memory.read(0x857638) == 264 * 65536,
              "actual initial B1 uses half-tile centers7.5,5.5");
        camera_memory.write(camera_parent_obj + 0x30, 0x61f2f1); camera_vm.step();
        const auto camera_child = camera_memory.read(0x6d04c9), camera_child_obj = *resolve_compact(camera_memory, camera_child);
        check(camera_pool.members(1) == std::vector<Handle>{camera_child} && camera_memory.read(camera_child_obj + 0x168) == 928 * 65536 && camera_memory.read(camera_child_obj + 0x16c) == 1560 * 65536,
              "actual relative B1 adds7,27 tiles to current camera and appends in early group1");
        ObjectPump camera_pump(camera_memory, camera_pool, [&](Address callback, Address obj, unsigned jobs) {
            if (callback != 0x430fd0) throw Fault(callback, "unexpected camera test callback");
            camera.tick_followup(obj, jobs);
        });
        for (unsigned job = 0; job < 240; ++job) camera_pump.update_group(1, 1);
        check(camera_memory.read(camera_child_obj + 0x20) == 1 && camera_memory.read(0x857638) < 1560 * 65536,
              "240-frame tween is still short of target after240 jobs because velocity truncates");
        camera_pump.update_group(1, 1);
        check(camera_memory.read(camera_child_obj + 0x20) == 0xffffffff && camera_memory.read(0x857638) == 1560 * 65536,
              "camera reaches target on241st job and schedules deferred release");
        camera_pump.update_group(1, 1);
        check(!resolve_compact(camera_memory, camera_child), "camera wait handle expires on following group update");
        camera_memory.write(0x7e0ca8, 768 * 65536); camera_memory.write(0x7e0cac, 2016 * 65536);
        camera_memory.write(0x6e12b0, 640); camera_memory.write(0x6e1440, 480);
        camera.clamp_target(929, 1537, true);
        check(camera_memory.read(0x7873c0) == 384 && camera_memory.read(0x7873c4) == 1537,
              "camera interior exactly640px centers X despite far-right focus");
        camera.clamp_target(384, 1728, true);
        check(camera_memory.read(0x7873c4) == 1727, "camera bottom equality retains original one-pixel overshoot adjustment");
        memory.write(0x6d9ebc,0x21);
        const auto readonly_palette=memory.bytes(0x4a21c0,144*4);
        memory.write(object+0x30,0x6202a8);vm.step();
        check(vm.pc()==0x6202c0&&memory.bytes(0x4a21c0,144*4)==readonly_palette,
              "Event2 fullscreen palette upload reads the original const span without a write");
        check(palette.raw_entries()[112]==memory.read(0x4a21c0)&&palette.raw_entries()[255]==memory.read(0x4a21c0+143*4),
              "fullscreen palette upload preserves all source entries including255");
        MapAssets room_assets;const auto room_names=Map::asset_names(map_memory,470);
        for(unsigned i=0;i<6;++i)room_assets.files[i]=read((std::filesystem::path(argv[1]).parent_path()/"MAPSET"/room_names[i]).string().c_str());
        map_memory.write(0x7cbca0+4096*4,0xdeadbeef);map.load_assets(470,room_assets);
        check(map_memory.read(0x800dbc)==1&&map_memory.read(0x800dc0)==2&&map_memory.read(0x7cbca0+4096*4)==0,
              "Miro room loads its empty second header and clears auxiliary attributes");
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto& region : memory.snapshot_regions()) for (auto byte : region.bytes) hash = (hash ^ byte) * 1099511628211ull;
        for (const auto& region : pump_memory.snapshot_regions()) for (auto byte : region.bytes) hash = (hash ^ byte) * 1099511628211ull;
        for (const auto& region : music_memory.snapshot_regions()) for (auto byte : region.bytes) hash = (hash ^ byte) * 1099511628211ull;
        for (const auto& region : map_memory.snapshot_regions()) for (auto byte : region.bytes) hash = (hash ^ byte) * 1099511628211ull;
        for (const auto& region : camera_memory.snapshot_regions()) for (auto byte : region.bytes) hash = (hash ^ byte) * 1099511628211ull;
        for (auto pixel : map_frame.pixels) hash = (hash ^ pixel) * 1099511628211ull;
        for (auto entry : palette.raw_entries()) for (unsigned i = 0; i < 4; ++i)
            hash = (hash ^ std::uint8_t(entry >> (i * 8))) * 1099511628211ull;
        for (auto pixel : canvas.pixels) hash = (hash ^ pixel) * 1099511628211ull;
        std::cout << "flow_state_fnv1a=" << std::hex << hash << std::dec << '\n';
        std::cout << "flow_checks=" << checks << " failures=" << failed << '\n';
        return failed ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
