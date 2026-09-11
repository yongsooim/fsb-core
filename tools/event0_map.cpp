#include "fsb_core/map.hpp"
#include "fsb_core/palette.hpp"
#include "lab_io.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace fsb::core;
int main(int argc, char** argv) {
    try {
        if (argc != 3) { std::cerr << "Usage: fsb_event0_map ASSETS OUTPUT_DIR\n"; return 2; }
        const std::filesystem::path assets(argv[1]), output(argv[2]); std::filesystem::create_directories(output);
        auto memory = Memory::from_pe32(fsb::lab::read(assets / "FLYINGSB.EXE"));
        memory.write(0x4a504c, 640); memory.write(0x4a5050, 480); // Explicit recording-resolution fixture, not EXE's800x600 default.
        MapAssets data; const auto names = Map::asset_names(memory, 469);
        for (unsigned i = 0; i < 6; ++i) data.files[i] = fsb::lab::read(assets / "MAPSET" / names[i]);
        Map map(memory); map.load_assets(469, data);
        memory.write(0x6d9ebc, 8); Palette palette(memory, {}); palette.upload(0x804660, 0, 256, true);
        std::ofstream trace(output / "map-trace.tsv"); trace << "fixture\tcamera_x\tcamera_y\tviewport_height\tlayer\torigin_x_q16\torigin_y_q16\tcommands\n";
        // Explicit static viewpoints. No camera tween, input, actors, foreground
        // interleaving, dialogue, audio, or Event0 runtime is simulated here.
        const std::array<std::pair<int,int>,3> viewpoints{{{241, 360}, {1537, 480}, {1681, 480}}};
        unsigned index = 0;
        for (auto [camera_y, height] : viewpoints) {
            memory.write(0x6e12b0, 640); memory.write(0x6e1440, height);
            memory.write(0x6d9d30, 0); memory.write(0x6d9d34, (480 - height) / 2);
            memory.write(0x6da548, 0); memory.write(0x6da54c, (480 - height) / 2);
            memory.write(0x6da550, 640); memory.write(0x6da554, (480 + height) / 2);
            memory.write(0x7873c0, 384); memory.write(0x7873c4, camera_y); map.update_scroll();
            Image8 frame; frame.width = 640; frame.height = 480; frame.pixels.resize(640 * 480); frame.palette = palette.colors();
            for (unsigned layer = 0; layer < memory.read(0x800dbc); ++layer) {
                const auto commands = map.background_commands(layer); map.draw_background(frame, commands);
                trace << index << "\t384\t" << camera_y << '\t' << height << '\t' << layer << '\t'
                      << memory.read(0x7e0ca0 + layer * 52) << '\t' << memory.read(0x7e0ca4 + layer * 52) << '\t' << commands.size() << '\n';
            }
            std::ostringstream name; name << "map-" << std::setw(2) << std::setfill('0') << index++ << ".bmp";
            fsb::lab::bmp(frame, output / name.str());
        }
        std::cout << "map469_background_fixtures=3; original MAP/MAT/MFO/PCX, no captured raster assets\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
