#include "../src/doax2_render_history.h"
#include "../src/doax2_render_packet.h"
#include "../src/doax2_capture_summary.h"
#include "../src/doax2_submission_packet.h"
#include "../src/doax2_procedural_matrix.h"
#include "../src/doax2_instance_evaluator.h"
#include "../src/doax2_pass_trace.h"
#include "../src/doax2_deformation_recipe.h"
#include "../src/doax2_transient_registry.h"
#include "../src/doax2_deformation_draw.h"
#include "../src/doax2_frame_bank.h"
#include <limits>
#include <thread>
#include <cstdlib>
#include <iostream>

void Check(bool condition) {
  if (!condition) { std::cerr << "render history test failed\n"; std::exit(1); }
}
int main() {
  // Live FXAA uses independent owners on one real submission timeline: the
  // active texture and read-only witness must BOTH retire before reuse.
  std::array<doax2::FrameBanks<1>, 2> resource_owners;
  for (auto& owner : resource_owners) {
    const auto lease = owner.Acquire(0);
    Check(lease && owner.Submitted(*lease, 42));
    Check(!owner.Acquire(41));
  }
  for (auto& owner : resource_owners) {
    const auto lease = owner.Acquire(42);
    Check(lease && owner.Submitted(*lease, 44));
    Check(!owner.Acquire(43));
    owner.QuarantineAll();
    Check(!owner.Acquire(44));
  }
  doax2::FrameBanks<2> banks;
  const auto first = banks.Acquire(0);
  const auto second = banks.Acquire(0);
  Check(first && second && first->bank != second->bank && !banks.Acquire(0));
  Check(!banks.Submitted(*first, 0));
  Check(banks.Submitted(*first, 10));
  Check(!banks.ExtendSubmitted(*first, 9, 12));
  Check(banks.ExtendSubmitted(*first, 10, 12));
  Check(!banks.CancelUnsubmitted(*first));
  Check(!banks.Submitted(*second, 12));
  Check(banks.Submitted(*second, 13));
  Check(!banks.Acquire(11));
  const auto reused = banks.Acquire(12);
  Check(reused && reused->bank == first->bank && reused->generation != first->generation);
  Check(!banks.IsRecording(*first) && !banks.CancelUnsubmitted(*first));
  Check(!banks.Acquire(11)); // Regressing completion observations reject.
  Check(banks.CancelUnsubmitted(*reused));
  const auto recording = banks.Acquire(12);
  Check(recording && !banks.Submitted(*reused, 12));
  banks.QuarantineAll();
  Check(!banks.IsRecording(*recording) && !banks.Acquire(UINT64_MAX));
  Check(!banks.CancelUnsubmitted(*recording));
  Check(doax2::DeformationWeightOffset(0) == 2576 && doax2::DeformationWeightOffset(3) == 2588);
  Check(doax2::DeformationWeightOffset(4) == 3472 && doax2::DeformationWeightOffset(7) == 3484);
  Check(doax2::kDeformationDirtyMask == 0x0008002000000000ULL);
  doax2::DeformationDrawPacket draw_packet;
  draw_packet.ticket = 3; draw_packet.valid = true;
  Check(!draw_packet.Ready(3));
  for (uint32_t i = 0; i < 5; ++i) draw_packet.Bind(i, 100+i);
  Check(draw_packet.Ready(3) && !draw_packet.Ready(4) && !draw_packet.Ready(0));
  draw_packet.Bind(0,100); Check(draw_packet.Ready(3));
  Check(draw_packet.Matches({}));
  Check(!draw_packet.Matches({1,0,0,0,0,0,0,0}));
  Check(!draw_packet.Matches({NAN,0,0,0,0,0,0,0}));
  draw_packet.Bind(0,200); Check(!draw_packet.Ready(3));
  doax2::TransientRegistry<2> registry;
  Check(!registry.Find(100));
  Check(registry.Add(100, 0, 200, 300));
  const auto old_ticket = registry.Find(100)->ticket;
  Check(registry.Find(100)->model == 200);
  registry.Retire(); Check(!registry.Find(100));
  Check(registry.Add(100, 1, 201, 301));
  Check(registry.Find(100)->ticket != old_ticket);
  Check(!registry.Add(100, 1, 201, 301)); Check(!registry.Find(100));
  registry.Retire(); Check(!registry.Add(0, 0, 0, 0));
  Check(registry.Add(1,0,0,0) && registry.Add(2,0,0,0));
  Check(!registry.Add(3,0,0,0) && !registry.Find(1));
  doax2::DeformationRecipe deformation;
  Check(doax2::EvaluateDeformationRecipe(-0.25f, 0.125f, -0.125f, 0.5f, deformation));
  Check(deformation.indices == std::array<uint32_t,4>{4,2,5,6});
  Check(deformation.weights == std::array<float,8>{0.5f,0.25f,0.125f,0.125f,0.125f,0.25f,0.125f,0.5f});
  Check(doax2::EvaluateDeformationRecipe(1, -1, 1, 0, deformation));
  Check(deformation.weights[0] == -2 && deformation.indices[1] == 3);
  Check(doax2::EvaluateDeformationRecipe(0, 0, 0, 0, deformation));
  Check(deformation.indices == std::array<uint32_t,4>{1,3,6,6});
  Check(deformation.weights[0] == 1 && std::signbit(deformation.weights[2]));
  Check(!doax2::EvaluateDeformationRecipe(INFINITY, 0, 0, 0, deformation));
  const doax2::DeformationEndpoint previous{7, 10, {-0.5f, 0.25f, -0.25f, 0.125f}};
  auto current = doax2::DeformationEndpoint{7, 11, {0.5f, -0.25f, 0.25f, -0.125f}};
  Check(doax2::InterpolateDeformationRecipe(previous, current, 0.5f, deformation));
  Check(deformation.indices == std::array<uint32_t,4>{1,3,6,6});
  Check(deformation.weights[0] == 1 && deformation.weights[4] == 1);
  for (float alpha : {0.0f, 1.0f}) {
    doax2::DeformationRecipe expected;
    const auto& v = alpha == 0 ? previous.signed_inputs : current.signed_inputs;
    Check(doax2::EvaluateDeformationRecipe(v[0], v[1], v[2], v[3], expected));
    Check(doax2::InterpolateDeformationRecipe(previous, current, alpha, deformation));
    Check(expected.indices == deformation.indices && expected.weights == deformation.weights);
  }
  const auto unchanged_recipe = deformation;
  Check(!doax2::InterpolateDeformationRecipe(previous, current, -0.1f, deformation));
  Check(!doax2::InterpolateDeformationRecipe(previous, current, 1.1f, deformation));
  Check(!doax2::InterpolateDeformationRecipe(previous, current, NAN, deformation));
  current.generation = 8;
  Check(!doax2::InterpolateDeformationRecipe(previous, current, 0.5f, deformation));
  current.generation = 7; current.simulation_frame = 12;
  Check(!doax2::InterpolateDeformationRecipe(previous, current, 0.5f, deformation));
  Check(!doax2::InterpolateDeformationRecipe({0,10,{}}, {0,11,{}}, 0.5f, deformation));
  Check(!doax2::InterpolateDeformationRecipe({7,UINT64_MAX,{}}, {7,0,{}}, 0.5f, deformation));
  Check(!doax2::InterpolateDeformationRecipe({7,10,{NAN,0,0,0}}, {7,11,{}}, 0.5f, deformation));
  Check(deformation.indices == unchanged_recipe.indices && deformation.weights == unchanged_recipe.weights);
  doax2::DeformationVectors vectors{};
  for (size_t i = 0; i < 7; ++i) vectors[i] = {float(i), float(2*i), 1, 0};
  std::array<float,4> vertex;
  Check(doax2::EvaluateDeformationRecipe(-0.25f, 0.125f, -0.125f, 0.5f, deformation));
  Check(doax2::EvaluateDeformationVector(deformation, vectors, 0, vertex));
  Check(vertex == std::array<float,4>{1.875f,3.75f,1,0});
  Check(doax2::EvaluateDeformationVector(deformation, vectors, 1, vertex));
  Check(vertex == std::array<float,4>{4.25f,8.5f,1,0});
  Check(!doax2::EvaluateDeformationVector(deformation, vectors, 2, vertex));
  deformation.indices[0] = 7;
  Check(!doax2::EvaluateDeformationVector(deformation, vectors, 0, vertex));
  // Sweeping across selector changes must remain finite and preserve the
  // homogeneous component; no blending of incompatible resource indices.
  current = {7,11,{0.5f,-0.25f,0.25f,-0.125f}};
  for (int step = 0; step <= 100; ++step) {
    Check(doax2::InterpolateDeformationRecipe(previous, current, step/100.0f, deformation));
    for (size_t bank = 0; bank < 2; ++bank) {
      Check(doax2::EvaluateDeformationVector(deformation, vectors, bank, vertex));
      Check(std::abs(vertex[2] - 1.0f) < 1e-6f);
    }
  }
  doax2::PassTrace<2> trace;
  trace.Draw(); trace.InlineDraw(); trace.Add(1, 2, 3, 4, 5);
  Check(trace.draws == 0 && trace.inline_draws == 0 && trace.count == 0);
  trace.Begin(); trace.Add(1, 2, 3, 4, 5); trace.Draw(); trace.Draw();
  trace.InlineDraw();
  trace.Add(6, 7, 8, 9, 10, {11, 12, 13, 14, 15});
  trace.Add(11, 12, 13, 14, 15); trace.End();
  Check(trace.events[0].inline_before == 0 && trace.events[1].inline_before == 1);
  Check(trace.events[1].r6_to_r10 == std::array<uint32_t, 5>{11, 12, 13, 14, 15});
  Check(trace.draws == 2 && trace.dropped == 1 && trace.count == 2);
  Check(trace.events[0].draws_before == 0 && trace.events[1].draws_before == 2);
  trace.Begin(); Check(trace.count == 0 && trace.draws == 0 && trace.inline_draws == 0 && trace.dropped == 0);
  trace.Begin(); Check(trace.dropped == 1); trace.End();
  Check(doax2::CanSubstituteInstance(0x825B016C, 0x825B0050, 0x1000, 0x12D0, true, false));
  Check(!doax2::CanSubstituteInstance(0x825B0860, 0x825B0050, 0x1000, 0x12D0, true, false));
  Check(!doax2::CanSubstituteInstance(0x825B016C, 0, 0x1000, 0x12D0, true, false));
  Check(!doax2::CanSubstituteInstance(0x825B016C, 0x825B0050, 0x1000, 0x12D4, true, false));
  Check(!doax2::CanSubstituteInstance(0x825B016C, 0x825B0050, 0x1000, 0x12D0, true, true));
  Check(!doax2::CanSubstituteInstance(0x825B016C, 0x825B0050, 0x1000, 0x12D0, false, false));
  Check(!doax2::CanSubstituteInstance(0x825B016C, 0x825B0050, 0xFFFFFF00, 0x1D0, true, false));
  doax2::FloatMatrix basis{}, evaluated;
  basis[0] = basis[5] = basis[10] = basis[15] = 1;
  Check(doax2::EvaluateInstanceMatrix(basis, 0, 0, 0, {1, 2, 4, 6}, evaluated));
  Check(evaluated[6] == 5);
  basis[13] = 2; // Row-vector translation; conjugation adds -2*shear to z.
  Check(doax2::EvaluateInstanceMatrix(basis, 0, 0, 0, {1, 2, 4, 6}, evaluated));
  Check(evaluated[6] == 5 && evaluated[14] == -10);
  Check(doax2::EvaluateInstanceMatrix(basis, 0, 0.25f, 1, {2, 0, 1, 0}, evaluated));
  Check(std::abs(evaluated[6] - 0.9207355f) < 1e-6f);
  const auto unchanged = basis;
  Check(doax2::EvaluateInstanceMatrix(basis, 1, 0, 0, {1, 2, 0, 0}, evaluated));
  Check(basis == unchanged && evaluated[14] == 0 && evaluated[15] == 1);
  basis = {};
  Check(!doax2::EvaluateInstanceMatrix(basis, 0, 0, 0, {1, 2, 4, 6}, evaluated));
  std::array<float, 16> procedural;
  Check(doax2::EvaluateProceduralMatrix(0, {1, 2, 4, 6}, procedural));
  Check(procedural[6] == 5 && procedural[0] == 1 && procedural[15] == 1);
  for (size_t i = 0; i < 16; ++i)
    if (i != 0 && i != 5 && i != 6 && i != 10 && i != 15) Check(procedural[i] == 0);
  Check(doax2::EvaluateProceduralMatrix(0.25f, {2, 3, 0, 0}, procedural));
  Check(procedural[6] == 0);
  Check(doax2::EvaluateProceduralMatrix(0.25f, {2, 0, 1, 0}, procedural));
  Check(std::abs(procedural[6] - 0.9207355f) < 1e-6f);
  Check(!doax2::EvaluateProceduralMatrix(std::numeric_limits<float>::infinity(), {1, 2, 3, 4}, procedural));
  Check(!doax2::EvaluateProceduralMatrix(1, {1, 2, std::numeric_limits<float>::quiet_NaN(), 4}, procedural));
  doax2::SubmissionPacket<2> packet;
  doax2::MatrixBits node_matrix{};
  Check(!packet.Add(1, 2, node_matrix));
  packet.Begin(10);
  Check(packet.Add(1, 2, node_matrix));
  node_matrix[0] = 99;
  Check(packet.item(0).matrix[0] == 0);
  Check(packet.End() && packet.manager() == 10);
  Check(!packet.Add(3, 4, node_matrix));
  packet.Begin(10);
  packet.Add(1, 2, node_matrix);
  Check(!packet.Add(1, 2, node_matrix) && !packet.End());
  packet.Begin(11);
  packet.Add(1, 2, node_matrix); packet.Add(2, 2, node_matrix);
  Check(!packet.Add(3, 2, node_matrix) && !packet.End());
  packet.Begin(12); packet.Begin(13); Check(!packet.End());
  doax2::CaptureSummary<2> summary;
  summary.Observe({1, 2, 3, 4, 5, 6, 7, 8, 9});
  summary.Observe({1, 2, 3, 4, 6, 10, 11, 12, 13});
  summary.Observe({2, 2, 3, 4, 7});
  summary.Observe({3, 2, 3, 4, 8});
  auto snapshot = summary.Read();
  Check(snapshot.events == 4 && snapshot.size == 2 && snapshot.dropped == 1);
  Check(snapshot.entries[0].calls == 2 && snapshot.entries[0].first_epoch == 5);
  Check(snapshot.entries[0].last.a0 == 10 && snapshot.entries[0].last.epoch == 6);
  doax2::CaptureSummary<4> threaded;
  auto writer = [&threaded](uint64_t tag) {
    for (uint32_t i = 0; i < 1000; ++i) threaded.Observe({1, 2, 3, tag, i});
  };
  std::thread t1(writer, 1), t2(writer, 2);
  t1.join(); t2.join();
  Check(threaded.Read().events == 2000 && threaded.Read().size == 2);
  doax2::MatrixBits input{};
  for (uint32_t i = 0; i < 16; ++i) input[i] = i;
  const auto packed = doax2::PackRenderInstance(input, 0x3f800000);
  for (size_t row = 0; row < 4; ++row)
    for (size_t col = 0; col < 4; ++col)
      Check(packed[row * 4 + col] == (row * 4 + col == 12 ? 0 : col * 4 + row));
  Check(doax2::PackRenderInstance(input, 0)[12] == 0x3f800000);
  Check(doax2::PackRenderInstance(input, 0x3e800000)[12] == 0x3f400000);
  Check(input[12] == 12);
  doax2::RenderHistory<2> history;
  const doax2::TransformKey a{1, 2, 3, 4, 2}, b{2, 2, 3, 4, 2}, c{3, 2, 3, 4, 2};
  doax2::MatrixBits x{}, y{};
  y[12] = 0x3f800000;
  Check(!history.Observe(a, x));
  history.Begin(); history.Observe(a, x); history.Observe(a, x);
  auto stats = history.End();
  Check(stats.unique == 1 && stats.repeated == 1 && stats.adjacent == 0);
  history.Begin(); history.Observe(a, y); stats = history.End();
  Check(stats.adjacent == 1 && stats.changed == 1);
  history.Begin(); history.Observe(a, x); history.Observe(a, y); stats = history.End();
  Check(stats.ambiguous == 1 && stats.adjacent == 0);
  history.Begin(); history.Observe(a, y); Check(history.End().adjacent == 0);
  history.Begin(); history.End(); // A missing frame breaks continuity.
  history.Begin(); history.Observe(a, y); Check(history.End().adjacent == 0);
  history.Begin(); auto other = a; other.model = 99; history.Observe(other, y);
  Check(history.End().adjacent == 0);
  history.Begin(); history.Observe(a, x); history.Observe(b, x); history.Observe(c, x);
  stats = history.End(); Check(!stats.complete && stats.dropped == 1);
  history.Begin(); history.Observe(a, x); Check(history.End().adjacent == 0);
  history.Begin(); history.Observe(a, x); history.Begin(); history.Observe(a, x);
  Check(history.End().adjacent == 0); // Abandoned frame cannot be used.
  history.Begin(); history.Observe(a, x); x[0] = 7; history.End();
  history.Begin(); history.Observe(a, x); Check(history.End().changed == 1); // Owned copy.
  std::cout << "render history tests passed\n";
}
