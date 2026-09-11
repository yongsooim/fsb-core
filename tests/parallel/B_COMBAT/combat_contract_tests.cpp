// Replays a B_COMBAT original-execution fixture through the product entry
// point. Before registration this exercises the generated body; afterwards the
// same expectations exercise the semantic reconstruction, so one baseline
// covers both. Expectations come from the executable, never from this code.
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actors.hpp"
#include "../../../tools/lab_io.hpp"
#include <iostream>
#include "pointer_contract.hpp"

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
        if (reference.size() < 12 || std::string(reference.begin(), reference.begin() + 8) != std::string("FSBACT1\0", 8))
            throw std::runtime_error("invalid combat oracle");
        cursor = 8;
        const auto cases = word(reference, cursor);
        Memory allocation_probe = initial;
        const auto first_allocation = allocation_probe.allocate_zeroed(16);
        unsigned failed = 0, pointer_cases = 0;
        std::uint64_t compared = 0;
        for (unsigned index = 0; index < cases; ++index) {
            Memory memory = initial;
            RecoveredBattle code(memory);
            // The same two services the fixture substitutes: heap release and
            // the music transition. Both are outside the entries under test, so
            // neither side runs them and the .data comparison stays honest.
            code.service=[&memory](Address target,RecoveredBattle& call){
                // Mirrors Battle::service for the actor visual boundary, which
                // reconstructed callees reach. Its sound request is a host
                // effect and never appears in the compared guest data.
                if(target==0x45c526){Actors(memory).tick_default_visual(call.argument(0));call.result(0,4);return true;}
                if(target==0x4972b0){call.result(0);return true;}          // cdecl free
                if(target==0x4337f4){call.result(0,24);return true;}       // stdcall music
                if(target==0x435373||target==0x4353cb){call.result(0,4);return true;} // sound cue
                // Mirrors Battle::service for the original's formatted print.
                if(target==0x8594c0){
                    const auto out=call.format_text(call.argument(1),call.r[4]+12);
                    for(unsigned i=0;i<=out.size();++i)
                        call.write(call.argument(0)+i,i<out.size()?std::uint8_t(out[i]):0,1);
                    call.result(unsigned(out.size()));return true;
                }
                // Mirrors Battle::service: placing an actor on a grid tile.
                if(target==0x45d774){set_actor_tile_position(memory,call.argument(0),signed32(call.argument(1)),
                    signed32(call.argument(2)),signed32(call.argument(3)));call.result(0,16);return true;}
                if(target==0x4320f6||target==0x43208d){call.result(0,4);return true;} // viewport
                // Palette device boundaries reached through the screen flash.
                if(target==0x404c56){call.result(0,12);return true;}
                if(target==0x404dbb||target==0x40536b){call.result(0,16);return true;}
                if(target==0x405572){call.result(0);return true;}
                if(target==0x401d66){                                      // stdcall transition
                    // Publish the rectangle the service was handed, so the
                    // comparison covers it the way the fixture recorded it.
                    for(unsigned i=0;i<4;++i)call.write(0x8059f0+i*4,call.read(call.argument(1)+i*4));
                    call.write(0x805a00,call.argument(0));
                    call.write(0x805a04,call.argument(2));
                    call.write(0x805a08,call.argument(3));
                    call.result(0,16);return true;
                }
                return false;
            };
            const auto entry = word(reference, cursor), mask = word(reference, cursor), count = word(reference, cursor);
            std::vector<std::uint32_t> args;
            for (unsigned i = 0; i < count; ++i) args.push_back(word(reference, cursor));
            const auto writes = word(reference, cursor);
            for (unsigned i = 0; i < writes; ++i) {
                const auto address = word(reference, cursor), width = word(reference, cursor), value = word(reference, cursor);
                memory.write(address, value, width);
            }
            auto expected = memory.bytes(0x4a5000, 3882100);
            const auto expected_return = word(reference, cursor), changes = word(reference, cursor);
            for (unsigned i = 0; i < changes; ++i) {
                const auto address = word(reference, cursor);
                expected.at(address - 0x4a5000) = reference.at(cursor++);
            }
            if (entry == 0x44a513) {
                // 44a623 passes its frame locals. Replay the same original
                // expectations with the two outputs in the legacy call stack.
                Memory stack_memory = memory;
                RecoveredBattle stack_code(stack_memory);
                auto stack_args = args;
                constexpr Address out_group = 0x0100ef90, out_index = out_group + 4;
                stack_args[2] = out_group; stack_args[3] = out_index;
                stack_code.write(out_group, memory.read(args[2]));
                stack_code.write(out_index, memory.read(args[3]));
                auto stack_expected = expected;
                const auto expected_word = [&](Address at) {
                    std::size_t offset = at - 0x4a5000;
                    return word(expected, offset);
                };
                const auto returned = stack_code.invoke(entry, stack_args) & mask;
                if (returned != expected_return || stack_code.read(out_group) != expected_word(args[2]) ||
                    stack_code.read(out_index) != expected_word(args[3]))
                    throw std::runtime_error("battle placement stack outputs differ from the original");
                // Original output locations are untouched when outputs go to stack.
                for (unsigned output : {2u, 3u})
                    for (unsigned byte = 0; byte < 4; ++byte)
                        stack_expected.at(args[output] - 0x4a5000 + byte) = memory.read(args[output] + byte, 1);
                if (stack_memory.bytes(0x4a5000, stack_expected.size()) != stack_expected)
                    throw std::runtime_error("battle placement changed data when outputs moved to stack");
            }
            pointer_cases += check_pointer_domains(memory,entry,args,expected,mask,expected_return);
            std::uint32_t actual_return = 0;
            try { actual_return = code.invoke(entry, args) & mask; }
            catch (const std::exception& error) {
                std::cerr << "case=" << index << " entry=0x" << std::hex << entry << std::dec
                          << " threw: " << error.what() << '\n';
                throw;
            }
            // The original camera helpers use stack locals, not the game heap.
            // A freed temporary can still shift all later resource handles.
            if ((entry == 0x462150 || entry == 0x462203 || entry == 0x4647ad || entry == 0x464925) &&
                memory.allocate_zeroed(16) != first_allocation)
                throw std::runtime_error("temporary ABI buffer consumed a game heap allocation");
            const auto actual = memory.bytes(0x4a5000, expected.size());
            compared += actual.size();
            unsigned mismatches = 0;
            for (std::size_t i = 0; i < actual.size(); ++i) if (actual[i] != expected[i]) {
                if (mismatches++ < 8)
                    std::cerr << "case=" << index << " entry=0x" << std::hex << entry
                              << " address=0x" << (0x4a5000 + i) << " actual=" << unsigned(actual[i])
                              << " expected=" << unsigned(expected[i]) << std::dec << '\n';
            }
            if (actual_return != expected_return)
                std::cerr << "case=" << index << " entry=0x" << std::hex << entry << std::dec
                          << " return=" << actual_return << " expected=" << expected_return << '\n';
            if (mismatches || actual_return != expected_return) { ++failed; if (failed >= 5) break; }
        }
        std::cout << "combat_cases=" << cases << " failed=" << failed
                  << " pointer_domain_cases=" << pointer_cases << " guest_bytes_compared=" << compared << '\n';
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
