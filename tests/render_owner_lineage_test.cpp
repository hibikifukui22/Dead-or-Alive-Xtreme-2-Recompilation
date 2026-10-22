#include "../sdk/rexglue-sdk/include/rex/graphics/render_owner_lineage.h"
#include "../sdk/rexglue-sdk/include/rex/graphics/render_operation_sequence.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

using namespace rex::graphics::render_audit;

static void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "owner lineage test failed: " << message << '\n';
    std::exit(1);
  }
}

static bool Has(const std::vector<uint64_t>& versions, uint64_t version) {
  return std::find(versions.begin(), versions.end(), version) != versions.end();
}

// Extracted terminal ownership route from saved log064, frame 360. This is
// deliberately only the observed tile 0..720 path, not a claimed full-scene
// capture or proof of independently backed render inputs.
static void Frame360(bool owned_seed) {
  // Saved log064 reports 34 draws, resolves before draws 2,3,4,5,34,
  // and distinct final depth/color spans at draws 5 and 6..33.
  std::vector<Target> targets(34);
  for (size_t i = 0; i < 34; ++i) {
    targets[i][0] = i == 5 ? 1 : 2;
    targets[i][1] = i == 5 ? 0x003082D0 : 0;
    targets[i][2] = i == 5 ? 0 : 0x00008000;
  }
  const size_t boundaries[] = {2, 3, 4, 5, 34};
  const auto sequence = BuildSequence(targets, boundaries);
  Check(sequence.valid && sequence.operations.size() == 11,
        "frame 360 ordered-operation shape changed");
  Check(sequence.operations[8].kind == Kind::kDrawSpan &&
            sequence.operations[8].first_draw == 5 &&
            sequence.operations[9].first_draw == 6 &&
            sequence.operations[9].draw_count == 28 &&
            sequence.operations[10].kind == Kind::kResolve &&
            sequence.operations[10].resolve_index == 4,
        "frame 360 terminal draw/resolve order changed");
  OwnerLineage graph;
  const Rect swap{0, 0, 1280, 720};
  const auto seed = graph.Seed(0x00008000, 0, 720, owned_seed);
  graph.Seed(0x000022D0, 720, 900, owned_seed);
  const auto to_depth = graph.Transfer(0x00008000, 0x003082D0, 0, 720, true);
  graph.Transfer(0x000022D0, 0x003082D0, 720, 900, true);
  // Illustrative *disjoint* depth-draw footprint. The saved log does not
  // prove this footprint, so the synthetic complete branch is not a live
  // acceptance result. The return of tiles 0..720 must keep their own version.
  const auto depth_draw = graph.Draw(0x003082D0, 720, 900, 5, {}, true);
  const auto to_color = graph.Transfer(0x003082D0, 0x00008000, 0, 720, true);
  const auto color_draw = graph.Draw(0x00008000, 0, 720, 6, {}, true);
  graph.Resolve(0, 720, swap, true);
  const auto trace = graph.TraceOutput(swap);
  Check(trace.complete == owned_seed, "frame 360 route must reject unknown seed");
  Check(trace.terminal_owner_keys == std::vector<uint32_t>{0x00008000},
        "frame 360 final owner must be color");
  for (auto version : {seed, to_depth, to_color, color_draw})
    Check(Has(trace.versions, version), "frame 360 route lost a content version");
  Check(Has(graph.Find(to_color)->dependencies, to_depth) &&
            !Has(trace.versions, depth_draw),
        "return transfer must read the retained color-tile version, not a disjoint depth draw");
}

// Extracted alternate terminal route from saved log063, frame 5880. Its
// additional tile transfers are intentionally not invented from this subset.
static void Frame5880(bool owned_seed) {
  OwnerLineage graph;
  const Rect swap{0, 0, 1280, 720};
  const auto seed = graph.Seed(0x00008000, 0, 720, owned_seed);
  graph.Seed(0x000022D0, 720, 900, owned_seed);
  const auto to_depth = graph.Transfer(0x00008000, 0x003082D0, 0, 720, true);
  graph.Transfer(0x000022D0, 0x003082D0, 720, 900, true);
  const auto depth_draw = graph.Draw(0x003082D0, 720, 900, 1250, {}, true);
  graph.Resolve(0, 720, swap, true);
  const auto trace = graph.TraceOutput(swap);
  Check(trace.complete == owned_seed, "frame 5880 route must reject unknown seed");
  Check(trace.terminal_owner_keys == std::vector<uint32_t>{0x003082D0},
        "frame 5880 final owner must be depth-keyed");
  for (auto version : {seed, to_depth})
    Check(Has(trace.versions, version), "frame 5880 route lost a content version");
  Check(!Has(trace.versions, depth_draw),
        "disjoint depth draw must not contaminate color-tile lineage");
}

