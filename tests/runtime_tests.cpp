#include "fsb_core/actors.hpp"
#include "fsb_core/audio.hpp"
#include "fsb_core/palette.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/viewport.hpp"
#include "fsb_core/vm.hpp"
#include "fsb_core/object_pump.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
namespace {
unsigned checks = 0, failures = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; } }
template<class F> void fault(F&& fn, const char* name) { bool yes = false; try { fn(); } catch (const Fault&) { yes = true; } check(yes, name); }
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        const std::filesystem::path assets = std::filesystem::path(argv[1]).parent_path();
        auto memory = Memory::from_pe32(fsb::lab::read(argv[1]));
        Arena arena(memory); arena.initialize(); Actors actors(memory); actors.bootstrap_new_game_actors();
        check(actors.player_id() == 3 && memory.read(0x803a20) == 1, "new-game actor bootstrap selects original starter3");
        check(memory.read(0x803a34) == 5 && memory.read(0x803988) == 9 && memory.read(0x803998) == 11,
              "common bootstrap snapshot1 retains original five-member party");
        check(memory.read(Actors::slot(767)) == 767 && memory.read(Actors::slot(90)) == 90, "actor pool identities initialize through camera slot767");

        Audio audio(memory); audio.initialize();
        for (auto id : {8u,49u,50u}) audio.register_wave(true, id, fsb::lab::read(assets / "audio" / Audio::resource_name(memory, true, id)));
        for (auto id : {90u,119u,120u,121u,122u,123u,124u,125u,126u}) {
            auto path = assets / "se_event" / Audio::resource_name(memory, false, id); path.replace_extension(".wav");
            audio.register_wave(false, id, fsb::lab::read(path));
        }
        Viewport viewport(memory); viewport.configure_framebuffer(640,480); // Explicit reference recording configuration; EXE/INI defaults are800x600.
        Map map(memory); MapAssets map_assets; const auto map_names = Map::asset_names(memory, 469);
        for (unsigned i = 0; i < 6; ++i) map_assets.files[i] = fsb::lab::read(assets / "MAPSET" / map_names[i]);
        {
            auto lazy_memory=memory;Map lazy(lazy_memory);unsigned requests=0;
            lazy.asset_provider=[&](unsigned id){check(id==469,"lazy asset request carries the original map id");++requests;return map_assets;};
            check(requests==0,"installing an asset provider does not load maps");
            lazy.load_registered(469);lazy.load_registered(469);
            check(requests==2&&lazy_memory.read(0x5d229c)==469,"map entry obtains and consumes the requested bundle");
            lazy.register_assets(469,map_assets);lazy.load_registered(469);
            check(requests==2,"explicit fixture bundle takes precedence over host asset provider");
        }
        map.register_assets(469, std::move(map_assets));
        memory.write(0x6d9ebc, 8); Palette palette(memory, {}); HsmQueue queue; VmEnvironment env; env.palette = &palette; env.audio = &audio; env.map = &map;
        const auto root = arena.clone_event(0x61f0ba, 0), object = *resolve_compact(memory, root); Vm vm(memory, queue, env, object);
        fault([&] { vm.run_frame(1); }, "integrated root reaches the unimplemented viewport opcode90");
        check(vm.pc() == 0x61f0f4 && audio.bgm_playing() && memory.read(0x769654) == 8,
              "root executes92,E2,ED,C1,E8,E0,E8 from original bytecode without skipped instructions");
        check(memory.read(0x5d229c) == 469 && memory.read(0x7ab9ac) == 0xffffffff && memory.read(0x7ab558) == 0 && memory.read(0x8577d8) == 0,
              "actual E0 loads map469,no matching sparkle/sequence actors,and clears pcpos direction");
        check(memory.read(Actors::slot(0) + 0x128) == 5 && memory.read(Actors::slot(0) + 0x12c) == 40 && !(memory.read(Actors::slot(0) + 4) & 0x10000),
              "E0 restores active player to original pcpos and leaves event movement disabled");
        check(memory.read(0x803a20) == 2 && actors.player_id() == 1 && actors.in_party(9) && !actors.in_party(3),
              "actual E8 mask65 replaces starter with actor1 and adds actor9");
        check(lookup_actor(memory, 1) == Actors::slot(0) && lookup_actor(memory, 9) == Actors::slot(1) && lookup_actor(memory, 3) == 0,
              "party compaction updates all affected catalog pointers");
        env.viewport = &viewport;
        viewport.center(640, 480, true, false);
        fault([&] { vm.run_frame(1); }, "connected root stops at real dialogue creation4F/09");
        check(vm.pc() == 0x61f24c && memory.read(0x6e1440) == 360 && memory.read(0x6d9d34) == 60,
              "root executes entire actor/map/viewport preamble to SONA dialogue creation");
        check(memory.read(0x6d0491) && memory.read(0x6d058d) && arena.members(1).size() == 2 && memory.read(0x6d050d) == Actors::slot(10),
              "root creates both real actor child VMs and SB effect before dialogue");

        // Continue actor setup commands in isolated contexts while viewport90 is unfinished.
        memory.write(object + 0x30, 0x61f0f0); vm.step();
        check(!(memory.read(Actors::slot(0) + 4) & 64) && !(memory.read(Actors::slot(1) + 4) & 64), "actual E8/10 hides both Event0 party actors");
        memory.write(object + 0x30, 0x61f11a); vm.step(); vm.step(); vm.step(); vm.step();
        const auto sonata = lookup_actor(memory, 9);
        check(memory.read(sonata + 8) == 416 * 65536 && memory.read(sonata + 12) == 1560 * 65536 && memory.read(sonata + 0x1c) == 0x18000,
              "original42 places sonata at6,32,1 with half-tile centers");
        check(memory.read(sonata + 0x134) == 26 && memory.read(sonata + 0x138) == 0 && !(memory.read(sonata + 4) & 8) && memory.read(sonata + 0x24) == 0xfffc0000,
              "original62,4A,25 set organ frame,remove shadow flag,and offsetY by-4px");
        vm.step(); vm.step();
        check(memory.read(Actors::slot(0) + 0x130) == 8 && memory.read(Actors::slot(0) + 0x128) == 6 && memory.read(Actors::slot(0) + 0x12c) == 37,
              "actor1 is placed by42 and switched to original profile8 by4A/09");
        actors.reset_sequence_list(); memory.write(object + 0x30, 0x61f1d4); vm.step(); vm.step();
        const auto effect = memory.read(0x6d050d);
        check(effect == Actors::slot(10) && memory.read(effect + 0x130) == 0 && memory.read(effect + 0x128) == 5 && memory.read(effect + 0x12c) == 38 && memory.read(effect + 0x24) == 0xffd80000,
              "actual58 creates SB effect in1AC actor slot10,not compact arena");

        // The original audio VM fade has a separate group1 object and busy flag.
        memory.write(object + 0x30, 0x61f44f); vm.step();
        const auto fade_handle = memory.read(0x768a84), fade_object = *resolve_compact(memory, fade_handle);
        ObjectPump pump(memory, arena, [&](Address callback, Address obj, unsigned jobs) {
            if (callback != 0x42c69e) throw Fault(callback, "unexpected audio fixture callback"); audio.tick_fade(obj, jobs);
        });
        memory.write(object + 0x30, 0x61f46b);
        check(vm.step() == Yield::Forced && vm.pc() == 0x61f46b, "C2/03 waits on actual45-job BGM fade");
        pump.update_object(fade_object, 44);
        check(memory.read(0x76e918) == 3 && audio.bgm_playing() && memory.read(0x768a9c) == 1, "negative fade interpolation truncates toward zero at44/45");
        pump.update_object(fade_object, 1);
        check(memory.read(0x76e918) == 0 && !audio.bgm_playing() && memory.read(fade_object + 0x20) == 0xffffffff && !memory.read(0x768a9c),
              "45th job silences and stops BGM; object release remains deferred");
        check(vm.step() == Yield::Continue && vm.pc() == 0x61f46f, "audio fade wait releases from callback completion");
        pump.update_object(fade_object, 1); check(!resolve_compact(memory, fade_handle), "audio fade object releases on next object update");
        check(Audio::volume_millibels(100) == 0 && Audio::volume_millibels(50) == -750 && Audio::volume_millibels(0) == -10000,
              "disassembled nonlinear DirectSound percent-to-millibel curve");

        auto wave_path = assets / "se_event/SP_WIND02.wav"; const auto wind_bytes = fsb::lab::read(wave_path); const auto wind = Pcm::wave(wind_bytes);
        audio.set_time(1000); memory.write(0x6d04f5, 126); memory.write(object + 0x30, 0x61f098); vm.step();
        check(vm.pc() == 0x61f0a1 && vm.step() == Yield::Forced, "actual external-child C8/02 parks on wind126 playback");
        const auto output_frames = unsigned((wind.frames() * 44100ull + wind.rate - 1) / wind.rate);
        auto pcm_output = audio.mix(output_frames - 1);
        check(vm.step() == Yield::Forced, "C8 remains blocked one output sample before end of original WAV");
        auto final_sample = audio.mix(1); pcm_output.insert(pcm_output.end(), final_sample.begin(), final_sample.end());
        check(vm.step() == Yield::Continue && vm.pc() == 0x61f0aa && audio.cue_finished(126), "sample completion releases original C8 wait without forced drain");
        check(pcm_output.size() == std::size_t(output_frames) * 2, "PCM renderer generates complete original sound duration");

        audio.play_cue(125); const auto older = memory.read(0x76ea58 + 125 * 4); audio.mix(10); audio.play_cue(125);
        const auto newer = memory.read(0x76ea58 + 125 * 4); audio.stop_cue(125);
        check(older != newer && audio.cue_finished(125), "same-cue overlap has distinct handles; stop/wait follows only latest");
        const auto tail = audio.mix(1000); bool nonzero = false; for (auto sample : tail) nonzero |= sample != 0;
        check(nonzero, "earlier overlapping sound continues after latest handle stopped");
        audio.apply_bgm(49); audio.fade(20, 100, 0, false); const auto canceled = memory.read(0x768a84); audio.cancel_fade();
        check(!audio.bgm_playing() && !memory.read(0x768a9c) && !resolve_compact(memory, canceled), "cancel fade applies target and stop policy then immediately releases fade object");
        auto broken = wind_bytes; broken.resize(30); fault([&] { Pcm::wave(broken); }, "truncated RIFF cannot create a false finished cue");
        auto door_bytes=fsb::lab::read(assets/"se_event/DOOR01.wav");const auto door=Pcm::wave(door_bytes);
        check(door.byte_count==17294&&door.frames()==8647,"original DOOR01 keeps complete PCM despite oversized outer RIFF header");
        door_bytes.pop_back();fault([&]{Pcm::wave(door_bytes);},"actual truncated DOOR01 PCM is still rejected");
        const auto miro=Pcm::wave(fsb::lab::read(assets/"audio/MIRO.WAV"));
        check(miro.byte_count==1503537&&miro.frames()==751768&&miro.samples.size()==1503536,"MIRO preserves source byte count and emits only complete stereo frames");

        auto view_memory = Memory::from_pe32(fsb::lab::read(argv[1])); Arena view_pool(view_memory); view_pool.initialize(); Viewport view(view_memory);
        view.configure_framebuffer(640,480); view_memory.write(0x6e146c, 1);
        view_memory.write(0x6db16c, 101); view_memory.write(0x6d9d00, 102);
        view.center(640,360);
        const auto border = view_pool.members(0).front(), border_object = *resolve_compact(view_memory, border), allocation = view_memory.read(border_object + 0x1a4);
        check(view_memory.read(allocation) == 1 && view_memory.read(allocation + 4) == 1 && view_memory.read(allocation + 20) == 0,
              "viewport shrink creates original80-byte top/bottom border state");
        ObjectPump view_pump(view_memory, view_pool, [&](Address, Address obj, unsigned jobs) { view.tick(obj, jobs); });
        view_pump.update_group(0, 1); auto fills = view.take_fills();
        Image8 border_frame; border_frame.width = 640; border_frame.height = 480; border_frame.pixels.assign(640*480, 250);
        for (const auto& fill : fills) if (fill.surface == 101) Viewport::fill(border_frame, fill);
        check(fills.size() == 4 && border_frame.pixels[0] == 0 && border_frame.pixels[60*640] == 250 && border_frame.pixels[479*640] == 0,
              "border callback fills exposed strips on target/backbuffer without touching interior");
        view_pump.update_group(0, 1); check(!resolve_compact(view_memory, border), "viewport border callback tears down through the scheduler");
        fault([&] { view_memory.read(allocation); }, "freed viewport state is unmapped rather than readable stale bytes");
        view.tween_center(30,640,576); view_pump.update_group(0, 15);
        check(view_memory.read(0x6d6694) == 6 && view_memory.read(0x6e1440) == 468,
              "viewport tween interpolates requested rect before screen clipping");
        view_pump.update_group(0, 15);
        check(view_memory.read(0x6d6694) == 0xffffffd0 && view_memory.read(0x6d9d34) == 0 && view_memory.read(0x6e1440) == 480,
              "authored576px viewport clamps to480px but keeps requested negative top");
        view_pump.update_group(0, 1); view_pump.update_group(0, 1);
        check(view_pool.members(0).empty(), "tween and same-traversal border children finish without leaked objects");
        VmEnvironment fixed_env;fixed_env.viewport=&view;HsmQueue fixed_queue;
        const auto fixed_handle=view_pool.clone_event(0x620172,0),fixed_object=*resolve_compact(view_memory,fixed_handle);
        Vm fixed_vm(view_memory,fixed_queue,fixed_env,fixed_object);fixed_vm.step();
        check(fixed_vm.pc()==0x620176&&view_memory.read(0x6d6690)==0xffffffb0u&&view_memory.read(0x6d6694)==15&&view_memory.read(0x6e12b0)==640&&view_memory.read(0x6e1440)==450,
              "actual Event2 90/5 centers800x450 then clips to the640px framebuffer");
        auto final_memory=Memory::from_pe32(fsb::lab::read(argv[1]));Actors final_actors(final_memory);const auto tail_actor=Actors::slot(2);
        final_memory.write(0x80465c,3);final_memory.write(0x57fd1c,2);final_memory.write(0x5d0768,0xffffffffu);final_memory.write(0x7e0d20+0x8028,12);
        for(unsigned event:{0u,0x100u,0x101u,0x403u})for(unsigned key:{13u,16u,32u,88u,38u}){
            for(unsigned offset=0;offset<428;offset+=4)final_memory.write(tail_actor+offset,0);
            final_memory.write(tail_actor,2);final_memory.write(tail_actor+4,0x1ce);final_memory.write(tail_actor+0x148,0x458ed7);
            set_actor_tile_position(final_memory,tail_actor,6,37,1);final_memory.write(0x776478,0);final_memory.write(0x6da2dc,event);final_memory.write(0x6d66b0,key);
            final_actors.finalize(tail_actor);
            check(final_memory.read(tail_actor)==2&&!final_memory.read(tail_actor+0x148)&&final_memory.read(0x776478)==1,
                  "party compaction accepts ordinary input while preserving the original field finalizer write");
        }
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto& region : memory.snapshot_regions()) for (auto byte : region.bytes) hash = (hash ^ byte) * 1099511628211ull;
        for (const auto& region : view_memory.snapshot_regions()) for (auto byte : region.bytes) hash = (hash ^ byte) * 1099511628211ull;
        for (auto sample : pcm_output) for (unsigned i = 0; i < 2; ++i) hash = (hash ^ std::uint8_t(std::uint16_t(sample) >> (i * 8))) * 1099511628211ull;
        for (const auto& event : audio.events()) {
            const std::uint64_t fields[] = {unsigned(event.kind),event.bgm,event.id,event.handle,event.output_sample,std::uint32_t(event.millibels)};
            for (auto field : fields) for (unsigned i = 0; i < 8; ++i) hash = (hash ^ std::uint8_t(field >> (i * 8))) * 1099511628211ull;
        }
        std::cout << "runtime_state_pcm_fnv1a=" << std::hex << hash << std::dec << '\n';
        std::cout << "runtime_checks=" << checks << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
