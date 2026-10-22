#include "../src/doax2_native60_phase.h"
#include "../src/doax2_native60_keyframe.h"

#include <cstdlib>
#include <iostream>
#include <limits>

static void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "native60 phase test failed: " << message << '\n';
    std::exit(1);
  }
}

int main() {
  const auto first = doax2::HalfNativePhaseStep(0.0f, 1.0f);
  Check(first && *first == 0.5f, "first 60-Hz phase step");
  const auto second = doax2::HalfNativePhaseStep(*first, *first + 1.0f);
  Check(second && *second == 1.0f, "two 60-Hz steps equal one native 30-Hz step");
  Check(!doax2::HalfNativePhaseStep(0.0f, std::numeric_limits<float>::infinity()),
        "nonfinite guest output rejected");
  Check(!doax2::HalfNativePhaseStep(std::numeric_limits<float>::quiet_NaN(), 1.0f),
        "nonfinite guest input rejected");
  const auto expected = doax2::HalfExpectedNativePhaseStep(10.0f, 11.0f, 1.0f);
  Check(expected && *expected == 10.5f, "verified unit advance halved");
  Check(!doax2::HalfExpectedNativePhaseStep(10.0f, 0.0f, 1.0f),
        "phase reset is not corrected");
  Check(!doax2::HalfExpectedNativePhaseStep(10.0f, 12.0f, 1.0f),
        "unrelated larger advance is not corrected");
  const auto countdown = doax2::HalfExpectedNativePhaseStep(
      900.0f, 899.0f, -1.0f);
  Check(countdown && *countdown == 899.5f,
        "verified scene countdown decrement halved");
  Check(!doax2::HalfExpectedNativePhaseStep(1.0f, 900.0f, -1.0f),
        "scene countdown reset is not corrected");
  doax2::Native60KeyframeCadence cadence;
  Check(cadence.Next(1, 10, 1) == 1, "first keyframe evaluation advances");
  Check(cadence.Next(1, 10, 1) == 0, "second keyframe evaluation holds integer tick");
  Check(cadence.Next(1, 10, 1) == 1, "third keyframe evaluation advances");
  Check(cadence.Next(2, 20, 1) == 1, "other manager has independent cadence");
  Check(cadence.Next(1, 11, 1) == 1, "replacement record resets cadence");
  Check(!cadence.Next(1, 11, 2), "unsupported integer step rejected");
  doax2::Native60SceneCounterCadence scene_cadence;
  Check(scene_cadence.Advance(100) == true, "first scene-counter tick advances");
  Check(scene_cadence.Advance(100) == false, "second scene-counter tick holds");
  Check(scene_cadence.Advance(100) == true, "third scene-counter tick advances");
  Check(scene_cadence.Advance(160) == true, "second scene record is independent");
  Check(!scene_cadence.Advance(0), "null scene record rejected");
  return 0;
}
