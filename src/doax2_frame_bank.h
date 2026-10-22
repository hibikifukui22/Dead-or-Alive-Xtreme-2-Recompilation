#pragma once
// Tests and the live renderer share the exact ownership implementation.
#include "../sdk/rexglue-sdk/include/rex/graphics/frame_bank.h"
namespace doax2 {
template <size_t Count> using FrameBanks = rex::graphics::FrameBanks<Count>;
}
