#include "rage_wars_scheduler.hpp"

#include <ultramodern/ultramodern.hpp>

namespace rage_wars {
namespace {

bool allow_contextless_handoff(OSThread *current, OSThread *target) {
    return scheduler().permits_contextless_handoff(
            current != nullptr ? 1U : 0U, target != nullptr ? 1U : 0U);
}

} // namespace

void install_scheduler_service() {
    ultramodern::set_contextless_handoff_policy(allow_contextless_handoff);
}

} // namespace rage_wars
