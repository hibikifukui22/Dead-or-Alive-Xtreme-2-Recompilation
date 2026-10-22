#include "../sdk/rexglue-sdk/include/rex/graphics/render_operation_sequence.h"
#include <cstdlib>
#include <iostream>

using namespace rex::graphics::render_audit;
void Require(bool condition) {
  if (!condition) { std::cerr << "operation sequence test failed\n"; std::exit(1); }
}
void Coverage(const Sequence& sequence, size_t draws, size_t resolves) {
  Require(sequence.valid);
  size_t next_draw = 0, next_resolve = 0;
  for (const auto& op : sequence.operations) {
    Require(op.first_draw == next_draw);
    if (op.kind == Kind::kDrawSpan) {
      Require(op.draw_count != 0);
      next_draw += op.draw_count;
    } else {
      Require(op.draw_count == 0 && op.resolve_index == next_resolve++);
    }
  }
  Require(next_draw == draws && next_resolve == resolves);
}
int main() {
  Coverage(BuildSequence({}, {}), 0, 0);
  const size_t empty_events[] = {0, 0};
  Coverage(BuildSequence({}, empty_events), 0, 2);
  std::vector<Target> targets(4);
  targets[2][0] = targets[3][0] = 2;
  auto changes = BuildSequence(targets, {});
  Coverage(changes, 4, 0);
  Require(changes.operations.size() == 2);
  const size_t events[] = {0, 1, 1, 4};
  auto mixed = BuildSequence(targets, events);
  Coverage(mixed, 4, 4);
  Require(mixed.operations.size() == 7);
  const size_t unordered[] = {2, 1}, outside[] = {5};
  Require(!BuildSequence(targets, unordered).valid);
  Require(!BuildSequence(targets, outside).valid);
  // Regression from log035 frame5880: same-target draw ordinals404..769
  // contain resolves at567,575,762,770. Never merge across these events.
  targets.assign(366, Target{});
  const size_t frame5880[] = {163, 171, 358, 366};
  auto segmented = BuildSequence(targets, frame5880);
  Coverage(segmented, 366, 4);
  Require(segmented.operations.size() == 8);
  const size_t expected[] = {163, 8, 187, 8};
  for (size_t i = 0; i < 4; ++i)
    Require(segmented.operations[i * 2].draw_count == expected[i]);
  // A format change is a boundary even with identical attachment keys.
  targets.assign(2, Target{});
  targets[1][10] = 1;
  Require(BuildSequence(targets, {}).operations.size() == 2);
  std::cout << "render operation sequence tests passed\n";
}
