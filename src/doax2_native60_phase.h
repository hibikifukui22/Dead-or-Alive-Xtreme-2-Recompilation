#pragma once

#include <cmath>
#include <optional>

namespace doax2 {

// Correct one measured per-call phase advance after the original guest
// function has run. The caller owns the manager field and decides whether the
// function was active; this does not change any shared/global game clock.
inline std::optional<float> HalfNativePhaseStep(float before,
                                                float native_after) {
  const float step = native_after - before;
  if (!std::isfinite(before) || !std::isfinite(native_after) ||
      !std::isfinite(step)) {
    return std::nullopt;
  }
  const float corrected = before + 0.5f * step;
  if (!std::isfinite(corrected)) return std::nullopt;
  return corrected;
}

// Only correct the known per-tick increment. A reset or another state change
// inside a larger guest update must never be mistaken for an ordinary step.
inline std::optional<float> HalfExpectedNativePhaseStep(
    float before, float native_after, float expected_step) {
  if (!std::isfinite(expected_step)) return std::nullopt;
  const float actual_step = native_after - before;
  if (!std::isfinite(actual_step) ||
      std::abs(actual_step - expected_step) >
          1e-4f * (1.0f + std::abs(expected_step))) {
    return std::nullopt;
  }
  return HalfNativePhaseStep(before, native_after);
}

}  // namespace doax2
