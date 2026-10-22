#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace doax2 {
// Observational CPU call sequence, not an executable GPU stream or a lifetime
// guarantee. Arguments are labels only; this class never reads guest memory.
template <size_t Capacity> struct PassTrace {
  struct Event {
    uint32_t function, caller, a0, a1, a2;
    uint64_t draws_before;
    std::array<uint32_t, 5> r6_to_r10;
    uint64_t inline_before;
  };
  std::array<Event, Capacity> events{};
  size_t count = 0;
  uint64_t draws = 0, inline_draws = 0, dropped = 0;
  bool active = false;
  void Begin() { count = 0; draws = 0; inline_draws = 0; dropped = active ? 1 : 0; active = true; }
  void Draw() { if (active) ++draws; }
  // Counts begin-inline calls, not successful allocations or GPU completions.
  void InlineDraw() { if (active) ++inline_draws; }
  void Add(uint32_t fn, uint32_t caller, uint32_t a0, uint32_t a1, uint32_t a2,
           std::array<uint32_t, 5> extra = {}) {
    if (!active) return;
    if (count == Capacity) { ++dropped; return; }
    events[count++] = {fn, caller, a0, a1, a2, draws, extra, inline_draws};
  }
  void End() { active = false; }
};
}
