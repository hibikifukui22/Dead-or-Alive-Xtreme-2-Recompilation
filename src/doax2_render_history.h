#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace doax2 {
// Address tuples are observations, NOT persistent object identities. Even a
// matching pair must not be interpolated until allocation lifetimes are known.
struct TransformKey {
  uint32_t object, type, source, model, selector;
  bool operator==(const TransformKey&) const = default;
};
using MatrixBits = std::array<uint32_t, 16>;

template <size_t Capacity>
class RenderHistory {
  static_assert(Capacity > 0);
 public:
  struct Entry {
    TransformKey key{};
    MatrixBits matrix{};
    bool ambiguous = false;
  };
  struct Stats {
    size_t unique = 0, repeated = 0, ambiguous = 0, dropped = 0;
    size_t adjacent = 0, changed = 0;
    bool complete = true;
  };
  void Begin() {
    // Abandoned/nested capture must not become a previous-frame endpoint.
    if (active_) previous_count_ = 0;
    current_count_ = 0;
    stats_ = {};
    active_ = true;
  }
  bool Observe(const TransformKey& key, const MatrixBits& matrix) {
    if (!active_) return false;
    for (size_t i = 0; i < current_count_; ++i) {
      auto& entry = current_[i];
      if (entry.key != key) continue;
      ++stats_.repeated;
      if (entry.matrix != matrix && !entry.ambiguous) {
        entry.ambiguous = true;
        ++stats_.ambiguous;
      }
      return !entry.ambiguous;
    }
    if (current_count_ == Capacity) {
      ++stats_.dropped;
      stats_.complete = false;
      return false;
    }
    current_[current_count_++] = {key, matrix, false};
    return true;
  }
  Stats End() {
    if (!active_) return {};
    stats_.unique = current_count_;
    // An overflow invalidates the whole endpoint, not just the excess entries.
    if (stats_.complete) {
      for (size_t i = 0; i < current_count_; ++i) {
        const auto& entry = current_[i];
        if (entry.ambiguous) continue;
        for (size_t j = 0; j < previous_count_; ++j) {
          const auto& old = previous_[j];
          if (old.ambiguous || old.key != entry.key) continue;
          ++stats_.adjacent;
          if (old.matrix != entry.matrix) ++stats_.changed;
          break;
        }
      }
      previous_.swap(current_);
      previous_count_ = current_count_;
    } else {
      previous_count_ = 0;
    }
    active_ = false;
    return stats_;
  }
  bool active() const { return active_; }
 private:
  std::array<Entry, Capacity> previous_{}, current_{};
  size_t previous_count_ = 0, current_count_ = 0;
  bool active_ = false;
  Stats stats_{};
};
}  // namespace doax2
