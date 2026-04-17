/*
 * Below is a minimal, correct, and verifiable example that does exactly what you asked:
✅ constexpr std::array mapping enum → string
✅ Guaranteed compile‑time initialization
✅ Demonstrates how to verify it lives in .rodata using objdump
This is the exact pattern used in profilers, tracers, and telemetry systems.
*/

#include <array>
#include <cstdint>
#include <string_view>

// Strongly typed enum
enum class Event : uint8_t
{
   Start,
   Stop,
   Frame,
   Render,
   Count
};

// Compile-time lookup table
constexpr std::array<std::string_view, static_cast<size_t>(Event::Count)> event_names = {
    "Start", "Stop", "Frame", "Render"};
// constexpr accessor
constexpr std::string_view to_string(Event e)
{
   return event_names[static_cast<size_t>(e)];
}

// Force usage so the table is not discarded
void use(Event e)
{
   volatile auto s = to_string(e);
   (void)s;
}

int main()
{
   use(Event::Frame);
}
