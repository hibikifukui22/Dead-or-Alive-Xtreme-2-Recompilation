#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace doax2 {
// 8248A348's render-side selection/weight calculation only. Does not advance
// 8248A640's accumulator or modify its interpolated vectors. Not a skeleton.
struct DeformationRecipe {
  std::array<uint32_t, 4> indices{};
  std::array<float, 8> weights{};
};
struct DeformationEndpoint {
  // Must be supplied by proven lifetime tracking, NOT a slot/address/hash.
  uint64_t generation = 0;
  uint64_t simulation_frame = 0;
  std::array<float, 4> signed_inputs{}; // x, z, y0, y1
};
inline bool EvaluateDeformationRecipe(float x, float z, float y0, float y1,
                                      DeformationRecipe& result) {
  for (float v : {x, z, y0, y1}) if (!std::isfinite(v)) return false;
  DeformationRecipe value;
  value.indices[0] = x < 0 ? 4 : 1;
  value.indices[1] = z > 0 ? 2 : 3;
  // Preserve the original comparisons, including signed-zero behavior.
  x = x < 0 ? -x : x;
  z = z > 0 ? z : -z;
  const float sum = x + z;
  for (size_t i = 0; i < 2; ++i) {
    float y = i ? y1 : y0;
    value.indices[2 + i] = y < 0 ? 5 : 6;
    y = y < 0 ? -y : y;
    const float total = sum + y;
    value.weights[4*i] = 1.0f - total; // Original does not clamp negative weights.
    value.weights[4*i+1] = x;
    value.weights[4*i+2] = z;
    value.weights[4*i+3] = y;
  }
  for (float v : value.weights) if (!std::isfinite(v)) return false;
  result = value;
  return true;
}

// Host-only midpoint construction. No guest pointers, live-state writes or
// inference of an object's lifetime. The caller must establish endpoint identity.
inline bool InterpolateDeformationRecipe(const DeformationEndpoint& previous,
                                         const DeformationEndpoint& current,
                                         float alpha, DeformationRecipe& result) {
  if (!previous.generation || previous.generation != current.generation ||
      previous.simulation_frame == std::numeric_limits<uint64_t>::max() ||
      current.simulation_frame != previous.simulation_frame + 1 ||
      !std::isfinite(alpha) || alpha < 0 || alpha > 1) return false;
  std::array<float, 4> inputs;
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (!std::isfinite(previous.signed_inputs[i]) ||
        !std::isfinite(current.signed_inputs[i])) return false;
    inputs[i] = std::lerp(previous.signed_inputs[i], current.signed_inputs[i], alpha);
  }
  return EvaluateDeformationRecipe(inputs[0], inputs[1], inputs[2], inputs[3], result);
}

using DeformationVectors = std::array<std::array<float, 4>, 7>;
// CPU consumer correspondence: 824B78E0 uses indices 0,1,2 / weights0..3;
// 824B7990 uses indices 0,1,3 / weights4..7, both with base vector zero.
// This provides a host numerical oracle, not a replacement for GPU skinning.
inline bool EvaluateDeformationVector(const DeformationRecipe& recipe,
                                     const DeformationVectors& vectors,
                                     size_t bank, std::array<float, 4>& result) {
  if (bank > 1) return false;
  const std::array<uint32_t, 4> indices{0, recipe.indices[0], recipe.indices[1], recipe.indices[2 + bank]};
  for (uint32_t i : indices) if (i >= vectors.size()) return false;
  std::array<float, 4> value{};
  for (size_t component = 0; component < 4; ++component) {
    std::array<float, 4> products;
    for (size_t i = 0; i < 4; ++i) {
      const float weight = recipe.weights[4 * bank + i];
      const float input = vectors[indices[i]][component];
      if (!std::isfinite(weight) || !std::isfinite(input)) return false;
      products[i] = weight * input;
    }
    value[component] = ((products[0] + products[1]) + products[2]) + products[3];
    if (!std::isfinite(value[component])) return false;
  }
  result = value;
  return true;
}
}
