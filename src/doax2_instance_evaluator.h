#pragma once
#include "doax2_procedural_matrix.h"
#include <algorithm>
#include <cstdint>

namespace doax2 {
using FloatMatrix = std::array<float, 16>;
inline bool CanSubstituteInstance(uint32_t caller, uint32_t parent,
                                  uint32_t stack, uint32_t output,
                                  bool supported, bool mismatch) {
  return supported && !mismatch && caller == 0x825B016C && parent == 0x825B0050 &&
         stack <= UINT32_MAX - 784u && output == stack + 720u;
}
// Isolated counterpart of 825AF158: inverse(B) * procedural(phase) * B.
// Consumes owned values only. No guest state, timestep or resource operations.
inline bool EvaluateInstanceMatrix(const FloatMatrix& basis, float phase,
                                   float phase_stride, uint32_t index,
                                   const std::array<float, 4>& parameters,
                                   FloatMatrix& output) {
  if (!std::isfinite(phase_stride)) return false;
  FloatMatrix shear;
  if (!EvaluateProceduralMatrix(std::fma(phase_stride, static_cast<float>(index), phase),
                                parameters, shear)) return false;
  double augmented[4][8]{};
  double scale = 0;
  for (size_t r = 0; r < 4; ++r) {
    for (size_t c = 0; c < 4; ++c) {
      if (!std::isfinite(basis[r * 4 + c])) return false;
      augmented[r][c] = basis[r * 4 + c];
      scale = std::max(scale, std::abs(augmented[r][c]));
    }
    augmented[r][r + 4] = 1;
  }
  if (!scale) return false;
  for (size_t c = 0; c < 4; ++c) {
    size_t pivot = c;
    for (size_t r = c + 1; r < 4; ++r)
      if (std::abs(augmented[r][c]) > std::abs(augmented[pivot][c])) pivot = r;
    // Reject poorly resolved pivots rather than inventing a fallback transform.
    if (std::abs(augmented[pivot][c]) <= scale * 1e-10) return false;
    for (size_t j = 0; j < 8; ++j) std::swap(augmented[c][j], augmented[pivot][j]);
    const double divisor = augmented[c][c];
    for (double& value : augmented[c]) value /= divisor;
    for (size_t r = 0; r < 4; ++r) {
      if (r == c) continue;
      const double factor = augmented[r][c];
      for (size_t j = 0; j < 8; ++j) augmented[r][j] -= factor * augmented[c][j];
    }
  }
  double product[4][4]{};
  for (size_t r = 0; r < 4; ++r)
    for (size_t c = 0; c < 4; ++c)
      for (size_t k = 0; k < 4; ++k)
        product[r][c] += double(shear[r * 4 + k]) * basis[k * 4 + c];
  FloatMatrix result;
  for (size_t r = 0; r < 4; ++r)
    for (size_t c = 0; c < 4; ++c) {
      double value = 0;
      for (size_t k = 0; k < 4; ++k) value += augmented[r][k + 4] * product[k][c];
      result[r * 4 + c] = static_cast<float>(value);
      if (!std::isfinite(result[r * 4 + c])) return false;
    }
  output = result;
  return true;
}
}  // namespace doax2
