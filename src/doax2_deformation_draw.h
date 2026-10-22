#pragma once
#include "doax2_deformation_recipe.h"
namespace doax2 {
// Device constant dirty groups: register49 -> bit51, register105 -> bit37.
inline constexpr uint64_t kDeformationDirtyMask = (uint64_t{1} << 51) | (uint64_t{1} << 37);
inline constexpr uint32_t DeformationWeightOffset(size_t i) {
  return i < 4 ? 2576u + 4u * static_cast<uint32_t>(i)
               : 3472u + 4u * static_cast<uint32_t>(i - 4);
}
// One normal wrapper invocation only. Resource values are numeric observations,
// not owned GPU references, and this packet must never be replayed.
struct DeformationDrawPacket {
  uint32_t object = 0;
  uint64_t ticket = 0;
  DeformationRecipe recipe{};
  std::array<uint32_t, 5> resources{};
  uint32_t mask = 0;
  bool valid = false;
  void Bind(uint32_t slot, uint32_t resource) {
    if (!valid || slot >= resources.size()) return;
    if (!resource || ((mask & (1u << slot)) && resources[slot] != resource)) {
      valid = false; return;
    }
    resources[slot] = resource;
    mask |= 1u << slot;
  }
  bool Ready(uint64_t current_ticket) const {
    return valid && ticket && ticket == current_ticket && mask == 31;
  }
  bool Matches(const std::array<float, 8>& actual) const {
    for (size_t i = 0; i < actual.size(); ++i)
      if (!std::isfinite(actual[i]) || !std::isfinite(recipe.weights[i]) ||
          std::abs(actual[i] - recipe.weights[i]) > 1e-6f * (1 + std::abs(actual[i]))) return false;
    return true;
  }
};
}
