// Compiled with -fno-exceptions -fno-rtti -Werror to keep core/engine usable on
// microcontrollers. Instantiates the render path so templates are checked too.
#include <adaptive/engine.hpp>

void portabilityCheck(adaptive::Engine& engine, float* out, uint32_t n) {
    engine.render(out, n);
}
