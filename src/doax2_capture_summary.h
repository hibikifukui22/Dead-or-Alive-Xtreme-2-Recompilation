#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace doax2 {
// Cross-thread call-edge coverage, NOT a synchronized scene snapshot. Guest
// addresses are numbers only: never retain or dereference guest pointers here.
template <size_t Capacity>
class CaptureSummary {
 public:
  struct Event {
    uint32_t function = 0, caller = 0, parent = 0;
    uint64_t thread = 0, epoch = 0;
    uint32_t a0 = 0, a1 = 0, a2 = 0, result = 0;
  };
  struct Entry { Event last{}; uint64_t calls = 0, first_epoch = 0; };
  struct Snapshot {
    std::array<Entry, Capacity> entries{};
    size_t size = 0;
    uint64_t events = 0, dropped = 0;
  };
  void Observe(const Event& event) {
    std::lock_guard lock(mutex_);
    ++data_.events;
    for (size_t i = 0; i < data_.size; ++i) {
      auto& entry = data_.entries[i];
      const auto& old = entry.last;
      if (old.function == event.function && old.caller == event.caller &&
          old.parent == event.parent && old.thread == event.thread) {
        entry.last = event;
        ++entry.calls;
        return;
      }
    }
    if (data_.size == Capacity) { ++data_.dropped; return; }
    data_.entries[data_.size++] = {event, 1, event.epoch};
  }
  Snapshot Read() const {
    std::lock_guard lock(mutex_);
    return data_;
  }
 private:
  mutable std::mutex mutex_;
  Snapshot data_{};
};
}  // namespace doax2
