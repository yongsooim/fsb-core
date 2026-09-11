#include "fsb_core/actors.hpp"
#include "fsb_core/object_pump.hpp"
#include "fsb_core/vm.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
namespace {
unsigned checks = 0, failed = 0;
std::uint64_t hash = 14695981039346656037ull;
void mix(std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) hash = (hash ^ std::uint8_t(value >> (i * 8))) * 1099511628211ull; }
void check(bool value, const char* name) { ++checks; if (!value) { ++failed; std::cerr << "FAIL " << name << '\n'; } }
struct Script {
    Address base = 0x20000000;
    std::vector<std::uint8_t> bytes;
    Address op(unsigned opcode, unsigned sub, std::initializer_list<Operand> operands = {}) {
        const auto pc = base + bytes.size(); const unsigned size = 4 + operands.size() * 5;
        bytes.insert(bytes.end(), {std::uint8_t(opcode), std::uint8_t(sub), std::uint8_t(size), std::uint8_t(size >> 8)});
        for (auto operand : operands) { bytes.push_back(operand.descriptor); for (unsigned i = 0; i < 4; ++i) bytes.push_back(std::uint8_t(operand.payload >> (i * 8))); }
        return static_cast<Address>(pc);
    }
};
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        auto memory = Memory::from_pe32(fsb::lab::read(argv[1]));
        const auto reference = fsb::lab::read(argv[2]);
        check(reference.size() == 8 + 4096 * 8 + 30031 * 32 && std::string(reference.begin(), reference.begin() + 7) == "FSBMOT1", "original x86 oracle format");
        if (failed) return 1;
        const auto expected = [&](unsigned offset) { std::uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(reference.at(offset + i)) << (i * 8); return value; };
        unsigned mismatch = 0;
        for (unsigned angle = 0; angle < 65536; ++angle) {
            const auto phase = ((angle + 8) >> 4) & 4095;
            const auto sine = fixed_sin(memory, angle), cosine = fixed_cos(memory, angle);
            mismatch += sine != expected(8 + phase * 8) || cosine != expected(12 + phase * 8); mix(sine); mix(cosine);
        }
        check(mismatch == 0, "all65536 rounded angles match actual original x86 sin/cos routines");
        check(fixed_sin(memory, 0xfffffff8) == 0 && fixed_cos(memory, 0xfffffff8) == 65536, "phase rounding wraps uint32 before lookup");
        Arena arena(memory); arena.initialize(); Actors actors(memory); actors.bootstrap_new_game_actors();
        const auto actor = lookup_actor(memory, 3);
        set_actor_raw_position(memory, actor, 0x1234567, 0xfffedcbb);
        const auto tween = actors.tween_tiles(3, 7, std::uint32_t(-3), 60, std::uint32_t(-17), 23, false, false);
        const auto obj = *resolve_compact(memory, tween), timer = memory.read(obj + 0x1a4);
        memory.write(timer, 0); memory.write(timer + 12, 0); // Explicit numeric fixture, no elapsed-time claim.
        mismatch = 0;
        for (unsigned progress = 0; progress <= 30030; ++progress) {
            memory.write(timer + 4, 30030 - progress); memory.write(obj + 0x20, 1);
            actors.tick_tile_tween(obj, false);
            unsigned lane = 0;
            for (const auto offset : {8u, 12u, 0x14u, 0x18u, 0x128u, 0x12cu, 0x158u, 0x15cu}) {
                const auto value = memory.read((lane < 6 ? actor : obj) + offset), wanted = expected(8 + 4096 * 8 + progress * 32 + lane * 4);
                if (value != wanted && mismatch++ < 4) std::cerr << "oracle p=" << progress << " lane=" << lane << " got=" << value << " expected=" << wanted << '\n';
                mix(value); ++lane;
            }
        }
        check(mismatch == 0, "all30031 progress values match original x86 complete tile-tween callback");
        ObjectPump pump(memory, arena, [&](Address callback, Address object, unsigned jobs) {
            if (callback == 0x431273) actors.tick_tile_tween(object, false);
            else if (callback == 0x4314c1) actors.tick_raw_tween(object, jobs);
            else throw Fault(callback, "unexpected tween callback");
        });
        pump.update_group(1, 1);
        check(!resolve_compact(memory, tween), "normal tween releases timer and object on next update");
        bool unmapped = false; try { memory.read(timer); } catch (const Fault&) { unmapped = true; }
        check(unmapped, "normal tween cleanup unmaps its owned timer");
        set_actor_raw_position(memory, actor, std::uint32_t(-100 * 65536 - 123), 100 * 65536 + 456);
        const auto raw = actors.tween_raw(3, std::uint32_t(-99), 99, 3, false), raw_obj = *resolve_compact(memory, raw);
        check(memory.read(raw_obj + 0x180) == 43690, "raw tween derives velocity from signed integer pixel, not fractional start");
        pump.update_group(1, 0); check(memory.read(actor + 8) == std::uint32_t(-100 * 65536 - 123), "zero jobs preserve raw position");
        pump.update_group(1, 3);
        check(memory.read(raw_obj + 0x20) == 0xffffffffu && memory.read(actor + 8) == std::uint32_t(-99 * 65536) && memory.read(actor + 12) == 99 * 65536 + 457,
              "raw tween clamps overshoot and completes at target integer pixel despite fractional Y");
        pump.update_group(1, 1); check(!resolve_compact(memory, raw), "raw tween deferred release skips callback");
        check(actors.tween_raw(3, 20, 30, 0, false) == 0 && memory.read(actor + 8) == 20 * 65536, "zero duration applies raw destination immediately");

        HsmQueue queue; VmEnvironment environment;
        ObjectPump vm_pump(memory, arena, [&](Address callback, Address object, unsigned jobs) {
            if (callback == 0x41a042) Vm(memory, queue, environment, object).run_frame(jobs);
            else throw Fault(callback, "unexpected VM callback");
        });
        bool turns_finished = true;
        for (unsigned from = 0; from < 8; ++from) for (unsigned to = 0; to < 8; ++to) {
            memory.write(actor + 0x110, from); memory.write(actor + 0x114, from);
            const auto child = actors.start_turn(3, to, 0); unsigned jobs = 0;
            while (resolve_compact(memory, child) && jobs < 1000) { vm_pump.update_group(1, 1); ++jobs; }
            turns_finished &= !resolve_compact(memory, child) && memory.read(actor + 0x110) == to; mix(jobs);
        }
        check(turns_finished, "original shared turn script completes all64 source/destination directions");
        check(actors.start_turn(3, 8, 0) == 0 && arena.members(1).empty(), "invalid direction creates no child");
        const auto floating = arena.clone_event(0x61fac1, 1), floating_obj = *resolve_compact(memory, floating);
        memory.write(floating_obj + 0xfc, actor); memory.write(floating_obj + 0xe8, 8); memory.write(floating_obj + 0xec, 0x100); memory.write(actor + 0x24, 12345);
        bool waveform = true;
        for (unsigned job = 1; job <= 256; ++job) {
            vm_pump.update_group(1, 1);
            const auto value = memory.read(actor + 0x24);
            waveform &= value == 12345u - fixed_cos(memory, 0x4000 + job * 0x100) * 8u; mix(value);
        }
        check(waveform && memory.read(actor + 0x24) == 12345, "actual SONA floating child completes one256-job oscillation without drift");
        arena.release(floating);

        const auto reaction = arena.clone_event(0x6ce342, 1), reaction_obj = *resolve_compact(memory, reaction);
        memory.write(reaction_obj + 0xf8, 3); memory.write(reaction_obj + 0xfc, actor); memory.write(reaction_obj + 0xf4, packed_id("SON5"));
        for (const auto code : {3001u,3035u,3000u}) {
            // Actual reaction codes present in SON5 markup; unit stimuli, no forged completion messages.
            queue.enqueue({packed_id("SON5"),0xffffffffu,code|0x8000u,0});
            for (unsigned job = 0; job < 60; ++job) vm_pump.update_group(1,1);
            check(memory.read(reaction_obj+0x30) == 0x6ce354 && queue.size() == 0, "shared reaction dispatcher consumes markup code and returns to genuine flagged wait");
            mix(memory.read(actor+0x134)); mix(memory.read(actor+0x138));
        }
        if (const auto child = memory.read(reaction_obj+0xdc); resolve_compact(memory,child)) arena.release(child);
        arena.release(reaction);

        Script script;
        const auto start = script.op(0x44, 0, {{4,3},{4,2},{4,1}});
        const auto arithmetic = script.op(0x2d, 1, {{0x44,0xe8},{4,10405},{4,12},{4,100}});
        script.op(0x2d, 2, {{0x44,0xe8},{4,2},{4,8}}); // Successful original confirm(<8).
        const auto diagnostic = script.op(0x2d, 2, {{0x44,0xe8},{4,2},{4,1}});
        script.op(0,0); memory.map(script.base,script.bytes,false);
        const auto root = arena.clone_event(start,0), root_obj = *resolve_compact(memory,root);
        Vm vm(memory,queue,environment,root_obj);
        auto result = vm.run_frame(1); check(result.yield == Yield::Forced && vm.pc() == start, "blocking44 waits at original PC while real child is live");
        for (unsigned i = 0; i < 100 && resolve_compact(memory,memory.read(root_obj+0xcc)); ++i) vm_pump.update_group(1,1);
        check(vm.step() == Yield::Continue && vm.pc() == arithmetic && memory.read(root_obj+0xcc) == 0, "blocking44 advances only after shared child expires");
        result = vm.run_frame(1);
        check(memory.read(root_obj+0x20) == 0 && memory.read(root_obj+0xe8) == 5 && environment.diagnostics.size() == 1 && environment.diagnostics[0].first == diagnostic,
              "computed ALU and confirm preserve original log-and-advance behavior without state overwrite");
        arena.release(root);
        const auto move_parent = arena.clone_event(0x61f62a,0), move_obj = *resolve_compact(memory,move_parent);
        memory.write(0x6d050d,actor); set_actor_raw_position(memory,actor,640*65536,480*65536);
        Vm mover(memory,queue,environment,move_obj); mover.step();
        const auto move = memory.read(0x6d04cd), move_child = *resolve_compact(memory,move);
        check(mover.pc() == 0x61f651 && memory.read(move_child+0x168) == std::uint32_t(-64*65536) && memory.read(memory.read(move_child+0x1a4)+20) == 1000,
              "actual root47/1 preserves indirect result slot, negative tile delta and60-frame1000ms duration");
        memory.write(0x6da2d8,1000); pump.update_group(1,1);
        check(memory.read(actor+8) == 576*65536 && memory.read(move_child+0x20) == 0xffffffffu, "actual root tween reaches destination through original progress timer");
        pump.update_group(1,1);
        memory.write(move_obj+0x30,0x61fd27); memory.write(move_obj+0xf8,3); mover.step();
        check(arena.members(1).size() == 1 && memory.read(move_obj+0xd0) == 0 && memory.read(move_obj+0xcc) == 0,
              "actual47/9 discard route still creates and schedules a live actor tween");
        pump.update_group(1,96); check(memory.read(actor+8) == 480*65536, "actual47/9 applies minus96 raw pixels over96 jobs");
        pump.update_group(1,1); arena.release(move_parent);
        std::cout << "motion_state_fnv1a=" << std::hex << hash << std::dec << '\n';
        std::cout << "motion_checks=" << checks << " failures=" << failed << '\n';
        return failed ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
