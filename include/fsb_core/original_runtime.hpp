#pragma once
#include "primitives.hpp"

namespace fsb::core {
class Runtime;
class RecoveredBattle;
namespace original_runtime {
// Original compact-object, message-queue, allocation and diagnostic ABI.
bool dispatch(Address entry, RecoveredBattle& call, Runtime& runtime);
}
}
