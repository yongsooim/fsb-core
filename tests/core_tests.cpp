#include "fsb_core/vm.hpp"
#include "fsb_core/arena.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>

using namespace fsb::core;
namespace {
unsigned checks = 0, failures = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
template<class F> void rejects(F&& fn, const char* message) {
    bool caught = false; try { fn(); } catch (const Fault&) { caught = true; }
    check(caught, message);
}
std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open test EXE");
    const auto length = in.tellg(); in.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    in.read(reinterpret_cast<char*>(bytes.data()), length); return bytes;
}
std::vector<std::uint8_t> command(unsigned op, unsigned sub, std::initializer_list<Operand> args = {}) {
    const unsigned size = 4 + 5 * args.size();
    std::vector<std::uint8_t> b{static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(sub), static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8)};
    for (auto a : args) { b.push_back(a.descriptor); for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(a.payload >> (i * 8))); }
    return b;
}
bool equal(Message a, Message b) { return a.target == b.target && a.channel == b.channel && a.code == b.code && a.sender == b.sender; }
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        Memory memory = Memory::from_pe32(read_file(argv[1]));
        check(memory.read(0x6d0144) == 0x61f0ba, "original event table points to Event 0");
        check(memory.read(0x61f0ba, 1) == 0x92, "Event 0 starts with 92/00");
        check(memory.read(0x6e1470 + 0x10) == 0, "PE BSS virtual tail is zero-initialized");
        rejects([&] { memory.read(0); }, "unmapped reads are faults, not zero");
        rejects([&] { memory.write(0x41a042, 0); }, "original native code is read-only");
        memory.map(0x10000000, std::vector<std::uint8_t>(256, 0), true);
        rejects([&] { memory.map(0x100000f0, std::vector<std::uint8_t>(32), true); }, "mapping overlap rejected");
        rejects([&] { memory.read(0x100000ff, 2); }, "read beyond segment rejected");
        memory.write(0x10000001, 0xaabbccdd);
        check(memory.read(0x10000001, 1) == 0xdd && memory.read(0x10000002, 2) == 0xbbcc, "unaligned guest little endian reads");
        const Address object = compact_address(10);
        for (unsigned i = 0; i < 1024; ++i) memory.write(compact_address(i) + 0x10, 1);
        memory.write(object + 0x10, 3); memory.write(object + 0x20, 1);
        check(compact_handle(memory, object) == 0x3000a, "compact generation/index encoding");
        check(resolve_compact(memory, 0x3000a) == object, "live generation resolves");
        memory.write(object + 0x10, 4);
        check(!resolve_compact(memory, 0x3000a), "stale generation does not resolve");
        rejects([&] { compact_address(1024); }, "arena is 1024 slots");
        rejects([&] { compact_handle(memory, object + 4); }, "misaligned compact object rejected");
        Operand{0x41, 0x44}.set(memory, object, 0x12345678);
        check(memory.read(object + 0x44) == 0x78, "byte destination truncates");
        Operand{0x42, 0x44}.set(memory, object, 0x12345678);
        check(Operand{0x42, 0x44}.get(memory, object) == 0x5678, "word operand zero extends");
        check(Operand{0x84, 0xdeadbeef}.get(memory, object) == 0xdeadbeef, "absolute literal is not dereferenced");
        rejects([&] { Operand{0xc4, 0xdeadbeef}.get(memory, object); }, "unknown indirect address never becomes identity");
        check(packed_id("010") == 0x303130 && packed_id("SONA") == 0x534f4e41, "MSVC compact IDs are big-endian packed");
        check(signed32(0xffffffffu) == -1 && signed32(0x80000000u) == (-2147483647 - 1), "explicit signed dword conversion");
        std::uint32_t seed = 1;
        check(msvc_rand(seed) == 41 && msvc_rand(seed) == 18467 && msvc_rand(seed) == 6334, "MSVC RNG known sequence");

        FrameClock clock;
        check(clock.advance(24) == 0 && clock.batch_base() == 0, "phase gate does not consume job clock early");
        check(clock.advance(25) == 1 && clock.batch_base() == 16, "25ms phase gives one 16ms job");
        check(clock.advance(49) == 0 && clock.advance(50) == 2, "phase and job remainders are separate");
        check(clock.advance(250) == 3 && clock.batch_base() == 240, "late frame discards excess jobs before clamping return count");
        check(clock.advance(251) == 0 && clock.advance(275) == 2, "late-frame backlog does not leak into later frames");
        FrameClock wrap(0xfffffff0u); wrap.set_interval(3);
        check(wrap.advance(0x10) == 2, "GetTickCount wraps naturally at 32 bits");
        FrameClock fast; fast.set_fast4(true);
        check(fast.advance(15) == 0 && fast.phase_base() == 12 && fast.advance(16) == 1, "4ms phase is independent of 16ms jobs");

        HsmQueue queue;
        queue.enqueue({1, 0xffffffffu, 0, 5}); queue.enqueue({1, 2, 0x8001, 6});
        check(!queue.find(1, 2) && queue.find(1, 0) == 0, "send sentinel is not a receive wildcard");
        check(queue.find_flagged(1) == 1, "flagged and unflagged messages stay separate");
        queue.clear(); check(queue.head() == 2 && queue.tail() == 2, "clear moves tail to head without resetting physical indices");
        for (unsigned i = 0; i < 29; ++i) queue.enqueue({i, 1, 0, i});
        queue.clear();
        queue.enqueue({101, 0, 0, 0}); queue.enqueue({102, 0, 0, 0});
        queue.remove(31);
        check(queue.tail() == 32 && queue.at(0).target == 102, "wrapped removal preserves original tail==32 cursor");
        queue.clear();
        for (unsigned i = 0; i < 31; ++i) queue.enqueue({i, 0, 0, i});
        rejects([&] { queue.enqueue({99, 0, 0, 0}); }, "HSM overflow faults instead of silently losing a message");
        check(queue.size() == 31, "overflow leaves previous messages intact");

        // Independent model checks ordering across many wrap/remove/clear combinations.
        std::vector<Message> expected;
        queue.clear(); std::mt19937 random(17);
        for (unsigned iteration = 0; iteration < 1000; ++iteration) {
            const unsigned action = random() % 5;
            if (action == 0) { queue.clear(); expected.clear(); }
            else if ((action <= 2 || expected.empty()) && expected.size() < 31) {
                Message m{iteration, random(), random(), random()}; queue.enqueue(m); expected.push_back(m);
            } else if (!expected.empty()) {
                const auto index = random() % expected.size();
                queue.remove((queue.tail() + index) & 31u); expected.erase(expected.begin() + index);
            }
            const auto actual = queue.logical_records();
            check(actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin(), equal), "queue preserves logical order under randomized ring operations");
        }

        VmEnvironment environment; Vm vm(memory, queue, environment, object);
        queue.clear(); memory.write(object + 0x30, 0x61f33e);
        check(vm.step() == Yield::Continue && vm.pc() == 0x61f34c, "actual Event 0 sends X to SONA");
        check(queue.at(queue.tail()).target == packed_id("SONA") && queue.at(queue.tail()).channel == packed_id("X"), "actual Event 0 send operand order");
        for (unsigned i = 0; i < 500; ++i) check(vm.run_frame(3).yield == Yield::Forced && vm.pc() == 0x61f34c, "Event 0 marker wait never times out into success");
        queue.enqueue({packed_id("010"), packed_id("OTHR"), 0, 12});
        check(vm.run_frame(3).yield == Yield::Forced, "wrong-channel marker does not resume Event 0");
        queue.enqueue({packed_id("010"), packed_id("SONA"), 0, 12});
        check(vm.step() == Yield::Continue && vm.pc() == 0x61f35a, "matching marker resumes actual Event 0 PC");
        check(memory.read(object + 0x10c) == packed_id("010") && memory.read(object + 0x110) == packed_id("SONA"), "HSM receive updates original cache fields");
        rejects([&] { vm.step(); }, "unimplemented next event operation is a fault");
        check(vm.pc() == 0x61f35a, "unsupported operation does not advance PC");

        auto script = command(0x02, 3, {{0x44, 0xd8}});
        memory.map(0x11000000, script, false);
        memory.write(object + 0x30, 0x11000000);
        const auto child = compact_address(7); memory.write(child + 0x10, 8);
        memory.write(object + 0xd8, compact_handle(memory, child));
        check(vm.run_frame(3).yield == Yield::Forced && vm.pc() == 0x11000000, "live child handle holds PC");
        memory.write(child + 0x10, 9); // controlled test of actual generation invalidation
        check(vm.step() == Yield::Continue && memory.read(object + 0xd8) == 0, "dead child clears the same typed slot");

        auto timed = command(0x02, 0, {{4, 3}});
        const auto forced = command(1, 1); timed.insert(timed.end(), forced.begin(), forced.end());
        memory.map(0x12000000, timed, false);
        memory.write(object + 0x30, 0x12000000); memory.write(object + 0x40, 0);
        memory.write(object + 0x1b, 0, 1);
        check(vm.run_frame(3).normal_yields == 1 && memory.read(object + 0x40) == 2, "ordinary VM consumes one timed yield");
        memory.write(object + 0x1b, 0x10, 1);
        const auto accelerated = vm.run_frame(3);
        check(accelerated.normal_yields == 2 && accelerated.yield == Yield::Forced, "accelerated VM crosses normal yields, stops at forced yield");

        const auto listing = Instruction::scan(memory, memory.read(0x6d0144), memory.read(0x6d0148), true);
        const unsigned instructions = static_cast<unsigned>(listing.size());
        unsigned root = 0; for (const auto& ins : listing) if (ins.pc < 0x61f96e) ++root;
        check(instructions == 283 && listing.back().next() == 0x61ff19,
              "Event 0 has 283 contiguous records plus 3 padding bytes, not 284 opcodes");
        rejects([&] { Instruction::scan(memory, 0x61f0ba, 0x62016e); }, "old Event-2 boundary cannot turn padding into a 37KB instruction");
        check(Instruction::scan(memory, 0x61f05d, 0x61f0ba).size() == 12, "external sound child before root contains 12 records");
        auto arena_memory = Memory::from_pe32(read_file(argv[1]));
        Arena arena(arena_memory); arena.initialize();
        check(!resolve_compact(arena_memory, 0), "original reserved slot makes null handle invalid");
        const auto root_handle = arena.clone_event(arena_memory.read(0x6d0144), 0);
        check(root_handle == 0x10001, "fresh event clone uses original first free slot 1 and generation 1");
        const auto root_object = *resolve_compact(arena_memory, root_handle);
        check(arena_memory.read(root_object + 0x30) == 0x61f0ba && arena_memory.read(root_object + 0x18) == 0x10030000,
              "original event clone PC and flags");
        const auto child_a = arena.clone_event(0x61fac1, 4), child_b = arena.clone_event(0x61fb45, 4);
        check(arena.members(4) == std::vector<Handle>{child_a, child_b}, "children append in source pool order");
        const auto child_a_address = *resolve_compact(arena_memory, child_a);
        const auto child_b_address = *resolve_compact(arena_memory, child_b);
        arena.release(child_a);
        check(!resolve_compact(arena_memory, child_a), "arena release invalidates handle generation");
        check(arena_memory.read(child_a_address + 4) == child_b_address, "released object preserves next link for live walker");
        check(arena.members(4) == std::vector<Handle>{child_b}, "release repairs both intrusive links");
        const auto reused = arena.clone_event(0x61f96e, 4);
        check((reused & 0xffff) == (child_a & 0xffff) && (reused >> 16) == 3, "reuse increments generation on release and allocation");
        check(arena.members(4) == std::vector<Handle>{child_b, reused}, "reused slot is appended, not sorted by slot number");
        check(arena_memory.read(0x6d9e60) == 3, "arena active counter follows allocation/release");
        rejects([&] { arena.release(child_a); }, "stale release cannot corrupt another live object");
        const auto exclusive = arena.allocate_after(Arena::head(0), 1, 0x100000, 0);
        check(arena.active_exclusive() == *resolve_compact(arena_memory, exclusive) && arena_memory.read(0x6d9e64) == 1,
              "first exclusive object claims original active ownership");
        std::uint64_t digest = 14695981039346656037ull;
        for (const auto& region : memory.snapshot_regions()) for (auto byte : region.bytes)
            digest = (digest ^ byte) * 1099511628211ull;
        for (const auto& region : arena_memory.snapshot_regions()) for (auto byte : region.bytes)
            digest = (digest ^ byte) * 1099511628211ull;
        std::cout << "state_fnv1a=" << std::hex << digest << std::dec << '\n';
        std::cout << "event0_scan=" << instructions << " root_region=" << root << '\n';
        std::cout << "checks=" << checks << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
