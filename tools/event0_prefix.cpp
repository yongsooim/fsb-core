#include "fsb_core/actors.hpp"
#include "fsb_core/audio.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/viewport.hpp"
#include "fsb_core/vm.hpp"
#include "fsb_core/arena.hpp"
#include "fsb_core/dialogue.hpp"
#include "lab_io.hpp"
#include <iomanip>
#include <iostream>

using namespace fsb::core;
int main(int argc, char** argv) {
    try {
        if (argc != 3) { std::cerr << "Usage: fsb_event0_prefix ASSETS OUTPUT_DIR\n"; return 2; }
        const std::filesystem::path assets(argv[1]), output(argv[2]); std::filesystem::create_directories(output);
        auto memory = Memory::from_pe32(fsb::lab::read(assets / "FLYINGSB.EXE"));
        Arena arena(memory); arena.initialize(); Actors actors(memory); actors.bootstrap_new_game_actors();
        Viewport viewport(memory); viewport.configure_framebuffer(640,480);
        Map map(memory); MapAssets maps; const auto names = Map::asset_names(memory,469);
        for (unsigned i = 0; i < 6; ++i) maps.files[i] = fsb::lab::read(assets / "MAPSET" / names[i]); map.register_assets(469,std::move(maps));
        Audio audio(memory); audio.initialize();
        for (auto id : {8u,49u,50u}) audio.register_wave(true,id,fsb::lab::read(assets / "audio" / Audio::resource_name(memory,true,id)));
        memory.write(0x6d9ebc,8); Palette palette(memory,{});
        HsmQueue queue;Dialogue dialogue(memory,queue);dialogue.configure_timing(0,30,0);
        VmEnvironment environment; environment.audio=&audio; environment.palette=&palette; environment.map=&map; environment.viewport=&viewport;environment.dialogue=&dialogue;
        const auto root = arena.activate_event(0), object = *resolve_compact(memory,root); Vm vm(memory,queue,environment,object);
        std::ofstream trace(output / "root-prefix.tsv"); trace << "step\tpc\topcode\tsubop\tnext_pc\n"; unsigned steps=0;
        environment.trace = [&](Address obj,const Instruction& ins,bool after) {
            if (after) trace << ++steps << "\t0x" << std::hex << ins.pc << "\t0x" << unsigned(ins.opcode) << "\t0x" << unsigned(ins.subop) << "\t0x" << memory.read(obj+0x30) << std::dec << '\n';
        };
        bool stopped = false;FrameResult result;
        try { result=vm.run_frame(1); } catch (const Fault& fault) { stopped = true; std::cerr << "Stopped: " << fault.what() << '\n'; }
        std::ofstream state(output / "root-prefix-state.json");
        state << "{\n  \"event0_complete\": false,\n  \"scope\": \"root preamble with actor/dialogue factories and explicit640x480 config; no full frame runner\",\n"
              << "  \"executed_instructions\": " << steps << ",\n  \"stopped\": " << (stopped ? "true" : "false")
              << ",\n  \"yield\": " << unsigned(result.yield) << ",\n  \"dialogue_controllers\": " << memory.read(0x768684)
              << ",\n  \"next_pc\": \"0x" << std::hex << vm.pc() << std::dec << "\",\n  \"map_id\": " << memory.read(0x5d229c)
              << ",\n  \"bgm_id\": " << memory.read(0x769654) << ",\n  \"viewport\": [" << memory.read(0x6d9d30) << ',' << memory.read(0x6d9d34) << ',' << memory.read(0x6d9d38) << ',' << memory.read(0x6d9d3c) << "],\n  \"actors\": [\n";
        unsigned index=0;
        for (auto actor : {lookup_actor(memory,1),lookup_actor(memory,9),memory.read(0x6d050d)}) {
            if (!actor) throw Fault(vm.pc(),"root preamble failed to materialize all3 actors");
            if (index++) state << ",\n";
            state << "    {\"address\": \"0x" << std::hex << actor << std::dec << "\", \"slot\": " << memory.read(actor) << ", \"tile_x\": " << memory.read(actor+0x128)
                  << ", \"tile_y\": " << memory.read(actor+0x12c) << ", \"sprite_base\": " << memory.read(actor+0x130) << ", \"frame_selector\": " << memory.read(actor+0x134) << '}';
        }
        state << "\n  ]\n}\n";
        std::cout << "executed_original_root_instructions=" << steps << " next_pc=0x" << std::hex << vm.pc() << " event0_complete=false\n";
        return stopped ? 3 : 4; //4 is the first scheduler yield, not complete Event0.
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
