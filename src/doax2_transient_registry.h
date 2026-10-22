#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace doax2 {
// Local allocation tickets, NOT persistent model identities. No guest pointers
// are dereferenced or kept alive. Retirement invalidates every lookup.
template <size_t Capacity> class TransientRegistry {
 public:
  struct Entry { uint32_t object, slot, model, resource; uint64_t ticket; };
  bool Add(uint32_t object, uint32_t slot, uint32_t model, uint32_t resource) {
    if (!object || invalid_) return false;
    for (size_t i = 0; i < size_; ++i) {
      if (entries_[i].object == object) { invalid_ = true; return false; }
    }
    if (size_ == Capacity || ticket_ == std::numeric_limits<uint64_t>::max()) {
      invalid_ = true; return false;
    }
    entries_[size_++] = {object, slot, model, resource, ++ticket_};
    return true;
  }
  const Entry* Find(uint32_t object) const {
    if (invalid_) return nullptr;
    for (size_t i = 0; i < size_; ++i) if (entries_[i].object == object) return &entries_[i];
    return nullptr;
  }
  void Retire() { size_ = 0; invalid_ = ticket_ == std::numeric_limits<uint64_t>::max(); }
 private:
  std::array<Entry, Capacity> entries_{};
  size_t size_ = 0;
  uint64_t ticket_ = 0;
  bool invalid_ = false;
};
}
