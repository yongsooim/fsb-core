// Compares SkillCallbacks against reference/parallel/D_EFFECTS/skill-callbacks-x86.bin,
// which tools/parallel/D_EFFECTS/prepare_skill_callback_reference.py recorded by
// running the original callbacks. Expected values never come from this code.
#include "fsb_core/visual_effects/skill_callbacks.hpp"
#include "../../../tools/lab_io.hpp"
#include <iostream>
#include <vector>

using namespace fsb::core;
using namespace fsb::core::visual_effects;

namespace {
constexpr Address play_cue_service = 0x435373;
constexpr std::uint32_t result_base = 0x5eed0000;
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        const auto initial = Memory::from_pe32(fsb::lab::read(argv[1]));
        const auto bytes = fsb::lab::read(argv[2]);
        std::size_t cursor = 8;
        if (bytes.size() < 12 || std::string(bytes.begin(), bytes.begin() + 8) != std::string("FSBDSK2\0", 8))
            throw std::runtime_error("bad skill callback fixture");
        const auto word = [&]() {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes.at(cursor++)) << (8 * i);
            return value;
        };
        using Call = std::pair<Address, std::vector<std::uint32_t>>;
        const auto count = word();
        for (unsigned index = 0; index < count; ++index) {
            const auto entry = word(), sequence = word();
            word();  // The phase is applied through the setup writes below.
            const auto wanted = word();
            std::vector<Call> expected_calls;
            for (auto calls = word(); calls; --calls) {
                const auto routine = word();
                std::vector<std::uint32_t> args;
                for (auto n = word(); n; --n) args.push_back(word());
                expected_calls.emplace_back(routine, std::move(args));
            }

            auto memory = initial;
            for (auto writes = word(); writes; --writes) { const auto at = word(); memory.write(at, word()); }
            auto expected = memory.bytes(0x4a5000, 3882100);
            for (auto changes = word(); changes; --changes) {
                const auto at = word();
                expected.at(at - 0x4a5000) = bytes.at(cursor++);
            }

            SkillCallbacks callbacks(memory);
            std::vector<Call> calls;
            callbacks.play_cue = [&](unsigned cue) {
                calls.emplace_back(play_cue_service, std::vector<std::uint32_t>{cue});
                return result_base | (play_cue_service & 0xffff);
            };
            for (const auto routine : SkillCallbacks::routines()) {
                const auto pushed = pushed_arguments(*SkillCallbacks::find(entry));
                callbacks.bind(routine, [&, routine, pushed](Address object, std::int32_t first, std::int32_t second) {
                    std::vector<std::uint32_t> args{object, std::uint32_t(first), std::uint32_t(second)};
                    args.resize(pushed);
                    calls.emplace_back(routine, std::move(args));
                    return result_base | (routine & 0xffff);
                });
            }
            const auto result = callbacks.invoke(entry, sequence);

            if (calls != expected_calls || result != wanted ||
                memory.bytes(0x4a5000, expected.size()) != expected)
                throw std::runtime_error("skill callback case " + std::to_string(index) +
                                         " entry=" + std::to_string(entry) + " mismatch");
        }
        if (cursor != bytes.size()) throw std::runtime_error("unconsumed skill callback fixture");
        std::cout << "skill_callback_original_cases=" << count << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