static void FailClosedCases() {
  const Rect full{0, 0, 1280, 720};
  {
    OwnerLineage graph;
    graph.Transfer(0x00008000, 0x003082D0, 0, 720, true);
    graph.Resolve(0, 720, full, true);
    Check(!graph.TraceOutput(full).complete, "missing transfer source accepted");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    const auto wrong = graph.Transfer(0x003082D0, 0x000022D0, 0, 720, true);
    Check(graph.Find(wrong)->failure == OwnerLineage::Failure::kWrongOwner,
          "wrong EDRAM owner accepted");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    graph.Resolve(0, 360, {0, 0, 640, 720}, true);
    Check(!graph.TraceOutput(full).complete, "partial final output accepted");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    graph.Resolve(0, 720, full, false);
    Check(!graph.TraceOutput(full).complete, "unproven resolve layout accepted");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    graph.Transfer(0x00008000, 0x003082D0, 0, 720, false);
    graph.Resolve(0, 720, full, true);
    Check(!graph.TraceOutput(full).complete, "unproven color/depth conversion accepted");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    graph.Draw(0x00008000, 0, 720, 1, {}, false);
    graph.Resolve(0, 720, full, true);
    Check(!graph.TraceOutput(full).complete, "unproven draw input accepted");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    const auto intermediate = graph.ResolveTexture(0x100000, 1280, 720,
                                                    0, 720, full, true);
    const auto consumer = graph.ConsumeTexture(0x100000, full, true);
    const uint64_t input[] = {consumer};
    graph.Draw(0x00008000, 0, 720, 2, input, true);
    graph.Resolve(0, 720, full, true);
    const auto trace = graph.TraceOutput(full);
    Check(trace.complete && Has(trace.versions, intermediate),
          "GPU-produced intermediate dependency lost");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    graph.ResolveTexture(0x100000, 1280, 720, 0, 720,
                         {0, 0, 640, 720}, true);
    const auto consumer = graph.ConsumeTexture(0x100000, full, true);
    const uint64_t input[] = {consumer};
    graph.Draw(0x00008000, 0, 720, 2, input, true);
    graph.Resolve(0, 720, full, true);
    Check(!graph.TraceOutput(full).complete,
          "partial intermediate texture accepted as complete");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    graph.Clear(0x00008000, 0, 720, false);
    graph.Resolve(0, 720, full, true);
    Check(!graph.TraceOutput(full).complete, "unproven clear accepted");
  }
  {
    OwnerLineage graph;
    graph.ClaimUnknown(0x00008000, 0, 720);
    graph.Draw(0x00008000, 0, 720, 0, {}, true);
    graph.Resolve(0, 720, full, true);
    Check(!graph.TraceOutput(full).complete,
          "uninitialized claim plus read-modify-write draw accepted");
  }
  {
    OwnerLineage graph;
    graph.ClaimUnknown(0x00008000, 0, 720);
    graph.Clear(0x00008000, 0, 720, true);
    graph.Resolve(0, 720, full, true);
    Check(graph.TraceOutput(full).complete,
          "proven full clear should supersede unknown claim");
  }
  {
    // The live 640-wide second pass has a known clear root, but that alone
    // cannot authorize its subsequent unsnapshotted draws.
    OwnerLineage graph(640, 720);
    graph.Seed(0x00084000, 0, 720, false);
    const auto clear = graph.Clear(0x00084000, 0, 720, true);
    graph.Draw(0x00084000, 0, 720, 580, {}, false);
    graph.Resolve(0, 720, {0, 0, 640, 720}, true);
    const auto trace = graph.TraceOutput({0, 0, 640, 720});
    Check(Has(trace.versions, clear) && !trace.complete,
          "known clear root incorrectly hid unproven draw inputs");
  }
  {
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    const OwnerLineage::TransferPlan transfers[] = {
        {0x00008000, 0x003082D0, 0, 720, true}};
    const OwnerLineage::ClaimPlan claims[] = {{0x003082D0, 0, 900}};
    const auto batch = graph.ApplyOwnershipBatch(transfers, claims);
    Check(batch.valid && batch.transfers == 1 &&
              batch.unknown_claim_pieces == 1,
          "draw claim should mark only the untransferred tile remainder unknown");
    graph.Draw(0x003082D0, 720, 900, 5, {}, false);
    const auto returned = graph.Transfer(0x003082D0, 0x00008000,
                                          0, 720, true);
    graph.Resolve(0, 720, full, true);
    auto trace = graph.TraceOutput(full);
    Check(trace.complete && Has(trace.versions, returned),
          "disjoint unknown depth draw contaminated preserved color tile versions");
  }
  {
    OwnerLineage graph;
    const OwnerLineage::TransferPlan transfers[] = {
        {0x00008000, 0x003082D0, 0, 720, false}};
    const OwnerLineage::ClaimPlan too_small[] = {{0x003082D0, 0, 360}};
    Check(!graph.ApplyOwnershipBatch(transfers, too_small).valid,
          "transfer outside exact draw claim accepted");
    const OwnerLineage::ClaimPlan full_claim[] = {{0x003082D0, 0, 720}};
    const OwnerLineage::TransferPlan overlapping[] = {
        transfers[0], {0x000022D0, 0x003082D0, 360, 720, false}};
    Check(!graph.ApplyOwnershipBatch(overlapping, full_claim).valid,
          "ambiguous overlapping transfers accepted");
    const OwnerLineage::ClaimPlan duplicate_claims[] = {
        {0x003082D0, 0, 720}, {0x003082D0, 360, 720}};
    Check(!graph.ApplyOwnershipBatch({}, duplicate_claims).valid,
          "overlapping same-owner claims accepted as one batch");
  }
  {
    OwnerLineage graph;
    const OwnerLineage::TransferPlan transfer[] = {
        {0x00008000, 0x003082D0, 0, 720, true}};
    const OwnerLineage::ClaimPlan claim[] = {{0x003082D0, 0, 720}};
    Check(graph.ApplyOwnershipBatch(transfer, claim).valid,
          "recorded transfer source should establish unknown prior owner");
    graph.Resolve(0, 720, full, true);
    Check(!graph.TraceOutput(full).complete,
          "unknown bootstrap pixels were accepted as independently owned");
  }
  {
    // A CPU cache ownership checkpoint may identify a persistent owner even
    // when the terminal suffix has no ownership changes. It cannot prove
    // that the old backing pixels are independently available.
    OwnerLineage graph;
    Check(graph.Seed(0x00008000, 0, 720, false) != 0,
          "uniform owner checkpoint seed rejected");
    graph.Resolve(0, 720, full, false);
    const auto trace = graph.TraceOutput(full);
    Check(trace.terminal_owner_keys.size() == 1 &&
              trace.terminal_owner_keys[0] == 0x00008000,
          "unchanged suffix lost checkpointed owner identity");
    Check(!trace.complete,
          "CPU owner checkpoint was incorrectly accepted as complete pixels");
  }
  {
    // A complex live frame can link thousands of read-modify-write versions.
    // Tracing must use bounded heap storage, not recursive call stack depth.
    OwnerLineage graph;
    graph.Seed(0x00008000, 0, 720, true);
    for (size_t draw = 0; draw < 8192; ++draw)
      graph.Draw(0x00008000, 0, 720, draw, {}, true);
    graph.Resolve(0, 720, full, true);
    const auto trace = graph.TraceOutput(full);
    Check(trace.complete && trace.versions.size() == 8194,
          "deep draw lineage was incomplete or truncated");
  }
}

int main() {
  Frame360(false);
  Frame360(true);
  Frame5880(false);
  Frame5880(true);
  FailClosedCases();
  std::cout << "owner lineage tests passed\n";
}
