#pragma once

#include "doax2_render_history.h"
#include <bit>

namespace doax2 {
// Exact layout of 825ABAE0/825AC548's 64-byte instance output. Do not treat
// these records as bone matrices: word 12 is overwritten with 1 - scalar.
// Input/output words are host integers representing guest big-endian words.
inline MatrixBits PackRenderInstance(const MatrixBits& input, uint32_t scalar) {
  MatrixBits output{};
  for (size_t row = 0; row < 4; ++row)
    for (size_t column = 0; column < 4; ++column)
      output[row * 4 + column] = input[column * 4 + row];
  output[12] = std::bit_cast<uint32_t>(1.0f - std::bit_cast<float>(scalar));
  return output;
}
}  // namespace doax2
