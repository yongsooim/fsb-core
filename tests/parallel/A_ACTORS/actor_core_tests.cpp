// Compare the A_ACTORS reconstructions against original x86 execution.
//
// Each fixture case restores the same captured state the oracle used, runs the
// original entry through the product dispatch (which now reaches the semantic
// C++), and compares the return value plus every byte of guest .data.
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/actor_fields.hpp"
#include "../../../tools/lab_io.hpp"
#include <algorithm>
#include <iostream>

using namespace fsb::core;

int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        const auto input = fsb::lab::read(argv[1]), reference = fsb::lab::read(argv[2]);
        const auto word = [](const auto& bytes, std::size_t& cursor) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes.at(cursor++)) << (i * 8);
            return value;
        };
        Memory initial;
        std::size_t cursor = 8;
        const auto regions = word(input, cursor);
        for (unsigned i = 0; i < regions; ++i) {
            const auto base = word(input, cursor), size = word(input, cursor);
            initial.map(base, {input.begin() + cursor, input.begin() + cursor + size}, true);
            cursor += size;
        }
        if (reference.size() < 12 ||
            std::string(reference.begin(), reference.begin() + 8) != std::string("FSBACT1\0", 8))
            throw std::runtime_error("invalid actor_core oracle");
        cursor = 8;
        const auto cases = word(reference, cursor);
        unsigned failed = 0, logs = 0, cues = 0;
        std::uint64_t compared = 0;
        for (unsigned index = 0; index < cases; ++index) {
            Memory memory = initial;
            RecoveredBattle code(memory);
            // The save slot pair is the one place the CRT stream and the heap
            // are substituted, so the entries that probe a slot header on their
            // own keep running the real path. While it is on, a read is served
            // from the same stand-in file the oracle serves, whose byte at any
            // offset is a pure function of that offset, and every call is folded
            // into the same digest, so the order, the sizes and the bytes the
            // two halves move are all part of the comparison.
            bool save_layer = false;
            std::uint32_t file_position = 0, save_heap = 0, file_calls = 0;
            const auto stand_in_byte = [](std::uint32_t offset) -> std::uint8_t {
                // The layout puts the two length prefixes at 0x1bfc and 0x1c00;
                // the second is the fixed size the event block insists on.
                if (offset >= 0x1bfc && offset < 0x1c00) return 0;
                if (offset >= 0x1c00 && offset < 0x1c04)
                    return std::uint8_t(0x2a4u >> (8 * (offset - 0x1c00)));
                return offset % 4 ? 0 : std::uint8_t((offset / 4 * 13 + 7) % 10);
            };
            const auto delete_file = memory.read(0x8593a4);
            const auto file_service = [&](Address target, RecoveredBattle& call) {
                if (!save_layer) return false;
                const bool transfer = target == 0x498870 || target == 0x498690;
                if (!transfer && target != 0x498670 && target != 0x4985c0 &&
                    target != 0x499340 && target != 0x40112a && target != 0x4972b0 &&
                    target != delete_file)
                    return false;
                const auto ordinal = file_calls++;
                std::vector<std::uint32_t> folded;
                std::uint32_t answer = 0, popped = 0;
                if (transfer) {
                    const auto at = call.argument(0), count = call.argument(1),
                               bytes = call.argument(2);
                    const auto total = count * bytes;
                    std::vector<std::uint8_t> payload(total);
                    if (target == 0x498690) {
                        for (std::uint32_t i = 0; i < total; ++i)
                            payload[i] = stand_in_byte(file_position + i);
                        for (std::uint32_t i = 0; i < total; ++i)
                            call.write(at + i, payload[i], 1);
                    } else {
                        for (std::uint32_t i = 0; i < total; ++i)
                            payload[i] = std::uint8_t(call.read(at + i, 1));
                    }
                    file_position += total;
                    folded = {count, bytes};
                    for (std::size_t i = 0; i < payload.size(); i += 4) {
                        std::uint32_t packed = 0;
                        for (unsigned b = 0; b < 4 && i + b < payload.size(); ++b)
                            packed |= std::uint32_t(payload[i + b]) << (8 * b);
                        folded.push_back(packed);
                    }
                    answer = count;
                } else {
                    const unsigned taken =
                        (target == 0x498670 || target == 0x499340) ? 2 : 1;
                    for (unsigned i = 0; i < taken; ++i) folded.push_back(call.argument(i));
                    if (target == 0x498670) { file_position = 0; answer = 0xabcdef; }
                    else if (target == 0x4985c0) { file_position = 0; }
                    // Every other rename is refused, so both rotate paths run.
                    else if (target == 0x499340) { answer = ordinal & 1; }
                    else if (target == 0x40112a) {
                        answer = save_heap;
                        save_heap = std::min(save_heap + ((call.argument(0) + 15) & ~15u),
                                             0x74c000u + 0x8000u - 0x400u);
                    } else if (target == delete_file) {
                        answer = 1;
                        popped = 4;  // DeleteFileA is stdcall.
                    }
                }
                auto digest = memory.read(0x6df900) ^ target;
                for (const auto value : folded) digest = (digest * 16777619u) ^ value;
                memory.write(0x6df900, digest);
                memory.write(0x6df904, memory.read(0x6df904) + 1);
                call.result(answer, popped);
                return true;
            };
            // The boundaries the reconstructions call out to. 42feb9 and 42ff9b
            // are the integration session's; 401a02 is substituted exactly as
            // the oracle substituted it, returning zero and popping nothing.
            code.service = [&](Address target, RecoveredBattle& call) {
                if (file_service(target, call)) return true;
                switch (target) {
                case 0x42feb9: call.result(lookup_actor(memory, call.argument(0)), 4); return true;
                case 0x42ff9b:
                    if (call.argument(1) > 1) throw Fault(target, "visibility mode must be zero or one");
                    Actors(memory).visible(call.argument(0), call.argument(1) != 0);
                    call.result(0, 8); return true;
                case 0x401a02: case 0x401a82: case 0x401ad8: ++logs; call.result(0); return true;
                // The motion and frame boundaries the integration session owns.
                case 0x45c55c: {
                    const auto step = Actors(memory).step_motion(call.argument(0));
                    if (step.sound) ++cues;
                    call.result(step.active ? 1 : 0, 4); return true;
                }
                case 0x45cc1b: {
                    Actors actors(memory);
                    if (memory.read(0x80465c) == 9) actors.resolve_battle_frame(call.argument(0));
                    else actors.resolve_field_frame(call.argument(0));
                    call.result(0, 4); return true;
                }
                // Substituted in the oracle too: the cue is recorded, not played.
                case 0x435373: ++cues; call.result(0, 4); return true;
                // The CIM blob's heap and clock, with the oracle's fixed values.
                case 0x4974a0: call.result(0x6df000); return true;
                case 0x8594a0: call.result(0x12345678); return true;
                // The drawing leaves, substituted exactly as the oracle does:
                // fold each request into the digest at 0x6df900 so the ordinary
                // whole-.data comparison covers the draw sequence.
                // 4544cf is a service boundary with no port body, so it is
                // substituted here and in the oracle; 45c14f's other three
                // calls have bodies and run for real on both sides.
                // 46018b's eleven service boundaries join the same digest.
                case 0x404c56: case 0x433794: case 0x433788: case 0x40bb53:
                case 0x457e1c: case 0x457191: case 0x4320f6: case 0x43208d:
                case 0x404bb0: case 0x4335c0: case 0x457458:
                case 0x40587f: case 0x4067ec: case 0x405dd6: case 0x4544cf: case 0x434584: {
                    unsigned taken = 6, popped = 0;
                    switch (target) {
                    case 0x40587f: taken = 5; popped = 0x14; break;
                    case 0x405dd6: popped = 0x18; break;
                    case 0x4544cf: taken = 4; popped = 0x10; break;
                    case 0x434584: taken = 2; popped = 8; break;
                    case 0x404c56: case 0x404bb0: case 0x4335c0: taken = 3; popped = 0xc; break;
                    case 0x433794: taken = 2; popped = 8; break;
                    case 0x433788: case 0x40bb53: case 0x457e1c: case 0x457191:
                        taken = 0; popped = 0; break;
                    case 0x4320f6: case 0x43208d: case 0x457458: taken = 1; popped = 4; break;
                    default: break;
                    }
                    if (target == 0x4067ec) {
                        // Varargs: only these two formats carry a value.
                        const auto format = call.argument(4);
                        taken = (format == 0x5b3504 || format == 0x5d2250) ? 6 : 5;
                    }
                    auto digest = memory.read(0x6df900) ^ target;
                    for (unsigned i = 0; i < taken; ++i) {
                        // Argument 4 of 405dd6 points at the caller's own frame,
                        // so fold the rectangle rather than the pointer.
                        const auto value = (target == 0x405dd6 && i == 4) ? 0 : call.argument(i);
                        digest = (digest * 16777619u) ^ value;
                    }
                    if (target == 0x405dd6)
                        for (unsigned i = 0; i < 4; ++i)
                            digest = (digest * 16777619u) ^ call.read(call.argument(4) + i * 4);
                    memory.write(0x6df900, digest);
                    memory.write(0x6df904, memory.read(0x6df904) + 1);
                    call.result(0, popped);
                    return true;
                }
                default: return false;
                }
            };
            const auto entry = word(reference, cursor), mask = word(reference, cursor),
                       argc_ = word(reference, cursor);
            std::vector<std::uint32_t> args;
            for (unsigned i = 0; i < argc_; ++i) args.push_back(word(reference, cursor));
            const auto writes = word(reference, cursor);
            for (unsigned i = 0; i < writes; ++i) {
                const auto at = word(reference, cursor), width = word(reference, cursor),
                           value = word(reference, cursor);
                memory.write(at, value, width);
            }
            auto expected = memory.bytes(0x4a5000, 3882100);
            const auto expected_return = word(reference, cursor), changes = word(reference, cursor);
            for (unsigned i = 0; i < changes; ++i) {
                const auto at = word(reference, cursor);
                expected.at(at - 0x4a5000) = reference.at(cursor++);
            }
            save_layer = entry == 0x460e58 || entry == 0x46118e;
            file_position = file_calls = 0;
            save_heap = 0x74c000;
            std::optional<Address> next_allocation;
            if(entry==0x460e58 || entry==0x46118e){
                auto probe=memory;next_allocation=probe.allocate_zeroed(16);
            }
            const auto actual_return = code.invoke(entry, args) & mask;
            if(next_allocation && memory.allocate_zeroed(16)!=*next_allocation)
                throw std::runtime_error("save-slot ABI scratch consumed game heap allocation");
            const auto actual = memory.bytes(0x4a5000, expected.size());
            compared += actual.size();
            unsigned mismatches = 0;
            for (std::size_t i = 0; i < actual.size(); ++i)
                if (actual[i] != expected[i] && mismatches++ < 8)
                    std::cerr << "case=" << index << " entry=0x" << std::hex << entry
                              << " address=0x" << (0x4a5000 + i) << " actual=" << unsigned(actual[i])
                              << " expected=" << unsigned(expected[i]) << std::dec << '\n';
            if (actual_return != expected_return)
                std::cerr << "case=" << index << " entry=0x" << std::hex << entry << std::dec
                          << " return=" << actual_return << " expected=" << expected_return << '\n';
            if (mismatches || actual_return != expected_return) {
                if (++failed >= 5) break;
            }
        }
        std::cout << "actor_core_cases=" << cases << " failed=" << failed
                  << " guest_bytes_compared=" << compared << " error_log_calls=" << logs
                  << " sound_cues=" << cues << '\n';
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
