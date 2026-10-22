#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace doax2 {

// One 30-Hz integer keyframe step for every two 60-Hz evaluations. Records
// remain independent, and replacing a record resets its cadence. A full table
// rejects new corrections rather than aliasing unrelated scene objects.
class Native60KeyframeCadence {
 public:
  std::optional<uint32_t> Next(uint32_t manager, uint32_t record,
                               uint32_t native_step) {
    if (!manager || !record || native_step != 1) return std::nullopt;
    Slot* empty = nullptr;
    for (auto& slot : slots_) {
      if (slot.manager == manager) {
        if (slot.record != record) {
          slot.record = record;
          slot.advance_next = true;
        }
        return Consume(slot);
      }
      if (!slot.manager && !empty) empty = &slot;
    }
    if (!empty) return std::nullopt;
    *empty = Slot{manager, record, true};
    return Consume(*empty);
  }

 private:
  struct Slot {
    uint32_t manager = 0;
    uint32_t record = 0;
    bool advance_next = true;
  };
  static uint32_t Consume(Slot& slot) {
    const uint32_t step = slot.advance_next ? 1u : 0u;
    slot.advance_next = !slot.advance_next;
    return step;
  }
  std::array<Slot, 32> slots_{};
};

// The preview scene has four 60-byte counter records. Keep their half-rate
// cadences independent; an unknown record is never assigned to another slot.
class Native60SceneCounterCadence {
 public:
  std::optional<bool> Advance(uint32_t record) {
    if (!record) return std::nullopt;
    Slot* empty = nullptr;
    for (auto& slot : slots_) {
      if (slot.record == record) return Consume(slot);
      if (!slot.record && !empty) empty = &slot;
    }
    if (!empty) return std::nullopt;
    *empty = Slot{record, true};
    return Consume(*empty);
  }

 private:
  struct Slot {
    uint32_t record = 0;
    bool advance_next = true;
  };
  static bool Consume(Slot& slot) {
    const bool advance = slot.advance_next;
    slot.advance_next = !slot.advance_next;
    return advance;
  }
  std::array<Slot, 8> slots_{};
};

}  // namespace doax2
