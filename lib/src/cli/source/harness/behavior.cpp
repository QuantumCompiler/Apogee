#include "harness/behavior.h"

#include <algorithm>

namespace apogee::harness {

bool ModelBehavior::has_opener(std::string_view candidate) const noexcept {
    return std::ranges::any_of(tool_call_openers,
                               [candidate](const std::string& o) { return o == candidate; });
}

}  // namespace apogee::harness
