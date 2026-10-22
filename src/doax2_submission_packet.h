#pragma once
#include "doax2_render_history.h"

namespace doax2 {
// Owned output of one hierarchy submission traversal. No spring state, child
// pointers or guest resource contents are retained. Address labels are not
// allocation identities and must not be used for cross-frame interpolation yet.
template <size_t Capacity>
class SubmissionPacket {
 public:
  struct Item { uint32_t node, target; MatrixBits matrix; };
  void Begin(uint32_t manager) {
    valid_ = !active_;
    active_ = true;
    manager_ = manager;
    size_ = 0;
  }
  bool Add(uint32_t node, uint32_t target, const MatrixBits& matrix) {
    if (!active_ || !valid_) return false;
    for (size_t i = 0; i < size_; ++i) {
      if (items_[i].node != node) continue;
      // A repeated node makes traversal/order ambiguous, even if identical.
      valid_ = false;
      return false;
    }
    if (size_ == Capacity) { valid_ = false; return false; }
    items_[size_++] = {node, target, matrix};
    return true;
  }
  bool End() { const bool ok = active_ && valid_; active_ = false; return ok; }
  bool active() const { return active_; }
  size_t size() const { return size_; }
  uint32_t manager() const { return manager_; }
  const Item& item(size_t index) const { return items_[index]; }
 private:
  std::array<Item, Capacity> items_{};
  size_t size_ = 0;
  uint32_t manager_ = 0;
  bool active_ = false, valid_ = false;
};
}  // namespace doax2
