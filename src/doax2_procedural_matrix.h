#pragma once
#include <array>
#include <cmath>

namespace doax2 {
// Host-only counterpart of 825B0E70. Phase is an input, never advanced here.
// This is one procedural shear component, not a skeleton/scene evaluator.
// Native sin is not claimed bit-identical to the title's polynomial routine.
inline bool EvaluateProceduralMatrix(float phase, const std::array<float, 4>& p,
                                     std::array<float, 16>& output) {
  if (!std::isfinite(phase)) return false;
  for (float x : p) if (!std::isfinite(x)) return false;
  const float angle0 = (p[0] * phase) * 2.0f;
  const float angle1 = (p[1] * phase) * 2.0f;
  if (!std::isfinite(angle0) || !std::isfinite(angle1)) return false;
  // Original sine helper has an exceptional-result branch at this magnitude.
  if (std::abs(angle0) >= 220000000.0f || std::abs(angle1) >= 220000000.0f) return false;
  // Keep the title's single-precision stages; the final accumulation is fused.
  const float wave0 = (static_cast<float>(std::sin(static_cast<double>(angle0))) + 1.0f) * 0.5f;
  const float wave1 = (static_cast<float>(std::sin(static_cast<double>(angle1))) + 1.0f) * 0.5f;
  const float first = wave0 * p[2];
  const float shear = std::fma(wave1, p[3], first);
  if (!std::isfinite(shear)) return false;
  output = {};
  output[0] = output[5] = output[10] = output[15] = 1.0f;
  output[6] = shear;
  return true;
}
}  // namespace doax2
