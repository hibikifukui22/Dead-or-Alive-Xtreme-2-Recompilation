// DOAX2 title-specific engine frame-rate unlock.

#include "generated/default/doax2_pch.h"
#include "doax2_render_history.h"
#include "doax2_render_packet.h"
#include "doax2_capture_summary.h"
#include "doax2_submission_packet.h"
#include "doax2_procedural_matrix.h"
#include "doax2_instance_evaluator.h"
#include "doax2_pass_trace.h"
#include "doax2_deformation_recipe.h"
#include "doax2_transient_registry.h"
#include "doax2_deformation_draw.h"
#include "doax2_fps_telemetry.h"
#include "doax2_native60_phase.h"
#include "doax2_native60_keyframe.h"

#include <bit>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <array>
#include <atomic>
#include <thread>
#include <functional>
#include <limits>
#include <cstring>
#include <string>
#include <mutex>
#include <map>
#include <filesystem>
#include <tuple>
#include <set>
#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif
#include <vector>

#include <rex/audio/title_playlist_player.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace {
std::atomic<uint64_t> doax2_engine_frame_count{0};
std::atomic<double> doax2_engine_fps{0.0};
std::atomic<uint32_t> native60_watched_manager{0};
}

double Doax2EngineFramesPerSecond() {
  return doax2_engine_fps.load(std::memory_order_relaxed);
}

uint64_t Doax2EngineFrameCount() {
  return doax2_engine_frame_count.load(std::memory_order_relaxed);
}

REXCVAR_DEFINE_BOOL(doax2_engine_60fps, false, "DOAX2",
                    "Enable legacy DOAX2 frame timing diagnostics")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_phase_half, false, "DOAX2",
                    "Experimental: halve verified procedural phase advances for a 60 Hz guest loop")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_keyframe_half, false, "DOAX2",
                    "Experimental: advance the verified integer keyframe step every other 60 Hz evaluation")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_scene_timeline_half, false, "DOAX2",
                    "Experimental: halve the verified per-call scene-object timeline increment at 60 Hz")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_scene_counters_half, false, "DOAX2",
                    "Experimental: advance verified scene-fiber counters every other 60 Hz call")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_mode3_gate_half, false, "DOAX2",
                    "Experimental: halve the verified mode-3 scene countdown decrement")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_preview_motion_half, false, "DOAX2",
                    "Experimental: halve verified title-preview motion-track phase and remaining-time steps")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// 0.85 chosen by the user as balanced at 60 FPS (2026-10-01).
REXCVAR_DEFINE_DOUBLE(doax2_native60_flags_cpu_strength, 0.85, "DOAX2",
                      "Beach Flags (60 Hz mode): scale of the CPU runner's per-update gauge gain (1 = original)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(doax2_native60_intro_autocapture, false, "DOAX2",
                    "Diagnostics: press F10 (xenos60 frame burst) automatically at fixed Niki Marina intro "
                    "times (race-state frames 330 and 520) so 60 and 30 runs capture the same moments")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(doax2_native60_intro_bisect, 0, "DOAX2",
                     "Diagnostics: Niki Marina intro bisect - run one group of race-loop functions only on "
                     "alternate updates during the intro (1 jet update, 2 rider, 3 girl placement, "
                     "4 race management, 5 girls' character update)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(doax2_native60_anim_sweep, false, "DOAX2",
                    "Diagnostics: animation memory sweep (3 snapshots of all guest RW memory: automatic in the "
                    "Niki Marina intro, or F9 three times anywhere) written to logs/anim_sweep_<rate>_<n>.txt")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(doax2_native60_write_watch, "", "DOAX2",
                      "Diagnostics: guest address (hex) for a hardware write breakpoint installed when the jet race starts; logs writer functions")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(doax2_native60_tick_scale, 1.0, "DOAX2",
                      "Experimental: multiply the title base game speed 83426298 (0.5 for a 60 Hz guest loop)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_script_fibers_30hz, false, "DOAX2",
                    "With the base speed halved, resume scripted-sequence fibers every other update (system fibers 0/10/14/15 stay per frame)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(doax2_native60_menu_transition_scale, 2.0, "DOAX2",
                      "60 FPS: menu/UI transition durations are multiplied by this (2 = original timing)");
REXCVAR_DEFINE_DOUBLE(doax2_native60_menu_window_scale, 3.0, "DOAX2",
                      "60 FPS: menu window pop in/out durations are multiplied by this (2 = original timing)");
REXCVAR_DEFINE_DOUBLE(doax2_native60_menu_repeat_scale, 2.0, "DOAX2",
                      "60 FPS: held-direction menu auto-repeat delay/interval are multiplied by this (2 = original speed)");
REXCVAR_DEFINE_INT32(doax2_money_multiplier, 1, "DOAX2",
                     "Multiplies casino and minigame winnings (1 = original; 10, 15, 20 or 100)");
REXCVAR_DEFINE_BOOL(doax2_native60_dev_keys, false, "DOAX2",
                    "Experimental build: F7 smoothing modes without diagnostic logging");
REXCVAR_DEFINE_BOOL(doax2_native60_counter_scan, false, "DOAX2",
                    "Diagnostic: report static-data words that change by exactly +-1 per guest update")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_native60_eval_probe, false, "DOAX2",
                    "Observe one recurring procedural instance output per guest frame")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_render_queue_probe, false, "DOAX2",
                    "Observe CPU render queue callback identities without replay")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_host_instance_test, false, "DOAX2",
                    "Use validated host matrices at one verified render stack call site (30 Hz)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_host_deformation_test, false, "DOAX2",
                    "Supply validated host deformation weights to one normal draw (30 Hz)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_time_source_probe, false, "DOAX2",
                    "Read-only census of title timing-source consumers")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(doax2_music_volume_bridge, false, "DOAX2",
                    "Apply DOAX2's Music Volume setting to host title music")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Fiber scheduler 8258CC00: 24 slots at 834A3258 (28 bytes: +0 flags,
// +4 wait, +8 fiber); 834A34F8 holds the slot currently running (-1 outside
// fibers). Scripts yield with wait 1 and count frames themselves, so their
// delays (minigame intros, READY/GO, lead-in shots) ran at double speed.
// Slots 0/10/14/15 are permanent system fibers (camera fiber is slot 10).
constexpr uint32_t kNative60FiberTable = 0x834A3258;
constexpr uint32_t kNative60FiberCurrent = 0x834A34F8;
inline bool Native60IsSystemFiberSlot(uint32_t slot) {
  return slot == 0 || slot == 10 || slot == 14 || slot == 15;
}
// Engine frame of the last update in which a minigame manager ran (jet ski
// 826496A0, Beach Flags 826ACB20, volleyball 82687C20). Script pacing is
// limited to minigames: pacing every script broke the pre-title FMV
// sequence, which waits on per-frame events (user report after log 196).
inline std::atomic<uint64_t> native60_minigame_frame{~0ull};
inline void Native60MarkMinigameUpdate() {
  native60_minigame_frame.store(Doax2EngineFrameCount(), std::memory_order_relaxed);
}
// Engine frame of the last jet-ski race manager (826496A0) update.
inline std::atomic<uint64_t> native60_jet_frame{~0ull};
inline bool Native60JetRaceActive() {
  const uint64_t last = native60_jet_frame.load(std::memory_order_relaxed);
  return last != ~0ull && Doax2EngineFrameCount() - last <= 2;
}
inline bool Native60IntroBisectSkip(uint8_t* base, int group);
inline bool Native60ScriptFibersThrottled() {
  if (!REXCVAR_GET(doax2_native60_script_fibers_30hz) ||
      float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f)
    return false;
  const uint64_t last = native60_minigame_frame.load(std::memory_order_relaxed);
  return last != ~0ull && Doax2EngineFrameCount() - last <= 2;
}
// True while a throttled script fiber runs; per-update corrections must not
// be applied again there (the fiber itself already runs at 30 Hz).
inline bool Native60InScriptFiber(uint8_t* base) {
  if (!Native60ScriptFibersThrottled()) return false;
  const int32_t slot = int32_t(REX_LOAD_U32(kNative60FiberCurrent));
  return slot >= 0 && slot < 24 && !Native60IsSystemFiberSlot(uint32_t(slot));
}
// Diagnostic: log once which fiber slot a corrected function runs in.
inline void Native60NoteFiberContext(uint8_t* base, const char* site) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  static std::mutex mutex;
  static std::set<std::string> seen;
  std::lock_guard lock(mutex);
  if (!seen.insert(site).second) return;
  REXLOG_INFO("DOAX2 native60 fiber context: site={} slot={}", site,
              int32_t(REX_LOAD_U32(kNative60FiberCurrent)));
}

// Scene camera index (839BC0F0 + 968) change detector for probe wrappers.
inline uint32_t Native60SceneIndex(uint8_t* base) {
  return REX_LOAD_U32(0x839BC0F0 + 968);
}
inline void Native60SceneIndexCheck(uint8_t* base, const char* site,
                                    uint32_t before) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  const uint32_t after = Native60SceneIndex(base);
  if (after == before) return;
  static uint64_t lines = 0;
  if (++lines <= 2000)
    REXLOG_INFO("DOAX2 native60 scene index changed inside {}: {} -> {} "
                "engine_frame={}", site, before, after, Doax2EngineFrameCount());
}

struct Native60PhaseSnapshot {
  uint32_t manager = 0;
  uint32_t bits = 0;
};

Native60PhaseSnapshot SnapshotNative60Phase(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_eval_probe)) return {};
  const uint32_t manager = native60_watched_manager.load(std::memory_order_relaxed);
  return manager ? Native60PhaseSnapshot{manager, REX_LOAD_U32(manager + 28)}
                 : Native60PhaseSnapshot{};
}

void ReportNative60PhaseBoundary(uint8_t* base, const char* boundary,
                                 Native60PhaseSnapshot before) {
  if (!before.manager) return;
  const uint32_t after = REX_LOAD_U32(before.manager + 28);
  if (after == before.bits) return;
  static std::atomic<uint64_t> changes{0};
  const uint64_t count = changes.fetch_add(1, std::memory_order_relaxed) + 1;
  if (count <= 24 || count % 512 == 0) {
    REXLOG_INFO("DOAX2 native60 phase boundary: site={} changes={} manager={:08X} "
                "before={} after={} engine_frame={}; read-only, may include "
                "concurrent guest writes",
                boundary, count, before.manager,
                std::bit_cast<float>(before.bits), std::bit_cast<float>(after),
                Doax2EngineFrameCount());
  }
}

void CorrectNative60PhaseField(uint8_t* base, const char* writer,
                               uint32_t phase_address, uint32_t before_bits) {
  if (!REXCVAR_GET(doax2_native60_phase_half)) return;
  const float before = std::bit_cast<float>(before_bits);
  const float native_after = std::bit_cast<float>(REX_LOAD_U32(phase_address));
  if (native_after == before) return;
  const float unit = std::bit_cast<float>(REX_LOAD_U32(0x82E2DD44));
  const auto corrected = doax2::HalfExpectedNativePhaseStep(
      before, native_after, unit);
  if (!corrected) {
    static std::atomic<uint64_t> rejected{0};
    const auto count = rejected.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 8 || count % 512 == 0)
      REXLOG_WARN("DOAX2 native60 phase correction skipped: writer={} "
                  "address={:08X} before={} after={} expected_unit={}",
                  writer, phase_address, before, native_after, unit);
    return;
  }
  REX_STORE_U32(phase_address, std::bit_cast<uint32_t>(*corrected));
  static std::atomic<uint64_t> corrected_calls{0};
  const auto count = corrected_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (count <= 8 || count % 512 == 0)
    REXLOG_INFO("DOAX2 native60 phase correction: writer={} calls={} "
                "address={:08X} before={} native_after={} corrected_after={} "
                "unit={}; no global clock change",
                writer, count, phase_address, before, native_after,
                *corrected, unit);
}

struct Native60KeyframeSnapshot {
  uint32_t manager = 0;
  uint32_t record = 0;
  uint32_t phase_bits = 0;
  uint32_t subframe = 0;
  uint32_t integer_step = 0;
  uint32_t modulus = 0;
  uint32_t blend_bits = 0;
};

Native60KeyframeSnapshot SnapshotNative60Keyframe(uint8_t* base,
                                                  uint32_t manager) {
  if (!manager || !REXCVAR_GET(doax2_time_source_probe) ||
      !REX_LOAD_U32(manager + 24) || !REX_LOAD_U32(manager + 4))
    return {};
  const uint32_t table = REX_LOAD_U32(manager);
  if (!table || !REX_LOAD_U32(table + 4)) return {};
  const uint32_t record = REX_LOAD_U32(table);
  if (!record) return {};
  return {manager, record, REX_LOAD_U32(record),
          REX_LOAD_U32(record + 16), REX_LOAD_U32(manager + 16),
          REX_LOAD_U32(manager + 12), REX_LOAD_U32(record + 12)};
}

void ReportNative60Keyframe(uint8_t* base, Native60KeyframeSnapshot before) {
  if (!before.record) return;
  const uint32_t after_bits = REX_LOAD_U32(before.record);
  const uint32_t after_subframe = REX_LOAD_U32(before.record + 16);
  const float phase_before = std::bit_cast<float>(before.phase_bits);
  const float phase_after = std::bit_cast<float>(after_bits);
  static thread_local uint64_t samples = 0, phase_changes = 0,
                               subframe_changes = 0, blend_changes = 0;
  static thread_local double phase_total = 0.0;
  ++samples;
  phase_changes += after_bits != before.phase_bits;
  subframe_changes += after_subframe != before.subframe;
  blend_changes += REX_LOAD_U32(before.record + 12) != before.blend_bits;
  if (std::isfinite(phase_before) && std::isfinite(phase_after))
    phase_total += double(phase_after) - double(phase_before);
  if (samples == 1 || samples % 120 == 0)
    REXLOG_INFO("DOAX2 native60 keyframe record: samples={} manager={:08X} "
                "record={:08X} phase_before={} phase_after={} "
                "subframe_before={} subframe_after={} integer_step={} "
                "modulus={} changed_phase={} changed_subframe={} "
                "changed_blend={} phase_delta_total={}; observed only",
                samples, before.manager, before.record, phase_before,
                phase_after, before.subframe, after_subframe,
                before.integer_step, before.modulus, phase_changes,
                subframe_changes, blend_changes, phase_total);
}

struct Native60SchedulerSnapshot {
  std::array<uint32_t, 24> flags{};
  std::array<uint32_t, 24> waits{};
};

Native60SchedulerSnapshot SnapshotNative60Scheduler(uint8_t* base) {
  Native60SchedulerSnapshot snapshot;
  if (!REXCVAR_GET(doax2_time_source_probe)) return snapshot;
  for (uint32_t i = 0; i < 24; ++i) {
    constexpr uint32_t table = 0x834A3258;
    snapshot.flags[i] = REX_LOAD_U32(table + i * 28);
    snapshot.waits[i] = REX_LOAD_U32(table + i * 28 + 4);
  }
  return snapshot;
}

void ReportNative60Scheduler(uint8_t* base,
                             const Native60SchedulerSnapshot& before) {
  if (!REXCVAR_GET(doax2_time_source_probe)) return;
  uint32_t active = 0, waiting = 0, ready = 0, decremented = 0,
           resumed = 0;
  for (uint32_t i = 0; i < 24; ++i) {
    constexpr uint32_t table = 0x834A3258;
    const uint32_t flags = before.flags[i];
    if (!(flags & 1) || (flags & 2)) continue;
    ++active;
    waiting += static_cast<int32_t>(before.waits[i]) > 0;
    ready += static_cast<int32_t>(before.waits[i]) <= 0;
    const uint32_t after = REX_LOAD_U32(table + i * 28 + 4);
    if (static_cast<int32_t>(before.waits[i]) > 0 &&
        after + 1 == before.waits[i]) ++decremented;
    if (static_cast<int32_t>(before.waits[i]) > 0 && after == 0) ++resumed;
  }
  static thread_local uint64_t calls = 0, total_decrements = 0,
                               total_resumes = 0, active_calls = 0;
  ++calls;
  total_decrements += decremented;
  total_resumes += resumed;
  active_calls += active != 0;
  if (calls == 1 || calls % 120 == 0)
    REXLOG_INFO("DOAX2 native60 scheduler: calls={} active={} waiting={} ready={} "
                "decrements_total={} resumes_total={} active_calls={}; "
                "24-slot fiber table, read-only",
                calls, active, waiting, ready, total_decrements, total_resumes,
                active_calls);
}

struct Native60CameraSnapshot {
  uint64_t first = 0;
  uint64_t second = 0;
  uint32_t first_position_bits = 0;
};

Native60CameraSnapshot SnapshotNative60Camera(uint8_t* base) {
  if (!REXCVAR_GET(doax2_time_source_probe)) return {};
  auto hash_matrix = [base](uint32_t address) {
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t i = 0; i < 16; ++i) {
      hash ^= REX_LOAD_U32(address + i * 4);
      hash *= 1099511628211ull;
    }
    return hash;
  };
  constexpr uint32_t first = 0x839BC1D0;
  constexpr uint32_t second = 0x839BC210;
  return {hash_matrix(first), hash_matrix(second), REX_LOAD_U32(first + 48)};
}

uint32_t Native60SelectedCameraObject(uint8_t* base) {
  // The camera corrections need the selected object independently of the
  // read-only probe; log 134 silently ran without them.
  if (!REXCVAR_GET(doax2_time_source_probe) &&
      !REXCVAR_GET(doax2_native60_scene_timeline_half) &&
      !REXCVAR_GET(doax2_native60_mode3_gate_half) &&
      !REXCVAR_GET(doax2_native60_preview_motion_half))
    return 0;
  constexpr uint32_t scene = 0x839BC0F0;
  const uint32_t index = REX_LOAD_U32(scene + 968);
  const uint32_t objects = REX_LOAD_U32(scene + 928);
  return objects && index < 8 ? objects + index * 4144 : 0;
}

bool IsNative60SceneObject(uint8_t* base, uint32_t object) {
  constexpr uint32_t scene = 0x839BC0F0;
  const uint32_t objects = REX_LOAD_U32(scene + 928);
  if (!object || !objects || object < objects) return false;
  const uint32_t offset = object - objects;
  return offset < 8 * 4144 && offset % 4144 == 0 &&
         REX_LOAD_U32(object + 3620) != 0;
}

struct Native60ObjectCameraSnapshot {
  uint64_t first = 0;
  uint64_t second = 0;
  uint64_t source = 0;
  uint64_t evaluated = 0;
  uint64_t auxiliary = 0;
  uint64_t camera_basis = 0;
  uint64_t camera_target = 0;
  uint32_t timeline = 0;
  std::array<uint32_t, 4> motion_inputs{};
};

Native60ObjectCameraSnapshot SnapshotNative60ObjectCamera(uint8_t* base,
                                                          uint32_t object) {
  if (!object) return {};
  auto hash_matrix = [base](uint32_t address) {
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t i = 0; i < 16; ++i) {
      hash ^= REX_LOAD_U32(address + i * 4);
      hash *= 1099511628211ull;
    }
    return hash;
  };
  return {hash_matrix(object + 512), hash_matrix(object + 576),
          hash_matrix(object), hash_matrix(object + 192),
          hash_matrix(object + 704), hash_matrix(object + 448),
          hash_matrix(object + 2736),
          REX_LOAD_U32(object + 3540),
          {REX_LOAD_U32(object + 240), REX_LOAD_U32(object + 244),
           REX_LOAD_U32(object + 3564), REX_LOAD_U32(object + 3568)}};
}

void ReportNative60ObjectCamera(uint8_t* base, const char* boundary,
                                uint32_t object,
                                Native60ObjectCameraSnapshot before,
                                uint32_t site) {
  if (!object) return;
  const auto after = SnapshotNative60ObjectCamera(base, object);
  static thread_local std::array<uint64_t, 20> changes{};
  if (before.first == after.first && before.second == after.second &&
      before.source == after.source &&
      before.evaluated == after.evaluated &&
      before.auxiliary == after.auxiliary &&
      before.camera_basis == after.camera_basis &&
      before.camera_target == after.camera_target &&
      before.timeline == after.timeline &&
      before.motion_inputs == after.motion_inputs) return;
  const auto count = ++changes[site];
  if (count <= 4 || count % 120 == 0)
    REXLOG_INFO("DOAX2 native60 selected camera object: boundary={} "
                "changes={} object={:08X} matrix_a_changed={} "
                "matrix_b_changed={} source_changed={} evaluated_changed={} "
                "auxiliary_changed={} basis_changed={} target_changed={} "
                "timeline_before={} timeline_after={} "
                "motion_a_before={} motion_a_after={} motion_b_before={} "
                "motion_b_after={} blend_a_before={} blend_a_after={} "
                "blend_b_before={} blend_b_after={}; "
                "read-only",
                boundary, count, object, before.first != after.first,
                before.second != after.second,
                before.source != after.source,
                before.evaluated != after.evaluated,
                before.auxiliary != after.auxiliary,
                before.camera_basis != after.camera_basis,
                before.camera_target != after.camera_target,
                std::bit_cast<float>(before.timeline),
                std::bit_cast<float>(after.timeline),
                std::bit_cast<float>(before.motion_inputs[0]),
                std::bit_cast<float>(after.motion_inputs[0]),
                std::bit_cast<float>(before.motion_inputs[1]),
                std::bit_cast<float>(after.motion_inputs[1]),
                std::bit_cast<float>(before.motion_inputs[2]),
                std::bit_cast<float>(after.motion_inputs[2]),
                std::bit_cast<float>(before.motion_inputs[3]),
                std::bit_cast<float>(after.motion_inputs[3]));
}

void ReportNative60Camera(uint8_t* base, const char* boundary,
                          Native60CameraSnapshot before, uint32_t site) {
  if (!REXCVAR_GET(doax2_time_source_probe)) return;
  const auto after = SnapshotNative60Camera(base);
  struct Counters {
    uint64_t calls = 0, first_changes = 0, second_changes = 0;
  };
  static thread_local std::array<Counters, 16> counters;
  auto& c = counters[site];
  ++c.calls;
  c.first_changes += before.first != after.first;
  c.second_changes += before.second != after.second;
  if (c.calls == 1 || c.calls % 120 == 0)
    REXLOG_INFO("DOAX2 native60 camera source: boundary={} calls={} "
                "first_changes={} second_changes={} first_pos_before={} "
                "first_pos_after={}; two 64-byte inputs to 82740E08",
                boundary, c.calls, c.first_changes, c.second_changes,
                std::bit_cast<float>(before.first_position_bits),
                std::bit_cast<float>(after.first_position_bits));
}

class Native60KeyframeStepScope {
 public:
  Native60KeyframeStepScope(uint8_t* base, uint32_t manager)
      : base_(base), manager_(manager) {
    if (!manager || !REXCVAR_GET(doax2_native60_keyframe_half) ||
        !REX_LOAD_U32(manager + 24) || !REX_LOAD_U32(manager + 4) ||
        REX_LOAD_U32(manager + 16) != 1 || REX_LOAD_U32(manager + 12) < 2)
      return;
    const uint32_t table = REX_LOAD_U32(manager);
    if (!table || !REX_LOAD_U32(table + 4)) return;
    const uint32_t record = REX_LOAD_U32(table);
    if (!record) return;
    static thread_local doax2::Native60KeyframeCadence cadence;
    const auto step = cadence.Next(manager, record, 1);
    if (!step) return;
    if (*step == 0) {
      REX_STORE_U32(manager + 16, 0);
      restore_ = true;
    }
    static std::atomic<uint64_t> calls{0}, held{0};
    const uint64_t count = calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (restore_) held.fetch_add(1, std::memory_order_relaxed);
    if (count <= 8 || count % 120 == 0)
      REXLOG_INFO("DOAX2 native60 keyframe step: calls={} held={} manager={:08X} "
                  "record={:08X} effective_step={}; original evaluator still runs",
                  count, held.load(std::memory_order_relaxed), manager, record,
                  *step);
  }

  Native60KeyframeStepScope(const Native60KeyframeStepScope&) = delete;
  Native60KeyframeStepScope& operator=(const Native60KeyframeStepScope&) = delete;
  ~Native60KeyframeStepScope() { Restore(); }

  void Restore() {
    if (!restore_) return;
    auto* base = base_;
    const uint32_t current = REX_LOAD_U32(manager_ + 16);
    if (current == 0) {
      REX_STORE_U32(manager_ + 16, 1);
    } else {
      REXLOG_WARN("DOAX2 native60 keyframe step: manager {:08X} changed "
                  "inside original update to {}; leaving it unchanged",
                  manager_, current);
    }
    restore_ = false;
  }

 private:
  uint8_t* base_;
  uint32_t manager_;
  bool restore_ = false;
};

REX_EXTERN(__imp__sub_82631818);
REX_EXTERN(__imp__sub_82643028);
REX_EXTERN(__imp__sub_826A4E00);
REX_EXTERN(__imp__sub_826A4AB8);
REX_EXTERN(__imp__sub_826A4CF0);
REX_EXTERN(__imp__sub_826A4288);
REX_EXTERN(__imp__sub_825F83D0);
REX_EXTERN(__imp__sub_826F42B0);
REX_EXTERN(__imp__sub_826F43F0);
REX_EXTERN(__imp__sub_826F4518);
REX_EXTERN(__imp__sub_826F4650);
REX_EXTERN(__imp__sub_826F46C8);
REX_EXTERN(__imp__sub_826A4800);
REX_EXTERN(__imp__sub_826A75A8);
REX_EXTERN(__imp__sub_826A7C70);
REX_EXTERN(__imp__sub_82633FE0);
REX_EXTERN(__imp__sub_82630458);
REX_EXTERN(__imp__sub_825153A0);
REX_EXTERN(__imp__sub_8262C8C0);
REX_EXTERN(__imp__sub_82513930);
REX_EXTERN(__imp__sub_82640B70);
REX_EXTERN(__imp__sub_826572A0);
REX_EXTERN(__imp__sub_82659568);
REX_EXTERN(__imp__sub_82657020);
REX_EXTERN(__imp__sub_82657B60);
REX_EXTERN(__imp__sub_82631908);
REX_EXTERN(__imp__sub_827571F8);
REX_EXTERN(__imp__sub_8275A800);
REX_EXTERN(__imp__sub_8275BDD0);
REX_EXTERN(__imp__sub_8275AAB0);
REX_EXTERN(__imp__sub_8275AB70);
REX_EXTERN(__imp__sub_825ABAE0);
REX_EXTERN(__imp__sub_825AC548);
REX_EXTERN(__imp__sub_825B0E70);
REX_EXTERN(__imp__sub_825AF158);

namespace {
enum class MusicSettingEncoding : uint8_t {
  kU32, kU16, kU8, kFloatPercent, kFloatUnit,
  kU32Step30, kU16Step30, kU8Step30,
  kU8Normalized255, kU8Normalized127, kU16Normalized65535,
};

struct MusicSettingCandidate {
  uint32_t music_address = 0;
  uint32_t stride = 0;
  MusicSettingEncoding encoding = MusicSettingEncoding::kU32;
  float previous = 67.0f;
  uint32_t previous_raw = 0;
  uint8_t sequence_stage = 0;
  uint8_t stable_frames = 0;
  uint8_t raw_log_count = 0;
};

std::vector<MusicSettingCandidate> music_setting_candidates;
bool music_setting_latched = false;
MusicSettingCandidate music_setting_latch;
uint64_t music_probe_frames = 0;
uint64_t music_probe_playing_frames = 0;
bool music_probe_scanned = false;
constexpr uint32_t kMusicVolumeFloatAddress = 0x8397D290;
constexpr uint32_t kMusicVolumeMirrorAddress = 0x8397D294;
constexpr uint32_t kMusicVolumeIntegerAddress = 0x8398A5D8;
bool music_direct_baseline_valid = false;
uint32_t music_direct_baseline_raw = 0;
uint8_t music_direct_stable_frames = 0;

float ReadMusicSetting(const MusicSettingCandidate& candidate, uint8_t* base,
                       uint32_t address) {
  switch (candidate.encoding) {
    case MusicSettingEncoding::kU32:
      return static_cast<float>(REX_LOAD_U32(address));
    case MusicSettingEncoding::kU16:
      return static_cast<float>(REX_LOAD_U16(address));
    case MusicSettingEncoding::kU8:
      return static_cast<float>(REX_LOAD_U8(address));
    case MusicSettingEncoding::kFloatPercent:
      return std::bit_cast<float>(REX_LOAD_U32(address));
    case MusicSettingEncoding::kFloatUnit:
      return std::bit_cast<float>(REX_LOAD_U32(address)) * 100.0f;
    case MusicSettingEncoding::kU32Step30:
      return static_cast<float>(REX_LOAD_U32(address)) * (100.0f / 30.0f);
    case MusicSettingEncoding::kU16Step30:
      return static_cast<float>(REX_LOAD_U16(address)) * (100.0f / 30.0f);
    case MusicSettingEncoding::kU8Step30:
      return static_cast<float>(REX_LOAD_U8(address)) * (100.0f / 30.0f);
    case MusicSettingEncoding::kU8Normalized255:
      return static_cast<float>(REX_LOAD_U8(address)) * (100.0f / 255.0f);
    case MusicSettingEncoding::kU8Normalized127:
      return static_cast<float>(REX_LOAD_U8(address)) * (100.0f / 127.0f);
    case MusicSettingEncoding::kU16Normalized65535:
      return static_cast<float>(REX_LOAD_U16(address)) * (100.0f / 65535.0f);
  }
  return -1.0f;
}

uint32_t ReadMusicSettingRaw(const MusicSettingCandidate& candidate,
                             uint8_t* base, uint32_t address) {
  switch (candidate.encoding) {
    case MusicSettingEncoding::kU8:
    case MusicSettingEncoding::kU8Step30:
    case MusicSettingEncoding::kU8Normalized255:
    case MusicSettingEncoding::kU8Normalized127:
      return REX_LOAD_U8(address);
    case MusicSettingEncoding::kU16:
    case MusicSettingEncoding::kU16Step30:
    case MusicSettingEncoding::kU16Normalized65535:
      return REX_LOAD_U16(address);
    default:
      return REX_LOAD_U32(address);
  }
}

const char* MusicSettingEncodingName(MusicSettingEncoding encoding) {
  switch (encoding) {
    case MusicSettingEncoding::kU32: return "u32";
    case MusicSettingEncoding::kU16: return "u16";
    case MusicSettingEncoding::kU8: return "u8";
    case MusicSettingEncoding::kFloatPercent: return "float-percent";
    case MusicSettingEncoding::kFloatUnit: return "float-unit";
    case MusicSettingEncoding::kU32Step30: return "u32-step30";
    case MusicSettingEncoding::kU16Step30: return "u16-step30";
    case MusicSettingEncoding::kU8Step30: return "u8-step30";
    case MusicSettingEncoding::kU8Normalized255: return "u8-normalized255";
    case MusicSettingEncoding::kU8Normalized127: return "u8-normalized127";
    case MusicSettingEncoding::kU16Normalized65535: return "u16-normalized65535";
  }
  return "unknown";
}

void FindMusicSettingCandidates(uint8_t* base) {
  music_setting_candidates.clear();

  const auto add_matches = [&](uint64_t range_begin, uint64_t range_end,
                               MusicSettingEncoding encoding, uint32_t alignment,
                               uint32_t minimum_stride, uint32_t maximum_stride) {
    if (range_end <= range_begin + alignment) return;
    MusicSettingCandidate probe;
    probe.encoding = encoding;
    for (uint64_t address64 = (range_begin + alignment - 1) & ~(uint64_t(alignment) - 1);
         address64 <= range_end - alignment; address64 += alignment) {
      const uint32_t address = static_cast<uint32_t>(address64);
      probe.music_address = address;
      probe.stride = 0;
      const float music = ReadMusicSetting(probe, base, address);
      if (std::abs(music - 67.0f) > 0.51f) continue;
      probe.previous = music;
      probe.previous_raw = ReadMusicSettingRaw(probe, base, address);
      music_setting_candidates.push_back(probe);
    }
  };

  // Use ReXGlue's guest allocation table, not VirtualQuery. Host mappings may
  // be committed while a guest page is deliberately fault-protected (log 136
  // hit a title-thread stack guard at 0x70430000). Normal title allocations
  // exclude the 0x3... and 0x7... system/title thread-stack bands.
  auto* memory = REX_KERNEL_MEMORY();
  const auto scan_heap = [&](uint32_t begin, uint32_t end) {
    uint64_t cursor = begin;
    while (cursor < end) {
      auto* heap = memory->LookupHeap(static_cast<uint32_t>(cursor));
      if (!heap) break;
      rex::memory::HeapAllocationInfo info{};
      if (!heap->QueryRegionInfo(static_cast<uint32_t>(cursor), &info)) break;
      const uint64_t step = std::max<uint64_t>(info.region_size, 4096);
      const uint64_t region_end = std::min<uint64_t>(end, cursor + step);
      const bool committed = (info.state & rex::memory::kMemoryAllocationCommit) != 0;
      const bool readable = (info.protect & rex::memory::kMemoryProtectRead) != 0;
      const bool writable = (info.protect & rex::memory::kMemoryProtectWrite) != 0;
      if (committed && readable && writable && region_end > cursor + 128) {
        add_matches(cursor, region_end, MusicSettingEncoding::kU32, 4, 4, 64);
        add_matches(cursor, region_end, MusicSettingEncoding::kU16, 2, 2, 32);
        add_matches(cursor, region_end, MusicSettingEncoding::kFloatPercent, 4, 4, 64);
        add_matches(cursor, region_end, MusicSettingEncoding::kFloatUnit, 4, 4, 64);
        add_matches(cursor, region_end, MusicSettingEncoding::kU8, 1, 1, 16);
        add_matches(cursor, region_end, MusicSettingEncoding::kU32Step30, 4, 4, 64);
        add_matches(cursor, region_end, MusicSettingEncoding::kU16Step30, 2, 2, 32);
        add_matches(cursor, region_end, MusicSettingEncoding::kU8Step30, 1, 1, 16);
        add_matches(cursor, region_end, MusicSettingEncoding::kU8Normalized255, 1, 1, 16);
        add_matches(cursor, region_end, MusicSettingEncoding::kU8Normalized127, 1, 1, 16);
        add_matches(cursor, region_end, MusicSettingEncoding::kU16Normalized65535, 2, 2, 32);
      }
      cursor = region_end;
    }
  };
  scan_heap(0x00010000, 0x30000000);
  scan_heap(0x40000000, 0x70000000);

  const size_t dynamic_candidate_count = music_setting_candidates.size();
  // Persistent title options normally live in the executable's writable data
  // sections, not in the low dynamic heaps. The old static tuple probe did
  // inspect this range safely, but discarded the real Music field by assuming
  // that S.E. and Voice immediately followed it. Scan each writable field here
  // independently and retain the deliberate sequence as the discriminator.
  scan_heap(REX_CODE_BASE + REX_CODE_SIZE, REX_IMAGE_BASE + REX_IMAGE_SIZE);
  const size_t title_data_candidate_count =
      music_setting_candidates.size() - dynamic_candidate_count;

  REXLOG_INFO("DOAX2 independent music slider probe: tracking {} fields equal to 67 (dynamic {}, writable title data {}); perform Music-only sequence 23 -> 0 -> 80",
              music_setting_candidates.size(), dynamic_candidate_count,
              title_data_candidate_count);
}

void ProbeMusicSetting(uint8_t* base) {
  if (!REXCVAR_GET(doax2_music_volume_bridge)) return;
  ++music_probe_frames;

  const auto playlist = rex::audio::GetTitlePlaylistDiagnostics();
  if (!playlist.playing) return;

  // The live 67 -> 23 -> 0 -> 80 trace proved that DOAX2 writes the Music
  // slider to two matching normalized floats and an integer-percent copy.
  // Require all three representations to agree before trusting the field so
  // zero-initialized title data cannot be mistaken for an initialized option.
  const uint32_t raw = REX_LOAD_U32(kMusicVolumeFloatAddress);
  const float unit = std::bit_cast<float>(raw);
  const float mirror =
      std::bit_cast<float>(REX_LOAD_U32(kMusicVolumeMirrorAddress));
  const uint8_t integer_percent = REX_LOAD_U8(kMusicVolumeIntegerAddress);
  const float percent = unit * 100.0f;
  const bool coherent =
      std::isfinite(unit) && std::isfinite(mirror) && unit >= 0.0f &&
      unit <= 1.0f && std::abs(unit - mirror) <= 0.00001f &&
      integer_percent <= 100 &&
      std::abs(percent - static_cast<float>(integer_percent)) <= 0.01f;

  if (!music_setting_latched) {
    if (!coherent) {
      music_direct_stable_frames = 0;
      return;
    }
    if (!music_direct_baseline_valid) {
      music_direct_baseline_valid = true;
      music_direct_baseline_raw = raw;
    }
    const bool changed_from_baseline = raw != music_direct_baseline_raw;
    if (integer_percent != 0 && music_direct_stable_frames < 30) {
      ++music_direct_stable_frames;
    }
    if (!changed_from_baseline && music_direct_stable_frames < 30) return;

    music_setting_latch.music_address = kMusicVolumeFloatAddress;
    music_setting_latch.encoding = MusicSettingEncoding::kFloatUnit;
    music_setting_latch.previous = percent;
    music_setting_latch.previous_raw = raw;
    music_setting_latched = true;
    rex::audio::SetTitlePlaylistVolume(unit);
    REXLOG_INFO("DOAX2 Music Volume direct bridge initialized: address {:08X}, mirror {:08X}, integer {:08X}; title {} -> requested {:.3f}, effective_gain {:.3f}",
                kMusicVolumeFloatAddress, kMusicVolumeMirrorAddress,
                kMusicVolumeIntegerAddress, percent, unit, unit * 0.25f);
    return;
  }

  if (music_setting_latched) {
    if (coherent &&
        std::abs(percent - music_setting_latch.previous) >= 0.01f) {
      music_setting_latch.previous = percent;
      rex::audio::SetTitlePlaylistVolume(unit);
      REXLOG_INFO("DOAX2 Music Volume bridge: title {} -> requested {:.3f}, effective_gain {:.3f}",
                  percent, unit, unit * 0.25f);
    }
    return;
  }
}

doax2::CaptureSummary<256> capture_summary;
doax2::CaptureSummary<128> shader_input_summary;
doax2::CaptureSummary<256> geometry_input_summary;
thread_local uint32_t shader_input_parent = 0;
thread_local doax2::TransientRegistry<1024> transient_registry;
thread_local doax2::DeformationDrawPacket deformation_draw;
thread_local uint64_t deformation_draw_prepared = 0, deformation_draw_skipped = 0;
thread_local uint64_t deformation_draw_checked = 0, deformation_draw_mismatch = 0, deformation_draw_incomplete = 0;
thread_local uint64_t deformation_draw_supplied = 0, deformation_changed_words = 0;
struct DeformationDrawScope {
  doax2::DeformationDrawPacket previous = deformation_draw;
  DeformationDrawScope() { deformation_draw = {}; }
  ~DeformationDrawScope() { deformation_draw = previous; }
};
thread_local uint64_t transient_created = 0, transient_rejected = 0;
thread_local uint64_t transient_matched = 0, transient_missing = 0, transient_retired = 0;
struct ShaderInputScope {
  uint32_t previous = shader_input_parent;
  explicit ShaderInputScope(uint32_t parent) { shader_input_parent = parent; }
  ~ShaderInputScope() { shader_input_parent = previous; }
};
std::atomic<uint64_t> capture_epoch{0};

enum class TimeProbeSite : size_t {
  kAnimationKeyframe,
  kProceduralPhase,
  kCameraSceneTransform,
  kEffectOscillator,
  kSpringPhysics,
  kGameplayCountdowns,
  kCameraPublish,
  kCount,
};
std::array<std::atomic<uint64_t>, size_t(TimeProbeSite::kCount)> time_probe_calls{};
std::array<std::atomic<uint32_t>, size_t(TimeProbeSite::kCount)> time_probe_callers{};
void ObserveTimeProbe(TimeProbeSite site, const PPCContext& ctx) {
  if (!REXCVAR_GET(doax2_time_source_probe)) return;
  time_probe_calls[size_t(site)].fetch_add(1, std::memory_order_relaxed);
  time_probe_callers[size_t(site)].store(static_cast<uint32_t>(ctx.lr),
                                         std::memory_order_relaxed);
}
thread_local uint32_t capture_parent = 0;
thread_local doax2::SubmissionPacket<512> hierarchy_packet;
thread_local doax2::PassTrace<128> pass_trace;
void RecordPassCall(uint32_t function, PPCContext& ctx) {
  pass_trace.Add(function, static_cast<uint32_t>(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
                 {ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32});
}
struct CaptureParentScope {
  uint32_t previous;
  explicit CaptureParentScope(uint32_t parent) : previous(capture_parent) { capture_parent = parent; }
  ~CaptureParentScope() { capture_parent = previous; }
};

void CaptureEdge(uint32_t function, uint32_t caller, uint32_t parent,
                 uint32_t a0, uint32_t a1, uint32_t a2, uint32_t result) {
  static thread_local const uint64_t thread_tag = std::hash<std::thread::id>{}(std::this_thread::get_id());
  capture_summary.Observe({function, caller, parent, thread_tag,
      capture_epoch.load(std::memory_order_relaxed), a0, a1, a2, result});
}

void ReportCapture() {
  REXLOG_INFO("DOAX2 host deformation consumption: enabled {}, supplied {}, differing words {}; same-phase normal draws only; cache restored, simulation untouched",
      REXCVAR_GET(doax2_host_deformation_test), deformation_draw_supplied, deformation_changed_words);
  REXLOG_INFO("DOAX2 deformation draw check: prepared {}, skipped {}, checked {}, mismatches {}, incomplete {}; current allocation only, all five bindings required; comparison gates optional same-phase consumption; no replay",
      deformation_draw_prepared, deformation_draw_skipped, deformation_draw_checked,
      deformation_draw_mismatch, deformation_draw_incomplete);
  REXLOG_INFO("DOAX2 transient ownership: created {}, rejected {}, matched wrappers {}, missing wrappers {}, retirements {}; same-thread allocation tickets only, not persistent model generations",
      transient_created, transient_rejected, transient_matched, transient_missing, transient_retired);
  const auto geometry = geometry_input_summary.Read();
  REXLOG_INFO("DOAX2 geometry input coverage: events {}, edges {}/256, dropped {}; observed renderer thread only; slot-keyed inputs, not pose identity",
              geometry.events, geometry.size, geometry.dropped);
  for (size_t i = 0; i < geometry.size; ++i) {
    const auto& entry = geometry.entries[i];
    const auto& e = entry.last;
    REXLOG_INFO("DOAX2 geometry input edge: fn {:08X}, caller {:08X}, slot {}, thread {:016X}, calls {}, args {:08X} {:08X} {:08X}; constant: count/source/device; stream: buffer/device/unused",
                e.function, e.caller, e.parent, e.thread, entry.calls, e.a0, e.a1, e.a2);
  }
  const auto inputs = shader_input_summary.Read();
  REXLOG_INFO("DOAX2 shader input provenance: events {}, edges {}/128, dropped {}; parent is callback or draw wrapper; numeric source labels only, not bone classification",
              inputs.events, inputs.size, inputs.dropped);
  for (size_t i = 0; i < inputs.size; ++i) {
    const auto& entry = inputs.entries[i];
    const auto& e = entry.last;
    REXLOG_INFO("DOAX2 shader input edge: fn {:08X}, caller {:08X}, parent {:08X}, thread {:016X}, calls {}, args {:08X} {:08X} {:08X}; uploads: start/count/source; wrapper: object/payload/material",
                e.function, e.caller, e.parent, e.thread, entry.calls, e.a0, e.a1, e.a2);
  }
  const auto data = capture_summary.Read(); // Release lock before logging.
  REXLOG_INFO("DOAX2 combined capture: epoch {}, events {}, edges {}/256, dropped {}; thread tags are host hashes; epochs observational, not worker synchronization",
              capture_epoch.load(), data.events, data.size, data.dropped);
  for (size_t i = 0; i < data.size; ++i) {
    const auto& entry = data.entries[i];
    const auto& e = entry.last;
    REXLOG_INFO("DOAX2 capture edge: fn {:08X}, caller {:08X}, parent {:08X}, thread {:016X}, calls {}, epochs {}-{}, args {:08X} {:08X} {:08X}, result {:08X}",
                e.function, e.caller, e.parent, e.thread, entry.calls,
                entry.first_epoch, e.epoch, e.a0, e.a1, e.a2, e.result);
  }
}

// Per-thread capture intentionally excludes worker callbacks outside the root
// scope. This is not a complete scene snapshot or an interpolation permission.
thread_local doax2::RenderHistory<2048> render_history;

// Read-only equivalence check for the two statically classified uploaders.
// No pointers are retained beyond the original call; no resource is replayed.
// Nonstandard count getters and oversized/invalid layouts fail closed.
struct InstanceUploadCheck {
  std::array<doax2::MatrixBits, 128> packed{};
  uint32_t owner = 0, destination = 0, count = 0;
  uint32_t caller = 0, getter = 0;
  const char* skip = "none";
};

InstanceUploadCheck CaptureInstanceUpload(PPCContext& ctx, uint8_t* base, bool ring) {
  InstanceUploadCheck check;
  check.owner = ctx.r3.u32;
  check.caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t owner = check.owner;
  const uint32_t capacity = REX_LOAD_U32(owner + 4);
  if (!capacity) { check.skip = "empty capacity"; return check; }
  const uint32_t kind = REX_LOAD_U32(owner + (ring ? 20 : 16));
  if (kind > 2 && kind != 6) { check.skip = "non-matrix kind"; return check; }
  const uint32_t table = REX_LOAD_U32(owner);
  const uint32_t getter = REX_LOAD_U32(table + 40);
  check.getter = getter;
  if (getter != (ring ? 0x825D9FC8u : 0x825AD898u)) {
    check.skip = "unclassified count getter"; return check;
  }
  const uint32_t slot = REX_LOAD_U32(owner + (ring ? 28 : 48));
  // The known double/triple-buffer layouts have two/three count slots.
  if (slot >= (ring ? 3u : 2u)) { check.skip = "slot out of range"; return check; }
  const uint32_t count = REX_LOAD_U32(owner + (ring ? 188 : 132) + slot * 4);
  if (!count) { check.skip = "empty count"; return check; }
  if (count > capacity || count > check.packed.size()) {
    check.skip = "count exceeds capacity or verifier limit"; return check;
  }
  const uint32_t source = REX_LOAD_U32(owner + 8);
  const uint32_t scalars = REX_LOAD_U32(owner + 12);
  const uint32_t destination = REX_LOAD_U32(owner + (ring ? 68 : 36) + slot * 4);
  const auto fits = [](uint32_t at, uint32_t bytes) {
    return at && at <= UINT32_MAX - bytes;
  };
  if (!fits(source, count * 64) || !fits(destination, count * 64) ||
      (scalars && !fits(scalars, count * 4))) {
    check.skip = "invalid address range"; return check;
  }
  for (uint32_t i = 0; i < count; ++i) {
    doax2::MatrixBits input;
    for (uint32_t j = 0; j < 16; ++j) input[j] = REX_LOAD_U32(source + i * 64 + j * 4);
    const uint32_t scalar = scalars ? REX_LOAD_U32(scalars + i * 4) : 0x3f800000;
    check.packed[i] = doax2::PackRenderInstance(input, scalar);
  }
  check.destination = destination;
  check.count = count;
  return check;
}

void VerifyInstanceUpload(PPCContext& ctx, uint8_t* base,
                          const InstanceUploadCheck& check, bool ring) {
  struct Totals { uint64_t calls = 0, skipped = 0, records = 0, mismatches = 0; };
  static thread_local std::array<Totals, 2> totals{};
  auto& total = totals[ring ? 1 : 0];
  ++total.calls;
  if (!check.count) ++total.skipped;
  uint32_t mismatches = 0;
  for (uint32_t i = 0; i < check.count; ++i) {
    bool differs = false;
    for (uint32_t j = 0; j < 16; ++j)
      differs |= check.packed[i][j] != REX_LOAD_U32(check.destination + i * 64 + j * 4);
    mismatches += differs;
  }
  total.records += check.count;
  total.mismatches += mismatches;
  CaptureEdge(ring ? 0x825AC548u : 0x825ABAE0u, check.caller, capture_parent,
              check.owner, check.destination, check.count, mismatches);
  if (total.calls == 1 || total.calls % 120 == 0 || (mismatches && total.mismatches == mismatches)) {
    REXLOG_INFO("DOAX2 instance pack check: uploader {:08X}, calls {}, skipped {}, checked records {}, mismatched records {}; latest owner {:08X}, caller {:08X}, getter {:08X}, count {}, skip {}; host comparison only",
                ring ? 0x825AC548u : 0x825ABAE0u, total.calls, total.skipped,
                total.records, total.mismatches, check.owner, check.caller,
                check.getter, check.count, check.skip);
  }
}

void ObserveInlineMatrices(PPCContext& ctx, uint8_t* base, uint32_t count) {
  if (!render_history.active()) return;
  const uint32_t object = ctx.r3.u32;
  const uint32_t type = REX_LOAD_U32(object);
  for (uint32_t n = 0; n < count; ++n) {
    const uint32_t source = object + 16 + n * 64;
    doax2::MatrixBits values;
    for (uint32_t i = 0; i < 16; ++i) values[i] = REX_LOAD_U32(source + i * 4);
    render_history.Observe({object, type, source, 0, n + 3}, values);
  }
}

// Bounded observations of proven renderer inputs, never replacement guest data.
void ObserveRenderType(PPCContext& ctx, uint8_t* base, uint32_t object, bool delegate = false) {
  const uint32_t table = REX_LOAD_U32(object);
  static thread_local std::array<uint32_t, 128> seen{};
  static thread_local size_t count = 0;
  for (size_t i = 0; i < count; ++i) if (seen[i] == table) return;
  if (count == seen.size()) return;
  seen[count++] = table;
  REXLOG_INFO("DOAX2 render type: object {:08X}, vtable {:08X}, slots4-32 {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
              object, table, REX_LOAD_U32(table + 4), REX_LOAD_U32(table + 8),
              REX_LOAD_U32(table + 12), REX_LOAD_U32(table + 16),
              REX_LOAD_U32(table + 20), delegate ? 0 : REX_LOAD_U32(table + 24),
              delegate ? 0 : REX_LOAD_U32(table + 28), delegate ? 0 : REX_LOAD_U32(table + 32));
}
}  // namespace

REX_HOOK_RAW(sub_825B0E70) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) {
    __imp__sub_825B0E70(ctx, base);
    return;
  }
  const uint32_t destination = ctx.r3.u32;
  const uint32_t parameters = ctx.r4.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const float phase = static_cast<float>(ctx.f1.f64);
  std::array<float, 4> p;
  for (uint32_t i = 0; i < 4; ++i)
    p[i] = std::bit_cast<float>(REX_LOAD_U32(parameters + 12 + i * 4));
  std::array<float, 16> expected;
  const bool supported = doax2::EvaluateProceduralMatrix(phase, p, expected);
  __imp__sub_825B0E70(ctx, base);
  static thread_local uint64_t calls = 0, checked = 0, skipped = 0, mismatched = 0;
  static thread_local float max_error = 0;
  ++calls;
  bool mismatch = false;
  if (!supported) ++skipped;
  else {
    ++checked;
    for (uint32_t i = 0; i < 16; ++i) {
      const float actual = std::bit_cast<float>(REX_LOAD_U32(destination + i * 4));
      const float error = std::abs(actual - expected[i]);
      if (std::isfinite(error) && error > max_error) max_error = error;
      mismatch |= !std::isfinite(actual) || error > 1e-5f * (1.0f + std::abs(actual));
    }
    if (mismatch) ++mismatched;
  }
  if (calls == 1 || calls % 4096 == 0 || (mismatch && mismatched == 1)) {
    REXLOG_INFO("DOAX2 procedural matrix check: calls {}, checked {}, skipped {}, mismatched {}, max finite abs error {}; caller {:08X}, parameters {:08X}, phase {}; tolerance 1e-5*(1+abs(actual)); comparison only",
                calls, checked, skipped, mismatched, max_error, caller, parameters, phase);
  }
}

REX_HOOK_RAW(sub_825AF158) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) {
    const bool native60_probe = REXCVAR_GET(doax2_native60_eval_probe);
    const uint32_t output = ctx.r3.u32;
    const uint32_t manager = ctx.r4.u32;
    const uint32_t index = ctx.r5.u32;
    __imp__sub_825AF158(ctx, base);
    if (native60_probe) {
      // Follow one recurring manager/index on this worker. A changed matrix
      // proves this evaluator produced a new visual input, not that a whole
      // completed game image was distinct. The separate final-swap capture
      // supplies the image-level proof.
      static thread_local uint32_t selected_manager = 0;
      static thread_local uint32_t selected_index = 0;
      static thread_local uint64_t previous_frame = UINT64_MAX;
      static thread_local uint64_t previous_hash = 0;
      static thread_local uint64_t samples = 0, changed = 0;
      static thread_local float previous_phase = 0.0f;
      static thread_local double phase_advance = 0.0;
      static thread_local auto window_start = std::chrono::steady_clock::now();
      if (!selected_manager && REX_LOAD_U32(manager + 48) != 0) {
        selected_manager = manager;
        selected_index = index;
        uint32_t empty = 0;
        native60_watched_manager.compare_exchange_strong(
            empty, manager, std::memory_order_relaxed);
        const uint32_t vtable = REX_LOAD_U32(manager);
        REXLOG_INFO("DOAX2 native60 evaluator selected: manager={:08X} "
                    "index={} vtable={:08X} advance_12={:08X} active={}; "
                    "address is a sample label, not lifetime identity",
                    manager, index, vtable, REX_LOAD_U32(vtable + 12),
                    REX_LOAD_U32(manager + 48));
      }
      const uint64_t frame = Doax2EngineFrameCount();
      if (manager == selected_manager && index == selected_index &&
          frame != previous_frame) {
        uint64_t hash = 14695981039346656037ull;
        for (uint32_t i = 0; i < 16; ++i) {
          hash ^= REX_LOAD_U32(output + i * 4);
          hash *= 1099511628211ull;
        }
        const float phase = std::bit_cast<float>(REX_LOAD_U32(manager + 28));
        if (samples) {
          changed += hash != previous_hash;
          phase_advance += double(phase) - double(previous_phase);
        }
        ++samples;
        previous_frame = frame;
        previous_hash = hash;
        previous_phase = phase;
        if (samples % 120 == 0) {
          const uint32_t previous_watch = native60_watched_manager.exchange(
              selected_manager, std::memory_order_relaxed);
          if (previous_watch != selected_manager) {
            REXLOG_INFO("DOAX2 native60 phase watch selected recurring manager "
                        "{:08X} (previous {:08X}); read-only",
                        selected_manager, previous_watch);
          }
          const auto now = std::chrono::steady_clock::now();
          const double seconds = std::chrono::duration<double>(now - window_start).count();
          REXLOG_INFO("DOAX2 native60 evaluator: manager={:08X} index={} "
                      "sampled_frames={} changed_matrices={} phase_advance={} "
                      "elapsed_s={:.3f}; one render input only",
                      manager, index, samples, changed, phase_advance, seconds);
          window_start = now;
        }
      }
    }
    return;
  }
  const uint32_t output = ctx.r3.u32, manager = ctx.r4.u32, index = ctx.r5.u32;
  const uint32_t entry_stack = ctx.r1.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t parameters = REX_LOAD_U32(manager + 24);
  // Original PPC address arithmetic wraps at 32 bits; only read its 64-byte
  // matrix input, not the rest of the 96-byte record.
  const uint32_t source = REX_LOAD_U32(manager + 8) + index * 96u;
  doax2::FloatMatrix basis, expected;
  std::array<float, 4> p;
  for (uint32_t i = 0; i < 16; ++i) basis[i] = std::bit_cast<float>(REX_LOAD_U32(source + i * 4));
  for (uint32_t i = 0; i < 4; ++i) p[i] = std::bit_cast<float>(REX_LOAD_U32(parameters + 12 + i * 4));
  const float phase = std::bit_cast<float>(REX_LOAD_U32(manager + 28));
  const float stride = std::bit_cast<float>(REX_LOAD_U32(parameters + 28));
  const bool supported = doax2::EvaluateInstanceMatrix(basis, phase, stride, index, p, expected);
  __imp__sub_825AF158(ctx, base);
  static thread_local uint64_t calls = 0, checked = 0, skipped = 0, mismatched = 0;
  static thread_local float max_error = 0;
  ++calls;
  bool mismatch = false;
  if (!supported) ++skipped;
  else {
    ++checked;
    for (uint32_t i = 0; i < 16; ++i) {
      const float actual = std::bit_cast<float>(REX_LOAD_U32(output + i * 4));
      const float error = std::abs(actual - expected[i]);
      if (std::isfinite(error) && error > max_error) max_error = error;
      mismatch |= !std::isfinite(actual) || error > 1e-4f * (1.0f + std::abs(actual));
    }
    if (mismatch) ++mismatched;
  }
  static thread_local uint64_t substituted = 0, retained = 0;
  const bool replacement_requested = REXCVAR_GET(doax2_host_instance_test);
  const bool replace = replacement_requested &&
      doax2::CanSubstituteInstance(caller, capture_parent, entry_stack, output, supported, mismatch);
  if (replace) {
    // Only caller-owned stack output; never write the live source matrix or
    // phase. Original execution preserves PPC register/FP side effects.
    for (uint32_t i = 0; i < 16; ++i)
      REX_STORE_U32(output + i * 4, std::bit_cast<uint32_t>(expected[i]));
    ++substituted;
  } else if (replacement_requested) ++retained;
  if (calls == 1 || calls % 4096 == 0 || (mismatch && mismatched == 1) || (replace && substituted == 1)) {
    REXLOG_INFO("DOAX2 instance evaluator check: calls {}, checked {}, skipped {}, mismatched {}, max finite abs error {}; caller {:08X}, manager {:08X}, source {:08X}, index {}; tolerance 1e-4*(1+abs(actual))",
                calls, checked, skipped, mismatched, max_error, caller, manager, source, index);
    REXLOG_INFO("DOAX2 host instance test: enabled {}, substituted {}, retained {}, latest stack-site eligible {}; original simulation cadence; no extra frame",
                replacement_requested, substituted, retained, replace);
  }
}

// Register-only observations: unknown guest layouts are never walked here.
// Producer scopes link indirect append/upload calls to their enclosing caller.
// Writer probe scope for functions that already have their own hooks (the
// DOAX2_NATIVE60_POS_PROBE wrappers cover the rest); defined with the probes.
struct Native60WriterProbeState {
  std::array<uint32_t, 12> pos{};
  std::array<uint32_t, 64> cam{};
  std::array<uint32_t, 16> watch{};
  int depth = 0;
  bool active = false;
};
Native60WriterProbeState Native60WriterProbeBegin(uint8_t* base);
void Native60WriterProbeEnd(uint8_t* base, const char* name, const Native60WriterProbeState& state);
struct Native60WriterProbeScope {
  uint8_t* base;
  const char* name;
  Native60WriterProbeState state;
  Native60WriterProbeScope(uint8_t* b, const char* n)
      : base(b), name(n), state(Native60WriterProbeBegin(b)) {}
  ~Native60WriterProbeScope() { Native60WriterProbeEnd(base, name, state); }
};

#define DOAX2_CAPTURE_PRODUCER(name, address) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    if constexpr (address == 0x825A4550) \
      ObserveTimeProbe(TimeProbeSite::kAnimationKeyframe, ctx); \
    const auto native60_keyframe_before = SnapshotNative60Keyframe( \
        base, address == 0x825A4550 ? ctx.r3.u32 : 0); \
    const auto native60_phase_before = SnapshotNative60Phase(base); \
    Native60KeyframeStepScope keyframe_step( \
        base, address == 0x825A4550 ? ctx.r3.u32 : 0); \
    if (!REXCVAR_GET(doax2_render_queue_probe)) { \
      __imp__##name(ctx, base); \
      keyframe_step.Restore(); \
      ReportNative60Keyframe(base, native60_keyframe_before); \
      ReportNative60PhaseBoundary(base, #name, native60_phase_before); \
      return; \
    } \
    const uint32_t caller = static_cast<uint32_t>(ctx.lr); \
    const uint32_t a0 = ctx.r3.u32, a1 = ctx.r4.u32, a2 = ctx.r5.u32; \
    const uint32_t parent = capture_parent; \
    CaptureParentScope scope(address); \
    __imp__##name(ctx, base); \
    keyframe_step.Restore(); \
    ReportNative60Keyframe(base, native60_keyframe_before); \
    ReportNative60PhaseBoundary(base, #name, native60_phase_before); \
    CaptureEdge(address, caller, parent, a0, a1, a2, ctx.r3.u32); \
  }

#define DOAX2_CAPTURE_LEAF(name, address) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    if (!REXCVAR_GET(doax2_render_queue_probe)) { __imp__##name(ctx, base); return; } \
    const uint32_t caller = static_cast<uint32_t>(ctx.lr); \
    const uint32_t a0 = ctx.r3.u32, a1 = ctx.r4.u32, a2 = ctx.r5.u32; \
    __imp__##name(ctx, base); \
    CaptureEdge(address, caller, capture_parent, a0, a1, a2, ctx.r3.u32); \
  }

DOAX2_CAPTURE_PRODUCER(sub_825A4550, 0x825A4550)
DOAX2_CAPTURE_PRODUCER(sub_825B0050, 0x825B0050)
DOAX2_CAPTURE_PRODUCER(sub_825F3110, 0x825F3110)
DOAX2_CAPTURE_LEAF(sub_825AD8F0, 0x825AD8F0)
DOAX2_CAPTURE_LEAF(sub_825DA020, 0x825DA020)
DOAX2_CAPTURE_LEAF(sub_82551240, 0x82551240)
DOAX2_CAPTURE_LEAF(sub_82551528, 0x82551528)
#undef DOAX2_CAPTURE_PRODUCER
#undef DOAX2_CAPTURE_LEAF

// Read-only representative timing consumers. Static tracing establishes that
// 825A4550/825B0028/825E04C8/82605298 read the common float at 82E2DD44.
// 825B7C08 instead performs a spring-like fixed-step update, and 8265E7B8
// decrements integer gameplay counters. Only the explicitly opt-in phase
// correction hook below changes behavior.
#define DOAX2_TIME_PROBE_HOOK(name, site) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    Native60WriterProbeScope native60_probe_scope(base, #name); \
    ObserveTimeProbe(TimeProbeSite::site, ctx); \
    const auto native60_phase_before = SnapshotNative60Phase(base); \
    const uint32_t native60_index_before = Native60SceneIndex(base); \
    __imp__##name(ctx, base); \
    Native60SceneIndexCheck(base, #name, native60_index_before); \
    ReportNative60PhaseBoundary(base, #name, native60_phase_before); \
  }
// 825ACE88 dispatches virtual +12 on each 200-byte manager in its list.
// Observe whether this producer is actually reached before attributing the
// evaluator's changing phase to 825B0028. This probe never edits guest state.
REX_EXTERN(__imp__sub_825ACE88);
REX_HOOK_RAW(sub_825ACE88) {
  if (REXCVAR_GET(doax2_native60_eval_probe)) {
    static thread_local uint64_t calls = 0;
    ++calls;
    if (calls == 1 || calls % 512 == 0) {
      const uint32_t list = ctx.r3.u32;
      const uint32_t count = REX_LOAD_U32(list + 4);
      const uint32_t first = count ? REX_LOAD_U32(list) : 0;
      const uint32_t vtable = first ? REX_LOAD_U32(first) : 0;
      const uint32_t target = vtable ? REX_LOAD_U32(vtable + 12) : 0;
      REXLOG_INFO("DOAX2 native60 phase dispatcher: calls={} caller={:08X} "
                  "list={:08X} count={} first={:08X} virtual_12={:08X}",
                  calls, static_cast<uint32_t>(ctx.lr), list, count, first,
                  target);
    }
  }
  __imp__sub_825ACE88(ctx, base);
}
// 825AE0E8 also adds the shared scalar to object+28, but unlike 825B0028
// it does not check object+48. Observe its live object identity and call rate
// before deciding whether either writer is appropriate for a timing fix.
REX_EXTERN(__imp__sub_825AE0E8);
REX_HOOK_RAW(sub_825AE0E8) {
  const bool probe_enabled = REXCVAR_GET(doax2_native60_eval_probe);
  const uint32_t object = ctx.r3.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const float before = probe_enabled
      ? std::bit_cast<float>(REX_LOAD_U32(object + 28)) : 0.0f;
  __imp__sub_825AE0E8(ctx, base);
  if (probe_enabled) {
    static thread_local uint64_t calls = 0;
    ++calls;
    if (calls == 1 || calls % 512 == 0) {
      const float after = std::bit_cast<float>(REX_LOAD_U32(object + 28));
      REXLOG_INFO("DOAX2 native60 alternate phase writer: calls={} "
                  "caller={:08X} object={:08X} before={} after={} vtable={:08X}",
                  calls, caller, object, before, after, REX_LOAD_U32(object));
    }
  }
}
REX_EXTERN(__imp__sub_825B0028);
REX_HOOK_RAW(sub_825B0028) {
  ObserveTimeProbe(TimeProbeSite::kProceduralPhase, ctx);
  const bool correction_enabled = REXCVAR_GET(doax2_native60_phase_half);
  const bool probe_enabled = REXCVAR_GET(doax2_native60_eval_probe);
  if (!correction_enabled && !probe_enabled) {
    __imp__sub_825B0028(ctx, base);
    return;
  }

  // This title function has one persistent write: when manager+48 is active,
  // it adds the shared per-tick scalar to manager+28. Run the original first
  // to preserve its PPC context effects, then correct only that phase write.
  // No global delta, gameplay clock, or other instance is changed here.
  const uint32_t manager = ctx.r3.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const bool active = REX_LOAD_U32(manager + 48) != 0;
  const float before = std::bit_cast<float>(REX_LOAD_U32(manager + 28));
  __imp__sub_825B0028(ctx, base);
  const float native_after = std::bit_cast<float>(REX_LOAD_U32(manager + 28));
  if (probe_enabled) {
    static thread_local uint64_t observed_calls = 0;
    ++observed_calls;
    if (observed_calls == 1 || observed_calls % 512 == 0) {
      REXLOG_INFO("DOAX2 native60 phase writer: calls={} caller={:08X} "
                  "manager={:08X} active={} before={} after={}; original call",
                  observed_calls, caller, manager, active, before, native_after);
    }
  }
  if (!correction_enabled) return;
  if (!active) return;

  const auto corrected_value = doax2::HalfNativePhaseStep(before, native_after);
  if (!corrected_value) {
    REXLOG_WARN("DOAX2 native60 phase correction skipped: nonfinite manager {:08X}",
                manager);
    return;
  }
  const float native_step = native_after - before;
  const float corrected = *corrected_value;
  REX_STORE_U32(manager + 28, std::bit_cast<uint32_t>(corrected));
  ctx.f0.f64 = double(corrected);

  static thread_local uint64_t corrected_calls = 0;
  static thread_local double native_step_total = 0.0;
  static thread_local double corrected_step_total = 0.0;
  ++corrected_calls;
  native_step_total += double(native_step);
  corrected_step_total += double(corrected - before);
  if (corrected_calls == 1 || corrected_calls % 512 == 0) {
    REXLOG_INFO(
        "DOAX2 native60 procedural phase: corrected_calls={} manager={:08X} "
        "last_before={} native_after={} corrected_after={} "
        "mean_native_step={} mean_corrected_step={}; only manager+28 altered",
        corrected_calls, manager, before, native_after, corrected,
        native_step_total / double(corrected_calls),
        corrected_step_total / double(corrected_calls));
  }
}
DOAX2_TIME_PROBE_HOOK(sub_825E04C8, kCameraSceneTransform)
DOAX2_TIME_PROBE_HOOK(sub_82605298, kEffectOscillator)
DOAX2_TIME_PROBE_HOOK(sub_825B7C08, kSpringPhysics)
DOAX2_TIME_PROBE_HOOK(sub_8265E7B8, kGameplayCountdowns)
DOAX2_TIME_PROBE_HOOK(sub_82740E08, kCameraPublish)
#undef DOAX2_TIME_PROBE_HOOK

// Game-wide step: 8250A568 publishes 833B84CC = base (83426298) * modifier
// (834262A4) when either changes, and 825C19C8 copies it into 82E2DD44.
// The base is written only by init 8250A118; the modifier carries the
// title's own pause/slow-motion. Scaling the base therefore halves every
// step consumer through the game's own formula while keeping pause intact.
constexpr uint32_t kNative60BaseSpeed = 0x83426298;
constexpr uint32_t kNative60SpeedModifier = 0x834262A4;
constexpr uint32_t kNative60GameStep = 0x833B84CC;
constexpr uint32_t kNative60TickUnit = 0x82E2DD44;

float Native60LoadFloat(uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(REX_LOAD_U32(address));
}

REX_EXTERN(__imp__sub_8250A118);
REX_HOOK_RAW(sub_8250A118) {
  __imp__sub_8250A118(ctx, base);
  const float scale = float(REXCVAR_GET(doax2_native60_tick_scale));
  if (scale == 1.0f || !std::isfinite(scale) || scale <= 0) return;
  const float native_base = Native60LoadFloat(base, kNative60BaseSpeed);
  // The cached copy at 834262A0 still holds the native base, so the next
  // 8250A568 call observes a change and republishes the game step.
  REX_STORE_U32(kNative60BaseSpeed, std::bit_cast<uint32_t>(native_base * scale));
  static std::atomic<uint64_t> inits{0};
  REXLOG_INFO("DOAX2 native60 base speed ACTIVE: init={} native_base={} "
              "scaled_base={} modifier={}; game recomputes its own step",
              inits.fetch_add(1) + 1, native_base, native_base * scale,
              Native60LoadFloat(base, kNative60SpeedModifier));
}

// Camera tracks and the mode-3 gate size their sample tables/durations from
// the game step (600 entries at step 0.5 versus 300 at 1.0) and then also
// advance by the step, which double-compensates to half speed (logs 140/141).
// Present the unscaled step only for the duration of those calls so they
// advance one 60 Hz table entry per update.
class Native60UnscaledStep {
 public:
  explicit Native60UnscaledStep(uint8_t* guest_base) : base(guest_base) {
    const float scale = float(REXCVAR_GET(doax2_native60_tick_scale));
    if (scale == 1.0f || !std::isfinite(scale) || scale <= 0) return;
    // 82424558 reaches 82421CF8 through 82421680; only the outermost
    // scope rewrites the step.
    if (depth_++ != 0) { nested_ = true; return; }
    scaled_bits_ = REX_LOAD_U32(kNative60GameStep);
    unscaled_bits_ = std::bit_cast<uint32_t>(
        std::bit_cast<float>(scaled_bits_) / scale);
    REX_STORE_U32(kNative60GameStep, unscaled_bits_);
    active_ = true;
  }
  ~Native60UnscaledStep() {
    if (nested_ || active_) --depth_;
    // Leave any value the guest itself published during the call.
    if (active_ && REX_LOAD_U32(kNative60GameStep) == unscaled_bits_)
      REX_STORE_U32(kNative60GameStep, scaled_bits_);
  }

 private:
  static inline thread_local int depth_ = 0;
  uint8_t* base;
  bool active_ = false, nested_ = false;
  uint32_t scaled_bits_ = 0, unscaled_bits_ = 0;
};

// Logs selected camera object / mode changes with wall time so 30 Hz and
// scaled 60 Hz runs of the same scene can be compared shot by shot.
void ReportNative60CameraState(uint8_t* base) {
  if (!REXCVAR_GET(doax2_engine_60fps)) return;
  constexpr uint32_t scene = 0x839BC0F0;
  const uint32_t index = REX_LOAD_U32(scene + 968);
  const uint32_t objects = REX_LOAD_U32(scene + 928);
  const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
  const uint32_t mode = object ? REX_LOAD_U32(object + 3580) : 0;
  static uint32_t last_index = ~0u, last_mode = ~0u, last_objects = ~0u;
  static uint64_t updates = 0, transitions = 0;
  static const auto start = std::chrono::steady_clock::now();
  ++updates;
  static double next_sample = 0;
  const double now_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  if (object && now_seconds >= next_sample) {
    next_sample = now_seconds + 0.5;
    constexpr uint32_t camera = 0x839BC1D0;
    REXLOG_INFO("DOAX2 native60 camera sample: t={:.3f}s update={} mode={} "
                "gate={} timer={} trackA={}/{} trackB={}/{} cam=({:.3f},{:.3f},{:.3f})",
                now_seconds, updates, mode,
                Native60LoadFloat(base, object + 1940),
                Native60LoadFloat(base, object + 1944),
                Native60LoadFloat(base, object + 832 + 128),
                Native60LoadFloat(base, object + 832 + 136),
                Native60LoadFloat(base, object + 1024 + 128),
                Native60LoadFloat(base, object + 1024 + 136),
                Native60LoadFloat(base, camera + 48),
                Native60LoadFloat(base, camera + 52),
                Native60LoadFloat(base, camera + 56));
  }
  if (index == last_index && mode == last_mode && objects == last_objects) return;
  last_index = index;
  last_mode = mode;
  last_objects = objects;
  if (++transitions > 400) return;
  const double seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  REXLOG_INFO("DOAX2 native60 camera state: t={:.3f}s update={} index={} "
              "objects={:08X} mode={} gate={} step={}",
              seconds, updates, index, objects, mode,
              object ? Native60LoadFloat(base, object + 1940) : 0.0f,
              Native60LoadFloat(base, kNative60GameStep));
}

#if defined(DOAX2_FUNC_CENSUS)
#include "doax2_census.h"
#include <cstdio>
#include <mutex>
#include <map>
namespace {
std::mutex census_mutex;
std::vector<Doax2CensusSlot*>& CensusSlots() {
  static std::vector<Doax2CensusSlot*> slots;
  return slots;
}
}  // namespace
Doax2CensusSlot* Doax2CensusRegister(const char* name) {
  auto* slot = new Doax2CensusSlot;
  slot->name = name;
  std::lock_guard lock(census_mutex);
  CensusSlots().push_back(slot);
  return slot;
}
// Every 30 updates, append each function's call delta with context so
// offline analysis can isolate functions active only in one game mode.
void DumpNative60Census(uint8_t* base) {
  static uint64_t updates = 0, window = 0;
  static std::vector<uint64_t> previous;
  static FILE* file = std::fopen("out/game/logs/doax2_census.csv", "w");
  static const auto start = std::chrono::steady_clock::now();
  if (!file || ++updates % 30 != 0) return;
  const double seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  constexpr uint32_t scene = 0x839BC0F0;
  const uint32_t index = REX_LOAD_U32(scene + 968);
  const uint32_t objects = REX_LOAD_U32(scene + 928);
  const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
  const uint32_t mode = object ? REX_LOAD_U32(object + 3580) : 0;
  std::lock_guard lock(census_mutex);
  auto& slots = CensusSlots();
  previous.resize(slots.size(), 0);
  ++window;
  std::fprintf(file, "W,%llu,%.3f,%llu,%g,%u,%08X\n",
               (unsigned long long)window, seconds,
               (unsigned long long)updates,
               Native60LoadFloat(base, kNative60GameStep), mode, objects);
  for (size_t i = 0; i < slots.size(); ++i) {
    const uint64_t now = slots[i]->calls.load(std::memory_order_relaxed);
    if (now != previous[i])
      std::fprintf(file, "F,%s,%llu\n", slots[i]->name,
                   (unsigned long long)(now - previous[i]));
    previous[i] = now;
  }
  if (window % 20 == 0) std::fflush(file);
}
#else
void DumpNative60Census(uint8_t*) {}
#endif

// Per-update counter finder. With the base speed scaled, step-driven timers
// move by +-0.5 per update; anything still moving by exactly +-1 per update
// (integer or float) is a fixed-step counter that runs at double speed.
// Segments split on camera mode/object changes; each segment reports its
// consistent +-1 words.
void ScanNative60Counters(uint8_t* base, uint32_t mode, uint32_t index) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  // 83300000-83CFFFFF: includes the player-state table 833B8520 and the
  // Butt Battle state 83CAF200 (outside the earlier 834-839 range).
  constexpr uint32_t kStart = 0x83300000, kWords = 0xA00000 / 4;
  static std::vector<uint32_t> previous(kWords), current(kWords);
  static std::vector<uint16_t> int_hits(kWords), float_hits(kWords),
      half_hits(kWords), unit_hits(kWords), change_count(kWords);
  static std::vector<uint8_t> unit_kind(kWords);
  // Alternation finder (Beach Flags hitch): words that change on exactly
  // every other update produce 30 Hz motion even though the loop is 60 Hz.
  static std::vector<uint8_t> changed_last(kWords);
  static std::vector<uint16_t> alternations(kWords);
  static bool primed = false;
  static uint32_t seg_mode = ~0u, seg_index = ~0u, seg_updates = 0, segments = 0;
  static double seg_start = 0;
  static const auto start = std::chrono::steady_clock::now();
  const double now = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  std::memcpy(current.data(), base + kStart, kWords * 4);
  auto report = [&] {
    if (seg_updates < 45 || ++segments > 300) return;
    uint32_t lines = 0;
    const uint32_t need = seg_updates * 4 / 5;
    for (uint32_t i = 0; i < kWords && lines < 160; ++i) {
      const bool is_int = int_hits[i] >= need, is_float = float_hits[i] >= need;
      if (!is_int && !is_float) continue;
      const uint32_t value = std::byteswap(current[i]);
      REXLOG_INFO("DOAX2 native60 counter: seg={} t={:.1f}-{:.1f}s mode={} "
                  "index={} updates={} addr={:08X} kind={} hits={} half={} "
                  "value={}",
                  segments, seg_start, now, seg_mode, seg_index, seg_updates,
                  kStart + i * 4, is_int ? "int" : "float",
                  is_int ? int_hits[i] : float_hits[i], half_hits[i],
                  is_int ? std::to_string(int32_t(value))
                         : std::to_string(std::bit_cast<float>(value)));
      ++lines;
    }
    // Short or sub-word counters: every change is an exact one-step tick of
    // an 8/16/32-bit field or a float, and there were at least 12 ticks.
    static const char* kinds[] = {"none", "u32", "hi16", "byte2", "byte0",
                                  "float"};
    uint32_t short_lines = 0;
    for (uint32_t i = 0; i < kWords && short_lines < 200; ++i) {
      if (change_count[i] < 12 || unit_hits[i] * 10 < change_count[i] * 9)
        continue;
      if (int_hits[i] >= need || float_hits[i] >= need) continue;
      const uint32_t value = std::byteswap(current[i]);
      REXLOG_INFO("DOAX2 native60 short counter: seg={} t={:.1f}-{:.1f}s "
                  "mode={} index={} updates={} addr={:08X} kind={} ticks={} "
                  "changes={} raw={:08X}",
                  segments, seg_start, now, seg_mode, seg_index, seg_updates,
                  kStart + i * 4, kinds[unit_kind[i] < 6 ? unit_kind[i] : 0],
                  unit_hits[i], change_count[i], value);
      ++short_lines;
    }
    // Non-counter values (not unit ticks) changing on ~half the updates in
    // strict alternation, grouped into address ranges.
    uint32_t alt_lines = 0, range_start = 0, range_count = 0, range_first = 0;
    auto flush_range = [&](uint32_t end) {
      if (range_count && alt_lines < 40) {
        const uint32_t value = std::byteswap(current[range_first]);
        REXLOG_INFO("DOAX2 native60 alternating: seg={} t={:.1f}-{:.1f}s "
                    "mode={} index={} updates={} range={:08X}-{:08X} words={} "
                    "changes={} alternations={} first_float={}",
                    segments, seg_start, now, seg_mode, seg_index, seg_updates,
                    kStart + range_start * 4, kStart + end * 4, range_count,
                    change_count[range_first], alternations[range_first],
                    std::bit_cast<float>(value));
        ++alt_lines;
      }
      range_count = 0;
    };
    uint32_t range_last = 0;
    for (uint32_t i = 0; i < kWords; ++i) {
      const bool alt = change_count[i] * 20 >= seg_updates * 7 &&
                       change_count[i] * 20 <= seg_updates * 13 &&
                       alternations[i] * 5 >= seg_updates * 4 &&
                       unit_hits[i] * 2 < change_count[i];
      if (range_count && i - range_last > 3) {
        flush_range(range_last + 1);
      }
      if (!alt) continue;
      if (!range_count) { range_start = i; range_first = i; }
      ++range_count;
      range_last = i;
    }
    flush_range(range_last + 1);
  };
  if (mode != seg_mode || index != seg_index || seg_updates >= 600) {
    report();
    std::fill(int_hits.begin(), int_hits.end(), 0);
    std::fill(float_hits.begin(), float_hits.end(), 0);
    std::fill(half_hits.begin(), half_hits.end(), 0);
    std::fill(unit_hits.begin(), unit_hits.end(), 0);
    std::fill(change_count.begin(), change_count.end(), 0);
    std::fill(alternations.begin(), alternations.end(), 0);
    seg_mode = mode;
    seg_index = index;
    seg_updates = 0;
    seg_start = now;
  }
  if (primed) {
    ++seg_updates;
    for (uint32_t i = 0; i < kWords; ++i) {
      const uint8_t changed = current[i] != previous[i];
      if (changed != changed_last[i] && alternations[i] < 0xFFFF) ++alternations[i];
      changed_last[i] = changed;
      if (!changed) continue;
      const uint32_t now_value = std::byteswap(current[i]);
      const uint32_t old_value = std::byteswap(previous[i]);
      const int32_t int_delta = int32_t(now_value - old_value);
      if (int_delta == 1 || int_delta == -1) ++int_hits[i];
      const float delta = std::bit_cast<float>(now_value) -
                          std::bit_cast<float>(old_value);
      if (delta == 1.0f || delta == -1.0f) ++float_hits[i];
      else if (delta == 0.5f || delta == -0.5f) ++half_hits[i];
      if (change_count[i] < 0xFFFF) ++change_count[i];
      const uint32_t diff = now_value - old_value;
      uint8_t kind = 0;
      if (diff == 1u || diff == 0xFFFFFFFFu) kind = 1;
      else if (diff == 0x10000u || diff == 0xFFFF0000u) kind = 2;
      else if (diff == 0x100u || diff == 0xFFFFFF00u) kind = 3;
      else if (diff == 0x1000000u || diff == 0xFF000000u) kind = 4;
      else if (delta == 1.0f || delta == -1.0f) kind = 5;
      if (kind && unit_hits[i] < 0xFFFF) {
        ++unit_hits[i];
        unit_kind[i] = kind;
      }
    }
  }
  primed = true;
  previous.swap(current);
}

// Jet-ski (and related water) physics use a hard-coded integration step of
// 1/120 s at 823EC120 (read by 82634EB0/82635BD8/82643028 and the jet-ski
// camera 8242D1B8; 82583B90/825851B0 were idle in the tested modes) instead
// of the game step, so the whole race ran at double speed at 60 Hz (census
// log 189). With the base speed scaled, halve that constant in place.
void ApplyNative60PhysicsStep(uint8_t* base) {
  constexpr uint32_t kPhysicsStep = 0x823EC120;
  const float scale = float(REXCVAR_GET(doax2_native60_tick_scale));
  if (scale != 0.5f) return;
  constexpr float kOriginal = 1.0f / 120.0f;
  const float current = Native60LoadFloat(base, kPhysicsStep);
  if (std::fabs(current - kOriginal) > 1e-7f) return;
  // The constant lives in a read-only page of the title image (a direct
  // store faulted, log 190); unprotect that one host page for the write.
  static bool failed = false;
  if (failed) return;
#if defined(_WIN32)
  void* host = base + kPhysicsStep;
  DWORD old_protect = 0;
  if (!VirtualProtect(host, 4, PAGE_READWRITE, &old_protect)) {
    failed = true;
    REXLOG_INFO("DOAX2 native60 physics step: VirtualProtect failed ({})",
                uint32_t(GetLastError()));
    return;
  }
  REX_STORE_U32(kPhysicsStep, std::bit_cast<uint32_t>(kOriginal * scale));
  DWORD ignored = 0;
  VirtualProtect(host, 4, old_protect, &ignored);
#else
  return;
#endif
  REXLOG_INFO("DOAX2 native60 physics step scaled: {} -> {}", current,
              Native60LoadFloat(base, kPhysicsStep));
}

// Diagnostic: confirm the jet-ski racer physics runs outside script fibers
// (the halved physics step assumes one call per 60 Hz update).
REX_EXTERN(__imp__sub_82634EB0);
REX_HOOK_RAW(sub_82634EB0) {
  Native60NoteFiberContext(base, "82634EB0 jet physics");
  if (Native60InScriptFiber(base)) {
    static uint64_t warnings = 0;
    if (++warnings <= 4)
      REXLOG_INFO("DOAX2 native60 WARNING: jet physics inside a throttled "
                  "script fiber; physics would run at half speed");
  }
  __imp__sub_82634EB0(ctx, base);
}

void ReportNative60HitWindows(uint8_t* base);
void ReportNative60StaticRegions(uint8_t* base);
#if defined(_WIN32)
#include <tlhelp32.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
// Hardware write breakpoints (diagnostics): DR0-DR3 on up to four guest
// words (comma-separated hex list) for every thread; the vectored handler
// unwinds the writer and its two callers and counts each chain per slot,
// reported every 300 updates with symbol names (recompiled function names
// from doax2.pdb), plus the per-update write counts of the last 40 updates.
// GPU (physical-view) writes do not trap.
namespace native60_watch {
using Chain = std::array<uintptr_t, 6>;
inline std::mutex mutex;
inline std::map<std::pair<int, Chain>, uint64_t> hits;
inline std::array<std::atomic<uint32_t>, 4> update_hits{};
inline std::array<uint32_t, 4> guests{};
inline std::array<bool, 4> read_write{};  // "<hex>r": trap reads too
inline int slots = 0;
inline Chain Unwind(const CONTEXT& fault) {
  Chain chain{};
  CONTEXT c = fault;
  for (int i = 0; i < 6; ++i) {
    chain[i] = uintptr_t(c.Rip);
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
    if (!fn) {
      c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
      c.Rsp += 8;
    } else {
      void* handler_data = nullptr;
      DWORD64 frame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fn, &c, &handler_data, &frame, nullptr);
    }
    if (!c.Rip) break;
  }
  return chain;
}
inline LONG CALLBACK Handler(PEXCEPTION_POINTERS info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  const DWORD64 dr6 = info->ContextRecord->Dr6;
  if (!(dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
  info->ContextRecord->Dr6 = 0;
  const Chain chain = Unwind(*info->ContextRecord);
  std::lock_guard lock(mutex);
  for (int i = 0; i < 4; ++i)
    if (dr6 & (1ull << i)) {
      ++hits[{i, chain}];
      ++update_hits[i];
    }
  return EXCEPTION_CONTINUE_EXECUTION;
}
inline void Install(uint8_t* base) {
  AddVectoredExceptionHandler(1, Handler);
  // Apply from a helper thread so the calling thread can be suspended too.
  std::thread([base] {
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    int armed = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
      if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self) continue;
      HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE,
                            te.th32ThreadID);
      if (!t) continue;
      if (SuspendThread(t) != DWORD(-1)) {
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(t, &c)) {
          DWORD64* dr[4] = {&c.Dr0, &c.Dr1, &c.Dr2, &c.Dr3};
          DWORD64 dr7 = c.Dr7 & ~0xFFFF00FFull;
          for (int i = 0; i < slots; ++i) {
            *dr[i] = DWORD64(base + guests[i]);
            dr7 |= (1ull << (2 * i)) | ((read_write[i] ? 3ull : 1ull) << (16 + 4 * i)) |
                   (3ull << (18 + 4 * i));  // Ln, write (or read/write), 4 bytes
          }
          c.Dr7 = dr7;
          if (SetThreadContext(t, &c)) ++armed;
        }
        ResumeThread(t);
      }
      CloseHandle(t);
    }
    CloseHandle(snap);
    REXLOG_INFO("DOAX2 native60 write watch armed on {} threads ({} addresses)", armed, slots);
  }).join();
}
inline std::string Symbol(uintptr_t pc) {
  char buffer[sizeof(SYMBOL_INFO) + 256]{};
  auto* info = reinterpret_cast<SYMBOL_INFO*>(buffer);
  info->SizeOfStruct = sizeof(SYMBOL_INFO);
  info->MaxNameLen = 255;
  DWORD64 displacement = 0;
  if (!pc) return "-";
  if (!SymFromAddr(GetCurrentProcess(), pc, &displacement, info)) return fmt::format("{:X}", pc);
  return fmt::format("{}+{:X}", info->Name, displacement);
}
inline std::array<std::string, 4> history;
inline std::atomic<bool> report_now{false};
inline void Report() {
  static bool sym = false;
  if (!sym) {
    sym = true;
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), "out\\native60-build;out\\game", TRUE);
  }
  std::map<std::pair<int, Chain>, uint64_t> copy;
  {
    std::lock_guard lock(mutex);
    copy.swap(hits);
  }
  for (int i = 0; i < slots; ++i) {
    std::string line;
    for (auto& [key, n] : copy)
      if (key.first == i)
        line += fmt::format(" [{} <- {} <- {} <- {} <- {} <- {} x{}]", Symbol(key.second[0]),
                            Symbol(key.second[1]), Symbol(key.second[2]), Symbol(key.second[3]),
                            Symbol(key.second[4]), Symbol(key.second[5]), n);
    REXLOG_INFO("DOAX2 native60 write watch {:08X}{} writers (300 updates):{}", guests[i],
                read_write[i] ? " (reads+writes)" : "",
                line.empty() ? std::string(" none") : line);
    REXLOG_INFO("DOAX2 native60 write watch {:08X} writes per update (last 40):{}", guests[i],
                history[i]);
  }
}
}  // namespace native60_watch
// Set by the slide update hook on its first call (write watch trigger).
std::atomic<bool> native60_slide_gameplay_started{false};
void Native60WriteWatchUpdate(uint8_t* base) {
  const std::string text = REXCVAR_GET(doax2_native60_write_watch);
  if (text.empty()) return;
  static bool installed = false;
  static uint64_t updates = 0;
  if (!installed) {
    const uint32_t index = REX_LOAD_U32(0x839BC0F0 + 968);
    const uint32_t objects = REX_LOAD_U32(0x839BC0F0 + 928);
    const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
    // A trailing '*' arms immediately (testing); otherwise wait for the jet
    // race (or race camera) or the Pool Slide gameplay.
    const bool now = text.back() == '*';
    if (!now && !Native60JetRaceActive() && !native60_slide_gameplay_started.load() &&
        (!object || REX_LOAD_U32(object + 3580) != 9))
      return;
    size_t pos = 0;
    while (pos < text.size() && native60_watch::slots < 4) {
      const size_t comma = text.find(',', pos);
      const std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
      if (!item.empty() && item != "*") {
        native60_watch::read_write[native60_watch::slots] = item.back() == 'r';
        native60_watch::guests[native60_watch::slots++] = uint32_t(std::stoul(item, nullptr, 16));
      }
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
    native60_watch::Install(base);
    installed = true;
    return;
  }
  for (int i = 0; i < native60_watch::slots; ++i) {
    auto& h = native60_watch::history[i];
    h += fmt::format(" {}", native60_watch::update_hits[i].exchange(0));
    if (h.size() > 160) h.erase(0, h.find(' ', h.size() - 150));
  }
  if (++updates % 300 == 0 || native60_watch::report_now.exchange(false)) native60_watch::Report();
}
#else
void Native60WriteWatchUpdate(uint8_t*) {}
#endif

// Jet race intro pacing, applied once per update after the game step:
// - the 16 bob/sway records 834237A8 + 680k: +4 is an integer angle
//   (0..359, random start, 82506C60 feeds it to a sine) advancing 1 per
//   update at both rates (counter census, logs 291/292), so the jets'
//   handlebar sway ran twice as fast; hold alternate +1 steps during the
//   jet race.
void ApplyNative60JetIntroPacing(uint8_t* base) {
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f || !Native60JetRaceActive()) return;
  // Camera object 0 +1940 counts down 1 per update during the intro
  // fly-over (420 -> 209 over the 7 s intro at 30 Hz): halve 1-step drops.
  const uint32_t objects = REX_LOAD_U32(0x839BC0F0 + 928);
  const uint32_t camera_index = REX_LOAD_U32(0x839BC0F0 + 968);
  static float last_gate = 0.0f;
  static uint32_t last_object = 0;
  if (objects && camera_index == 0) {
    const float gate = Native60LoadFloat(base, objects + 1940);
    if (REX_LOAD_U32(objects + 3580) == 3 && objects == last_object && std::isfinite(gate) &&
        std::fabs((last_gate - gate) - 1.0f) < 0.01f)
      REX_STORE_U32(objects + 1940, std::bit_cast<uint32_t>(last_gate - 0.5f));
    last_gate = Native60LoadFloat(base, objects + 1940);
    last_object = objects;
  }
  static std::array<uint32_t, 16> last_angle{};
  static bool hold = false;
  hold = !hold;
  for (uint32_t k = 0; k < 16; ++k) {
    const uint32_t record = 0x834237A8 + k * 680;
    const uint32_t angle = REX_LOAD_U32(record + 4);
    if (REX_LOAD_U32(record) != 0xFFFF && hold && angle < 360 && last_angle[k] < 360 &&
        angle == (last_angle[k] + 1) % 360)
      REX_STORE_U32(record + 4, last_angle[k]);
    last_angle[k] = REX_LOAD_U32(record + 4);
  }
}
// Jet intro rate scan (diagnostics): snapshots 83300000-83CFFFFF when the
// player's race state 0 frame counter (834D8CA0 +20, 90 per second at both
// rates after pacing) reaches 90, 270 and 450, then logs every float word
// that changed steadily (both intervals within 25% of each other) so the
// 60 and 30 runs can be compared for values advancing twice as fast.
void Native60JetIntroRateScan(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  if (!REXCVAR_GET(doax2_native60_anim_sweep)) return;
  constexpr uint32_t kStart = 0x83300000, kWords = 0xA00000 / 4;
  static std::array<std::vector<uint32_t>, 3> snaps;
  static int taken = 0;
  static bool done = false;
  if (done) return;
  const int32_t state = int32_t(REX_LOAD_U32(0x834D8CA0 + 16));
  const int32_t frames = int32_t(REX_LOAD_U32(0x834D8CA0 + 20));
  static constexpr int32_t kMarks[3] = {90, 270, 450};
  if (state != 0 || taken >= 3 || frames < kMarks[taken]) return;
  snaps[taken].resize(kWords);
  const uint32_t* src = reinterpret_cast<const uint32_t*>(base + kStart);
  std::memcpy(snaps[taken].data(), src, kWords * 4);
  REXLOG_INFO("DOAX2 native60 jet rate scan snapshot {} at frames={} engine_frame={}", taken, frames,
              Doax2EngineFrameCount());
  if (++taken < 3) return;
  done = true;
  uint32_t lines = 0;
  for (uint32_t w = 0; w < kWords && lines < 30000; ++w) {
    // Skip the water height-field buffers (8 x 512 KB from 834FA800).
    const uint32_t address = kStart + w * 4;
    if (address >= 0x834F8000 && address < 0x83900000) continue;
    const float a = std::bit_cast<float>(_byteswap_ulong(snaps[0][w]));
    const float b = std::bit_cast<float>(_byteswap_ulong(snaps[1][w]));
    const float c = std::bit_cast<float>(_byteswap_ulong(snaps[2][w]));
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) continue;
    if (std::fabs(a) > 1e6f || std::fabs(c) > 1e6f) continue;
    const float d1 = b - a, d2 = c - b;
    if (std::fabs(d1) < 1e-4f || std::fabs(d2) < 1e-4f || (d1 > 0) != (d2 > 0)) continue;
    if (std::fabs(d1 - d2) > 0.25f * std::max(std::fabs(d1), std::fabs(d2))) continue;
    // Skip integers stored as words (small ints read as denormals are
    // already rejected by the magnitude test below).
    if (std::fabs(a) < 1e-30f && a != 0.0f) continue;
    REXLOG_INFO("DOAX2 native60 jet rate {:08X} {:.6g} {:.6g} {:.6g}", kStart + w * 4, a, d1, d2);
    ++lines;
  }
  REXLOG_INFO("DOAX2 native60 jet rate scan done: {} steady words", lines);
  for (auto& v : snaps) std::vector<uint32_t>().swap(v);
}
// Jet intro transition scan (diagnostics): during race state 0 (intro) of
// racer 0, every 0.1 s of intro time (race-state frames step 9, 90/s at
// both rates) each float word in 83300000-83CFFFFF (minus water buffers)
// keeps the time of its largest single change; at the end of the intro
// words whose largest change is big are logged with that time, so the
// 60 and 30 runs show which animation value moves early at 60.
void Native60JetTransitionScan(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  if (!REXCVAR_GET(doax2_native60_anim_sweep)) return;
  constexpr uint32_t kStart = 0x83300000, kWords = 0xA00000 / 4;
  static std::vector<uint32_t> prev;
  static std::vector<float> best;
  static std::vector<uint16_t> when;
  static int32_t next_mark = 0;
  static uint16_t samples = 0;
  static bool active = false, done = false;
  if (done) return;
  const int32_t state = int32_t(REX_LOAD_U32(0x834D8CA0 + 16));
  const int32_t frames = int32_t(REX_LOAD_U32(0x834D8CA0 + 20));
  const uint32_t* src = reinterpret_cast<const uint32_t*>(base + kStart);
  if (!active) {
    if (state != 0 || !Native60JetRaceActive() || frames > 9) return;
    prev.assign(src, src + kWords);
    best.assign(kWords, 0.0f);
    when.assign(kWords, 0);
    next_mark = 9;
    samples = 0;
    active = true;
    return;
  }
  if (state != 0) {
    done = true;
    uint32_t lines = 0;
    for (uint32_t w = 0; w < kWords && lines < 30000; ++w) {
      const uint32_t address = kStart + w * 4;
      if (address >= 0x834F8000 && address < 0x83900000) continue;
      if (std::fabs(best[w]) < 0.02f) continue;
      REXLOG_INFO("DOAX2 native60 jet transition {:08X} {} {:.5g} {:.5g}", address, when[w], best[w],
                  std::bit_cast<float>(_byteswap_ulong(prev[w])));
      ++lines;
    }
    REXLOG_INFO("DOAX2 native60 jet transition scan done: {} samples, {} words", samples, lines);
    std::vector<uint32_t>().swap(prev);
    std::vector<float>().swap(best);
    std::vector<uint16_t>().swap(when);
    return;
  }
  if (frames < next_mark) return;
  next_mark += 9;
  ++samples;
  for (uint32_t w = 0; w < kWords; ++w) {
    const uint32_t now = src[w];
    if (now == prev[w]) continue;
    const float a = std::bit_cast<float>(_byteswap_ulong(prev[w]));
    const float b = std::bit_cast<float>(_byteswap_ulong(now));
    prev[w] = now;
    if (!std::isfinite(a) || !std::isfinite(b) || std::fabs(a) > 1e5f || std::fabs(b) > 1e5f) continue;
    const float d = b - a;
    if (std::fabs(d) > std::fabs(best[w])) {
      best[w] = d;
      when[w] = samples;
    }
  }
}
// Jet intro heap scan (diagnostics): snapshots every committed read/write
// guest page in 40000000-BFFFFFFF (heaps + one physical view) when the
// player's race state 0 frame counter (90/s at both rates) reaches 225,
// 414 and 612 (2.5 s, 4.6 s, 6.8 s into the intro) and writes, for every
// float word that changed by more than 0.02, "address v1 d1 d2" to
// logs/jet_heap_<tick>.txt. A value that drops between the first two
// snapshots at 60 but between the last two at 30 is the early pole.
void Native60JetHeapScan(uint8_t* base) {
#if defined(_WIN32)
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  if (!REXCVAR_GET(doax2_native60_anim_sweep)) return;
  struct Region { uint32_t guest; uint32_t bytes; };
  static std::vector<Region> regions;
  static std::vector<uint32_t> snap1, snap2;
  static int taken = 0;
  static bool jet_done = false;
  static int sweeps = 0;
  // F9 (edge) takes the next snapshot anywhere; in the Niki Marina intro the
  // three snapshots are automatic at race-state frames 225/414/612.
  static bool key_was_down = false;
  const bool key_down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
  const bool key_pressed = key_down && !key_was_down;
  key_was_down = key_down;
  const int32_t state = int32_t(REX_LOAD_U32(0x834D8CA0 + 16));
  const int32_t frames = int32_t(REX_LOAD_U32(0x834D8CA0 + 20));
  static constexpr int32_t kMarks[3] = {225, 414, 612};
  const bool jet_mark = !jet_done && state == 0 && Native60JetRaceActive() && frames >= kMarks[taken];
  if (!jet_mark && !key_pressed) return;
  if (jet_mark && taken == 2) jet_done = true;
  if (taken == 0) {
    regions.clear();
    uint64_t total = 0;
    for (uint64_t guest = 0x40000000; guest < 0xC0000000;) {
      MEMORY_BASIC_INFORMATION info{};
      if (!VirtualQuery(base + guest, &info, sizeof(info))) break;
      const uint64_t start = uint64_t(reinterpret_cast<uint8_t*>(info.BaseAddress) - base);
      const uint64_t end = std::min<uint64_t>(start + info.RegionSize, 0xC0000000);
      if (info.State == MEM_COMMIT && (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
          !(info.Protect & PAGE_GUARD) && end > guest) {
        const uint64_t from = std::max<uint64_t>(start, guest);
        regions.push_back({uint32_t(from), uint32_t(end - from)});
        total += end - from;
      }
      guest = std::max<uint64_t>(end, guest + 0x1000);
    }
    REXLOG_INFO("DOAX2 native60 jet heap scan: {} regions, {} MB", regions.size(), total >> 20);
    if (total > (1ull << 30)) { taken = 0; return; }
  }
  std::vector<uint32_t>& target = taken == 0 ? snap1 : snap2;
  if (taken < 2) {
    target.clear();
    for (const Region& r : regions) {
      const uint32_t* src = reinterpret_cast<const uint32_t*>(base + r.guest);
      target.insert(target.end(), src, src + r.bytes / 4);
    }
    REXLOG_INFO("DOAX2 native60 jet heap snapshot {} at frames={}", taken, frames);
    ++taken;
    return;
  }
  taken = 0;
  ++sweeps;
  char module[MAX_PATH]{};
  GetModuleFileNameA(nullptr, module, MAX_PATH);
  std::string dir(module);
  dir = dir.substr(0, dir.find_last_of("\\/") + 1);
  const std::string path = fmt::format("{}logs/anim_sweep_{}_{}.txt", dir,
                                       float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f ? "60" : "30",
                                       sweeps);
  std::ofstream out(path);
  size_t index = 0;
  uint64_t lines = 0;
  for (const Region& r : regions) {
    const uint32_t* now = reinterpret_cast<const uint32_t*>(base + r.guest);
    for (uint32_t w = 0; w < r.bytes / 4; ++w, ++index) {
      const uint32_t a0 = snap1[index], a1 = snap2[index], a2 = now[w];
      if (a0 == a1 && a1 == a2) continue;
      const float v0 = std::bit_cast<float>(_byteswap_ulong(a0));
      const float v1 = std::bit_cast<float>(_byteswap_ulong(a1));
      const float v2 = std::bit_cast<float>(_byteswap_ulong(a2));
      if (!std::isfinite(v0) || !std::isfinite(v1) || !std::isfinite(v2)) continue;
      if (std::fabs(v0) > 1e5f || std::fabs(v1) > 1e5f || std::fabs(v2) > 1e5f) continue;
      if ((v0 != 0.0f && std::fabs(v0) < 1e-20f) || (v2 != 0.0f && std::fabs(v2) < 1e-20f)) continue;
      const float d1 = v1 - v0, d2 = v2 - v1;
      if (std::fabs(d1) + std::fabs(d2) < 0.02f) continue;
      out << fmt::format("{:08X} {:.6g} {:.6g} {:.6g}\n", r.guest + w * 4, v0, d1, d2);
      ++lines;
    }
  }
  REXLOG_INFO("DOAX2 native60 jet heap scan done: {} lines -> {}", lines, path);
  std::vector<uint32_t>().swap(snap1);
  std::vector<uint32_t>().swap(snap2);
#endif
}
// Automatic F10 at fixed jet intro times (diagnostics): the xenos60 renderer
// starts a 60-swap burst on the F10 key edge; pressing it at the same race
// state frames (90/s at both rates) gives matching 60/30 captures.
void Native60JetIntroAutoCapture(uint8_t* base) {
#if defined(_WIN32)
  if (!REXCVAR_GET(doax2_native60_intro_autocapture)) return;
  static int next = 0;
  static constexpr int32_t kMarks[2] = {330, 520};
  if (next >= 2) return;
  const int32_t state = int32_t(REX_LOAD_U32(0x834D8CA0 + 16));
  const int32_t frames = int32_t(REX_LOAD_U32(0x834D8CA0 + 20));
  if (state != 0 || !Native60JetRaceActive() || frames < kMarks[next]) return;
  REXLOG_INFO("DOAX2 native60 intro autocapture {} at frames={}", next + 1, frames);
  ++next;
  std::thread([] {
    keybd_event(VK_F10, 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    keybd_event(VK_F10, 0, KEYEVENTF_KEYUP, 0);
  }).detach();
#endif
}
void ReportNative60PosWriters();
void Native60CountModeUpdate(uint8_t* base);
void Native60JetCameraDump(uint8_t* base);
// Jet intro dump (diagnostics): from the first update the jet race manager
// runs until the race camera (mode 9) starts, or 11 s, the jet control
// records, bodies, rigs and player 0 are sampled every update (every
// other update at 60 Hz, so both rates log 30 samples per second) to
// compare intro animation state between 60 and 30 updates per second.
inline std::array<std::atomic<uint32_t>, 8> native60_characters{};
inline void Native60NoteCharacter(uint32_t character) {
  for (auto& slot : native60_characters) {
    uint32_t expected = slot.load();
    if (expected == character) return;
    if (expected == 0 && slot.compare_exchange_strong(expected, character)) return;
  }
}
void Native60JetIntroDump(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  const uint32_t index = REX_LOAD_U32(0x839BC0F0 + 968);
  const uint32_t objects = REX_LOAD_U32(0x839BC0F0 + 928);
  const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
  const bool race_camera = object && REX_LOAD_U32(object + 3580) == 9;
  const bool half = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f;
  struct Block { const char* name; uint32_t address; uint32_t bytes; };
  static std::vector<Block> blocks = {{"ctl", 0x834CD960, 0x344 * 4},
                                            {"body", 0x834D99C0, 208 * 4},
                                            {"rig", 0x83CA3050, 736 * 4},
                                            {"bones", 0x833B5310, 2048},
                                            {"player0", 0x833B8520, 152},
                                            {"rec", 0x834F7E00, 0xA0 * 4},
                                            {"pose", 0x834271A0, 288 * 8},
                                            {"sway", 0x834237A8, 680 * 4},
                                            {"clock", 0x839865C0, 32},
                                            {"rstate", 0x834D8CA0, 44 * 4},
                                            {"root", 0x83426FD0, 116 * 4},
                                            {"poleblend", 0x833B7330, 16},
                                            {"motion", 0x8342B418, 1512 * 4},
                                            {"models", 0x8342CA30, 412 * 8},
                                            {"blendtab", 0x834D9900, 0xC0}};
  static std::vector<std::vector<uint32_t>> samples(blocks.size());
  static uint32_t collected = 0, updates = 0;
  static bool active = false, done = false;
  const uint32_t limit = 330;  // 11 s (lead-in ~2 s + intro ~7.3 s)
  auto dump = [&]() {
    done = true;
    active = false;
    REXLOG_INFO("DOAX2 native60 jet intro dump: {} samples", collected);
    for (size_t b = 0; b < blocks.size(); ++b) {
      const uint32_t words = blocks[b].bytes / 4;
      for (uint32_t w = 0; w < words; ++w) {
        bool changed = false, is_float = true;
        for (uint32_t i = 0; i < collected; ++i) {
          const uint32_t v = samples[b][i * words + w];
          if (i && v != samples[b][(i - 1) * words + w]) changed = true;
          const float f = std::bit_cast<float>(v);
          if (!std::isfinite(f) || (v && std::fabs(f) < 1e-6f) || std::fabs(f) > 1e6f) is_float = false;
        }
        if (!changed) continue;
        std::string line;
        for (uint32_t i = 0; i < collected; ++i) {
          const uint32_t v = samples[b][i * words + w];
          line += is_float ? fmt::format(" {:.6g}", std::bit_cast<float>(v)) : fmt::format(" {:08X}", v);
        }
        REXLOG_INFO("DOAX2 native60 jet intro {}+{}:{}", blocks[b].name, w * 4, line);
      }
    }
  };
  if (done) return;
  if (!active) {
    if (!Native60JetRaceActive() || race_camera) return;
    active = true;
    collected = 0;
    updates = 0;
    static const char* kCharNames[8] = {"char0", "char1", "char2", "char3", "char4", "char5", "char6", "char7"};
    while (!blocks.empty() && std::string_view(blocks.back().name).rfind("char", 0) == 0) blocks.pop_back();
    for (uint32_t i = 0; i < 8; ++i)
      if (const uint32_t c = native60_characters[i].load())
        blocks.push_back({kCharNames[i], c, 512});
    // Jet pole animation records ([82B498D4] + 2528k), +1240..+1295.
    static const char* kAnimNames[4] = {"char_anim0", "char_anim1", "char_anim2", "char_anim3"};
    if (const uint32_t anim = REX_LOAD_U32(0x82B498D4))
      for (uint32_t k = 0; k < 4; ++k) blocks.push_back({kAnimNames[k], anim + 2528 * k + 1240, 56});
    samples.assign(blocks.size(), {});
    for (auto& b : blocks)
      if (std::string_view(b.name).rfind("char", 0) == 0)
        REXLOG_INFO("DOAX2 native60 jet intro {} = {:08X} (player {})", b.name, b.address,
                    REX_LOAD_U8(b.address + 398));
  }
  if (race_camera || collected >= limit) {
    dump();
    return;
  }
  if (half && (updates++ & 1)) return;
  for (size_t b = 0; b < blocks.size(); ++b)
    for (uint32_t offset = 0; offset < blocks[b].bytes; offset += 4)
      samples[b].push_back(REX_LOAD_U32(blocks[b].address + offset));
  ++collected;
}
// Water slide dump (diagnostics): sub_826A4E00 is the slide update (r3 =
// manager, +8 counts calls; slide state 0x83957D00). While it runs, the
// manager and the state block are sampled 30 times per second at both rates
// (every other call at 60 Hz); when the calls stop (or after 40 s) every word
// that changed is logged as a series to compare 60 vs 30 FPS.
struct Native60SlideDump {
  struct Block { const char* name; uint32_t address; uint32_t bytes; };
  std::vector<Block> blocks;
  std::vector<std::vector<uint32_t>> samples;
  uint32_t collected = 0, calls = 0, idle_updates = 0;
  bool active = false, done = false;
};
Native60SlideDump native60_slide_dump;

void Native60SlideDumpWrite(Native60SlideDump& dump, uint8_t* base) {
  dump.active = false;
  REXLOG_INFO("DOAX2 native60 slide dump: {} samples over {} calls", dump.collected, dump.calls);
  for (size_t b = 0; b < dump.blocks.size(); ++b) {
    const uint32_t words = dump.blocks[b].bytes / 4;
    for (uint32_t w = 0; w < words; ++w) {
      bool changed = false, is_float = true;
      for (uint32_t i = 0; i < dump.collected; ++i) {
        const uint32_t v = dump.samples[b][i * words + w];
        if (i && v != dump.samples[b][(i - 1) * words + w]) changed = true;
        const float f = std::bit_cast<float>(v);
        if (!std::isfinite(f) || (v && std::fabs(f) < 1e-6f) || std::fabs(f) > 1e6f) is_float = false;
      }
      if (!changed) continue;
      std::string line;
      for (uint32_t i = 0; i < dump.collected; ++i) {
        const uint32_t v = dump.samples[b][i * words + w];
        line += is_float ? fmt::format(" {:.6g}", std::bit_cast<float>(v)) : fmt::format(" {:08X}", v);
      }
      REXLOG_INFO("DOAX2 native60 slide {}+{}:{}", dump.blocks[b].name, w * 4, line);
    }
  }
  dump.collected = 0;
  dump.calls = 0;
}

void Native60SlideDumpSample(uint8_t* base, uint32_t manager) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  auto& dump = native60_slide_dump;
  if (dump.done) return;
  dump.idle_updates = 0;
  if (!dump.active) {
    dump.active = true;
    dump.blocks = {{"mgr", manager, 0x200}, {"state", 0x83957D00, 0x400}, {"player0", 0x833B8520, 152}};
    dump.samples.assign(dump.blocks.size(), {});
    REXLOG_INFO("DOAX2 native60 slide dump start: manager={:08X}", manager);
  }
  // Sampled once per slide simulation step (the step runs at 30 Hz at both
  // rates once it is paced, see the sub_826A4E00 hook).
  ++dump.calls;
  for (size_t b = 0; b < dump.blocks.size(); ++b)
    for (uint32_t offset = 0; offset < dump.blocks[b].bytes; offset += 4)
      dump.samples[b].push_back(REX_LOAD_U32(dump.blocks[b].address + offset));
  if (++dump.collected >= 1200) Native60SlideDumpWrite(dump, base);
}

// Called every update: writes the dump once the slide update stops running.
void Native60SlideDumpTick(uint8_t* base) {
  auto& dump = native60_slide_dump;
  if (!dump.active || dump.done) return;
  if (++dump.idle_updates >= 30) Native60SlideDumpWrite(dump, base);
}

// Water slide pacing: the whole slide simulation (physics, input, timers,
// fail checks) advances a fixed step per call, so at 60 Hz it ran twice as
// fast. Run it on alternate updates (30 Hz, the original rate). Motion stays
// smooth: on a simulation update the render-facing values (player 0 transform
// and the slide state block) show the midpoint between the previous and the
// new step; on the following update the exact simulated values are restored
// before anything simulates again.
// Pool Slide session: from the setup call (sub_826A4AB8) until the slide
// phases (state 2 sub_826A4CF0, state 3 sub_826A4E00) stop being called.
uint64_t native60_slide_session_start = 0, native60_slide_last_call = 0;
bool native60_slide_session = false;
inline void Native60SlideNoteCall(const char* phase) {
  const uint64_t frame = Doax2EngineFrameCount();
  if (!native60_slide_session) {
    native60_slide_session = true;
    native60_slide_session_start = frame;
  }
  static const char* last_phase = nullptr;
  if (phase != last_phase) {
    last_phase = phase;
    REXLOG_INFO("DOAX2 native60 slide phase {} at engine_frame={}", phase, frame);
  }
  native60_slide_last_call = frame;
}
// Pool Slide main dispatcher (sub_826A4288) ran this or the previous update.
uint64_t native60_slide_main_frame = ~0ull;
// True while Pool Slide runs (camera shot pacing gate).
inline bool Native60SlideActive() {
  return native60_slide_main_frame != ~0ull &&
         Doax2EngineFrameCount() - native60_slide_main_frame <= 2;
}
// F7 cycles the in-between smoothing: 0 player + slide state, 1 player only,
// 2 off (diagnostics for splash hitching / sound).
int native60_slide_smoothing = 0;
// Experimental bisect: one mode per slide session from smoothing_mode.txt.
std::vector<int> native60_slide_mode_list;
uint32_t native60_slide_mode_session = 0;
inline void Native60SlideNextSessionMode() {
  if (native60_slide_mode_list.empty()) return;
  const size_t i = std::min<size_t>(native60_slide_mode_session, native60_slide_mode_list.size() - 1);
  native60_slide_smoothing = native60_slide_mode_list[i];
  REXLOG_INFO("DOAX2 native60 slide run {} smoothing mode {}", native60_slide_mode_session + 1, native60_slide_smoothing);
  ++native60_slide_mode_session;
}
inline int Native60SlideSmoothing() {
  if (!REXCVAR_GET(doax2_native60_counter_scan) && !REXCVAR_GET(doax2_native60_dev_keys))
    return native60_slide_smoothing;
  // Experimental: starting mode from out/game/smoothing_mode.txt (bisect runs
  // without having to press F7 before the minigame).
  static bool mode_file_read = false;
  if (!mode_file_read) {
    mode_file_read = true;
    if (FILE* f = std::fopen("out/game/smoothing_mode.txt", "r")) {
      int mode = 0;
      while (std::fscanf(f, "%d", &mode) == 1)
        if ((mode >= 0 && mode <= 5) || (mode >= 10 && mode <= 21) || (mode >= 100 && mode <= 32867))
          native60_slide_mode_list.push_back(mode);
      std::fclose(f);
      if (!native60_slide_mode_list.empty()) native60_slide_smoothing = native60_slide_mode_list[0];
      REXLOG_INFO("DOAX2 native60 slide smoothing modes from file: {} (first {})", native60_slide_mode_list.size(),
                  native60_slide_smoothing);
    }
  }
  static bool f7_was_down = false;
  const bool f7_down = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
  if (f7_down && !f7_was_down) {
    native60_slide_smoothing = native60_slide_smoothing >= 4 ? 0 : native60_slide_smoothing + 1;
    static const char* kNames[5] = {"all", "skeleton only (no small blocks)", "small blocks only (no skeleton)",
                                    "off", "slide gameplay only (no intro/countdown/drop/win pose smoothing)"};
    REXLOG_INFO("DOAX2 native60 slide smoothing (F7): {}", kNames[native60_slide_smoothing]);
  }
  f7_was_down = f7_down;
  return native60_slide_smoothing;
}

// Update number of the last slide simulation step (sound slot pacing).
uint64_t native60_slide_sim_frame = ~0ull;
// Update number of the last results phase call (hold scan trigger).
uint64_t native60_slide_results_frame = ~0ull;
// Update number of the last end phase call (hold scan trigger).
uint64_t native60_slide_end_frame = ~0ull;
// Update number on which a paced slide phase (slide, intro, countdown,
// results, end) last actually stepped: the splash emitter follows it.
uint64_t native60_slide_step_frame = ~0ull;

// Extra 30 Hz blocks found by the hold scan (diagnostics test): large
// single-buffered ranges that change only on slide simulation updates (the
// character skeleton block). Smoothed like the state block.
std::mutex native60_slide_auto_mutex;
std::vector<std::pair<uint32_t, uint32_t>> native60_slide_auto_ranges;  // guest, words

struct Native60SlidePacing {
  static constexpr uint32_t kPlayer = 0x833B8520, kState = 0x83957D00;
  static constexpr uint32_t kPlayerFirst = 12, kPlayerEnd = 44, kStateBytes = 0x400;
  std::array<uint32_t, (kPlayerEnd - kPlayerFirst) / 4> player_new{};
  std::array<uint32_t, kStateBytes / 4> state_new{};
  uint64_t last_frame = 0;
  bool odd = false, have_new = false, state_restore = false;
  std::vector<std::pair<uint32_t, uint32_t>> auto_ranges;
  std::vector<uint32_t> auto_new;
  bool auto_restore = false;
  // Only words this smoothing wrote get the exact value back, and only while
  // they still hold the midpoint: restoring every listed word (73 KB incl.
  // the skeleton region) overwrote what other game code updated on the
  // in-between update - no splashes, model flicker (logs 455-458).
  std::vector<uint32_t> auto_shown;
  std::vector<uint8_t> auto_written;
  bool midpoint_active = false;  // midpoints shown, in-between restore pending
  std::vector<uint8_t> auto_changed_prev;
};
Native60SlidePacing native60_slide_pacing;

inline bool Native60InterpolatableFloat(uint32_t a, uint32_t b) {
  const float fa = std::bit_cast<float>(a), fb = std::bit_cast<float>(b);
  auto plain = [](uint32_t v, float f) {
    return std::isfinite(f) && (v == 0 || (std::fabs(f) > 1e-6f && std::fabs(f) < 1e6f));
  };
  return plain(a, fa) && plain(b, fb) && std::fabs(fa - fb) < 50.0f;
}

// Heap pointers (40000000-4FFFFFFF) read as plain floats 2.0..8.6e9; a
// reallocated pointer averaged with the old one crashed the game (log 438).
// A word that looks like a pointer and did not change on the previous step is
// treated as one.
inline bool Native60PointerLike(uint32_t a, uint32_t b) {
  return (a >> 28) == 4 && (b >> 28) == 4 && (a & 7) == 0 && (b & 7) == 0;
}
// F7 (diagnostics) smoothing modes: 0 all, 1 skeleton block only (no small
// blocks), 2 small blocks only (no skeleton), 3 off.
inline bool Native60SmoothRangeEnabled(size_t index, uint32_t words, int mode) {
  const bool skeleton = index >= 8;
  if (mode >= 100) return ((mode - 100) >> index) & 1;
  if (mode >= 10) return int(index) != mode - 10;
  return mode == 0 || mode == 4 || mode == 5 || (mode == 1 && skeleton) || (mode == 2 && !skeleton);
}
// Scanned blocks may hold angles: never midpoint across a +-pi wrap (not used
// for the player / state blocks: blocking large steps there made the girl
// spin, user test after log 461).
inline bool Native60InterpolatableScanned(uint32_t a, uint32_t b) {
  if (!Native60InterpolatableFloat(a, b)) return false;
  const float fa = std::bit_cast<float>(a), fb = std::bit_cast<float>(b);
  if (std::fabs(fa) <= 1.5f && std::fabs(fb) <= 1.5f && std::fabs(fa - fb) > 1.0f) return false;
  return !(std::fabs(fa - fb) > 3.0f && std::fabs(fa) <= 3.2f && std::fabs(fb) <= 3.2f);
}

// Pool Slide render-facing blocks that the paced phases only update every
// other 60 Hz update (hold scans, logs 430/431): the character skeleton block
// ([833FB0EC] + 0x2FE8, 18424 words; the pointer was the same in every
// session) and small state blocks (heading vectors 833A10E4, player0 +0x7C
// and its mirror 83426FF4, ...). The splash particle pools (A9458EA0 /
// A9470EA0 in log 431) are not listed: averaging live particle records made
// the splashes disappear.
inline std::vector<std::pair<uint32_t, uint32_t>> Native60SlideStaticRanges(uint8_t* base) {
  std::vector<std::pair<uint32_t, uint32_t>> ranges = {
      {0x833A10E4, 77}, {0x833B859C, 1}, {0x83426FF4, 1}, {0x834206D8, 26}, {0x834BD640, 3},
      {0x82E2ED64, 10}, {0x83AC5B70, 3}, {0x83AFBCF0, 3}};
  const uint32_t anchor = REX_LOAD_U32(0x833FB0EC);
  // Skeleton region as 4 quarters (static ranges 8..11) so a bisect can
  // switch them separately.
  if (anchor >= 0x40000000 && anchor < 0xBFF00000)
    for (uint32_t q = 0; q < 4; ++q) ranges.push_back({anchor + 0x2FE8 + q * 4606 * 4, 4606});
  return ranges;
}

// Midpoint of two step results. Angles are re-expressed by whole turns
// between steps (player0 +32, the facing: 1.60 -> -4.68 at one step, flicker
// trace log 468): the plain mean (-1.59) turned the girl and the floatie
// ~180 degrees for one frame (the model "flicker"). A difference within 0.5
// of a whole number of turns is treated as the same angle.
inline uint32_t Native60Midpoint(uint32_t a, uint32_t b) {
  const float fa = std::bit_cast<float>(a), fb = std::bit_cast<float>(b);
  float d = fb - fa;
  constexpr float kTurn = 6.28318530718f;
  if (std::fabs(d) > 3.14159265f) {
    const float turns = std::round(d / kTurn);
    if (turns != 0.0f && std::fabs(d - turns * kTurn) < 0.5f) d -= turns * kTurn;
  }
  return std::bit_cast<uint32_t>(fa + 0.5f * d);
}

// Generic in-between smoothing for a phase that runs on alternate updates:
// on a run update the listed floats that the step changed show the midpoint
// of before/after; on the in-between update each word still holding that
// midpoint gets the exact value back (words another system wrote meanwhile
// are left alone).
struct Native60PhaseSmoother;
inline std::vector<Native60PhaseSmoother*> native60_phase_smoothers;
struct Native60PhaseSmoother {
  Native60PhaseSmoother() { native60_phase_smoothers.push_back(this); }
  std::vector<std::pair<uint32_t, uint32_t>> ranges;
  std::vector<uint32_t> after, shown;
  std::vector<uint8_t> written, changed_prev;
  uint64_t last_frame = 0, last_run = 0;
  bool odd = false, pending = false;
  // Player0 position (+12/+16/+20) also gets the root-motion delta added by
  // 82510218 every update (write watch, log 447): with additive_player the
  // restore keeps that delta (exact + what was added since the midpoint)
  // instead of leaving the stale midpoint (drop scene stayed at half rate).
  bool additive_player = false;
  bool sets_step_frame = true;  // intro / countdown: false (emitter unpaced, v1.0.1)
  void Flush(uint8_t* base) {
    if (!pending) return;
    size_t k = 0;
    for (const auto& [guest, words] : ranges)
      for (uint32_t i = 0; i < words; ++i, ++k) {
        if (!written[k]) continue;
        const uint32_t addr = guest + i * 4;
        const uint32_t now = REX_LOAD_U32(addr);
        if (now == shown[k]) {
          REX_STORE_U32(addr, after[k]);
        } else if (additive_player && addr >= 0x833B852C && addr <= 0x833B8534) {
          const float added = std::bit_cast<float>(now) - std::bit_cast<float>(shown[k]);
          if (std::isfinite(added) && std::fabs(added) < 5.0f)
            REX_STORE_U32(addr, std::bit_cast<uint32_t>(std::bit_cast<float>(after[k]) + added));
        }
      }
    pending = false;
  }
  template <typename F>
  void Update(uint8_t* base, bool include_player_state, F&& run) {
    if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) {
      run();
      return;
    }
    const uint64_t frame = Doax2EngineFrameCount();
    if (frame != last_frame + 1) {
      odd = false;
      pending = false;
    }
    last_frame = frame;
    if (odd) {
      odd = false;
      if (pending) {
        size_t k = 0;
        for (const auto& [guest, words] : ranges)
          for (uint32_t i = 0; i < words; ++i, ++k)
            if (written[k] && REX_LOAD_U32(guest + i * 4) == shown[k]) REX_STORE_U32(guest + i * 4, after[k]);
        pending = false;
      }
      return;
    }
    odd = true;
    if (sets_step_frame) native60_slide_step_frame = frame;
    Flush(base);
    const bool continuous = last_run != 0 && last_run + 2 == frame;
    last_run = frame;
    ranges = Native60SlideStaticRanges(base);
    if (include_player_state) {
      ranges.push_back({0x833B8520 + 12, 8});
      ranges.push_back({0x83957D00, 0x100});
    }
    std::vector<uint32_t> before;
    for (const auto& [guest, words] : ranges)
      for (uint32_t i = 0; i < words; ++i) before.push_back(REX_LOAD_U32(guest + i * 4));
    run();
    after.clear();
    for (const auto& [guest, words] : ranges)
      for (uint32_t i = 0; i < words; ++i) after.push_back(REX_LOAD_U32(guest + i * 4));
    shown.assign(after.size(), 0);
    written.assign(after.size(), 0);
    pending = false;
    const int mode = Native60SlideSmoothing();
    if (changed_prev.size() != after.size()) changed_prev.assign(after.size(), 0);
    const bool phases_off = mode == 3 || mode == 4 || (mode >= 100 && ((mode - 100) & 4096));
    if (phases_off || !continuous) {
      for (size_t j = 0; j < after.size(); ++j) changed_prev[j] = before[j] != after[j];
      return;
    }
    size_t k = 0;
    const size_t statics = ranges.size() - (include_player_state ? 2 : 0);
    for (size_t r = 0; r < ranges.size(); ++r) {
      const auto [guest, words] = ranges[r];
      const bool enabled = r >= statics || Native60SmoothRangeEnabled(r, words, mode);
      for (uint32_t i = 0; i < words; ++i, ++k) {
        const bool changed = before[k] != after[k];
        const bool pointer = Native60PointerLike(before[k], after[k]) && !changed_prev[k];
        changed_prev[k] = changed;
        if (!enabled || !changed || pointer || !Native60InterpolatableScanned(before[k], after[k])) continue;
        shown[k] = Native60Midpoint(before[k], after[k]);
        written[k] = 1;
        REX_STORE_U32(guest + i * 4, shown[k]);
        pending = true;
      }
    }
  }
};

// Pool Slide main dispatcher: mgr+0 selects intro (1, sub_826A4800),
// countdown (2), slide (3), results (4, sub_826A75A8) and the end phase (7,
// sub_826A7C70). Every phase counts fixed per-update steps.
// Puts every pending midpoint back to the exact step result. Called before
// the slide dispatcher each update (so a midpoint never survives into another
// phase, the setup of a new run or a new session) and from the game step when
// the dispatcher stopped running.
void Native60SlideFlushMidpoints(uint8_t* base) {
  auto& pacing = native60_slide_pacing;
  using P = Native60SlidePacing;
  if (pacing.midpoint_active) {
    for (uint32_t i = 0; i < pacing.player_new.size(); ++i)
      REX_STORE_U32(P::kPlayer + P::kPlayerFirst + i * 4, pacing.player_new[i]);
    if (pacing.state_restore)
      for (uint32_t i = 0; i < pacing.state_new.size(); ++i) REX_STORE_U32(P::kState + i * 4, pacing.state_new[i]);
    if (pacing.auto_restore) {
      size_t k = 0;
      for (const auto& [guest, n] : pacing.auto_ranges)
        for (uint32_t i = 0; i < n && k < pacing.auto_written.size(); ++i, ++k)
          if (pacing.auto_written[k] && REX_LOAD_U32(guest + i * 4) == pacing.auto_shown[k])
            REX_STORE_U32(guest + i * 4, pacing.auto_new[k]);
    }
    pacing.midpoint_active = false;
  }
  for (Native60PhaseSmoother* sm : native60_phase_smoothers) sm->Flush(base);
}

REX_HOOK_RAW(sub_826A4288) {
  native60_slide_main_frame = Doax2EngineFrameCount();
  {
    const int m = native60_slide_smoothing;
    if (!(m >= 100 && ((m - 100) & 16384))) Native60SlideFlushMidpoints(base);
  }
  __imp__sub_826A4288(ctx, base);
}
// Runs a phase update on alternate updates at 60 Hz (30 Hz, original rate).
struct Native60AlternateGate {
  uint64_t last_frame = 0;
  bool odd = false;
  bool Run() {
    if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) return true;
    const uint64_t frame = Doax2EngineFrameCount();
    if (frame != last_frame + 1) odd = false;
    last_frame = frame;
    odd = !odd;
    return odd;
  }
};
#define DOAX2_NATIVE60_SLIDE_PHASE(name, label) \
  REX_HOOK_RAW(name) { \
    static Native60AlternateGate gate; \
    Native60SlideNoteCall(label); \
    if (gate.Run()) __imp__##name(ctx, base); \
  }
// Intro log: mgr+4 (0 = waiting for the girl's entry, 1 = pan 2 countdown),
// mgr+8 (200 -> 100: second half of pan 2, 0: setup) and the skip bit the
// intro checks (pad flags & 0x10 -> straight to setup).
inline void Native60SlideNoteIntro(const PPCContext& ctx, uint8_t* base) {
  const uint32_t manager = ctx.r3.u32;
  const uint64_t frame = Doax2EngineFrameCount();
  const uint32_t state = REX_LOAD_U32(manager + 4), countdown = REX_LOAD_U32(manager + 8);
  const uint32_t pad = REX_LOAD_U32(0x83426238) != 0
                           ? REX_LOAD_U32(0x834A3508 + REX_LOAD_U32(0x83426248) * 44 + 8)
                           : REX_LOAD_U32(0x839848D8 + 8);
  static uint32_t last_state = ~0u;
  static uint64_t last_frame = 0;
  if (frame != last_frame + 1) {
    last_state = ~0u;
    if (last_frame == 0 || frame - last_frame > 60) {
      Native60SlideSmoothing();  // read the mode file once
      Native60SlideNextSessionMode();
    }
  }
  last_frame = frame;
  const bool log_on = REXCVAR_GET(doax2_native60_dev_keys) || REXCVAR_GET(doax2_native60_counter_scan);
  if (log_on && (state != last_state || countdown % 20 == 0 || countdown == 100 || (pad & 0x10))) {
    static uint32_t lines = 0;
    if (++lines <= 300)
      REXLOG_INFO("DOAX2 native60 slide intro: frame={} state={} countdown={} skip_bit={}", frame, state,
                  countdown, (pad & 0x10) ? 1 : 0);
  }
  last_state = state;
}

// Intro, results (win pose) and end phases: alternate updates with the
// in-between smoothing (they showed the girl and raft at 30 Hz).
#define DOAX2_NATIVE60_SLIDE_SMOOTH_PHASE(name, label) \
  REX_HOOK_RAW(name) { \
    static Native60PhaseSmoother smoother; \
    smoother.sets_step_frame = false; \
    Native60SlideNoteIntro(ctx, base); \
    Native60SlideNoteCall(label); \
    smoother.Update(base, true, [&] { __imp__##name(ctx, base); }); \
  }
DOAX2_NATIVE60_SLIDE_SMOOTH_PHASE(sub_826A4800, "intro")
// Results phase = the drop into the lake after the slide (89 updates at 60,
// then the end phase / win pose). Per call: mgr+8 countdown -1 (15: effect,
// 0: next phase), t = clamp((mgr+8 - 30) * [820B8E8C], [823E9434],
// [82063DE0]) -> 826A69A8 (scene / camera blend), a random camera shake
// (8273FAB8 -> 825CA6E0). Run on alternate updates it showed the scene at
// half rate and the shake on/off every other frame (camera zigzag, log 444).
// Now it runs every update: on the in-between update the countdown is
// pre-incremented (the phase's own -1 leaves it unchanged; the 15 / 0
// triggers are never re-fired: that update is held instead) and 826A69A8 gets
// t for countdown - 0.5. F5 (diagnostics) switches back to the old pacing.
bool native60_drop_inbetween = false;
bool native60_drop_end_logged = false;
REX_HOOK_RAW(sub_826A75A8) {
  static Native60PhaseSmoother smoother;
  smoother.additive_player = true;
  static bool every_update = false;
  if (REXCVAR_GET(doax2_native60_counter_scan)) {
    static bool f5_was_down = false;
    const bool f5_down = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (f5_down && !f5_was_down) {
      every_update = !every_update;
      REXLOG_INFO("DOAX2 native60 drop scene pacing (F5): {}",
                  every_update ? "every update (countdown halved)" : "alternate updates + smoothing");
    }
    f5_was_down = f5_down;
  }
  Native60SlideNoteCall("results");
  const uint64_t frame = Doax2EngineFrameCount();
  const uint32_t manager = ctx.r3.u32;
  static uint64_t last_frame = 0;
  static bool step = false;
  if (frame != last_frame + 1) {
    step = false;
    native60_drop_end_logged = false;
    REXLOG_INFO("DOAX2 native60 drop scene start: engine_frame={} countdown={} success={}", frame,
                REX_LOAD_U32(manager + 8), REX_LOAD_U32(manager + 12));
    native60_watch::report_now = true;  // writers before the drop
  }
  last_frame = frame;
  native60_slide_results_frame = frame;
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f || !every_update) {
    smoother.Update(base, true, [&] { __imp__sub_826A75A8(ctx, base); });
    return;
  }
  step = !step;
  if (step) {
    native60_slide_step_frame = frame;
    __imp__sub_826A75A8(ctx, base);
  } else {
    const uint32_t countdown = REX_LOAD_U32(manager + 8);
    if (countdown == 15 || countdown == 0) return;  // never re-fire a trigger
    REX_STORE_U32(manager + 8, countdown + 1);
    native60_drop_inbetween = true;
    __imp__sub_826A75A8(ctx, base);
    native60_drop_inbetween = false;
  }
  if (REX_LOAD_U32(manager + 8) <= 1 && !native60_drop_end_logged) {
    native60_drop_end_logged = true;
    REXLOG_INFO("DOAX2 native60 drop scene end: engine_frame={}", frame);
    native60_watch::report_now = true;  // writers during the drop
  }
}

// Scene / camera blend of the drop: fractional t on the in-between update.
REX_EXTERN(__imp__sub_826A69A8);
REX_HOOK_RAW(sub_826A69A8) {
  if (native60_drop_inbetween && uint32_t(ctx.lr) == 0x826A7634) {
    const uint32_t manager = ctx.r3.u32;
    const float k = Native60LoadFloat(base, 0x820B8E8C);
    const float hi = Native60LoadFloat(base, 0x82063DE0), lo = Native60LoadFloat(base, 0x823E9434);
    float t = (float(int32_t(REX_LOAD_U32(manager + 8)) - 30) - 0.5f) * k;
    if (t > hi) t = hi;
    if (t < lo) t = lo;
    ctx.f2.f64 = double(t);
  }
  __imp__sub_826A69A8(ctx, base);
}
REX_HOOK_RAW(sub_826A7C70) {
  static Native60PhaseSmoother smoother;
  Native60SlideNoteCall("end");
  native60_slide_end_frame = Doax2EngineFrameCount();
  smoother.Update(base, true, [&] { __imp__sub_826A7C70(ctx, base); });
}

// Slide setup (state 2 entry): mgr+0 = 2, mgr+8 = a 90-update countdown.
REX_HOOK_RAW(sub_826A4AB8) {
  native60_slide_session = false;
  Native60SlideNoteCall("setup");
  __imp__sub_826A4AB8(ctx, base);
}

// State 2 (pre-slide countdown, decrements mgr+8 once per call): run it on
// alternate updates at 60 Hz like the slide itself.
REX_HOOK_RAW(sub_826A4CF0) {
  Native60SlideNoteCall("countdown");
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) {
    __imp__sub_826A4CF0(ctx, base);
    return;
  }
  static Native60PhaseSmoother smoother;
  smoother.sets_step_frame = false;
  smoother.Update(base, true, [&] { __imp__sub_826A4CF0(ctx, base); });
}

// Sound call log (diagnostics, Pool Slide): every call into the sound slot
// functions during the slide session with the update number, arguments and
// caller, to compare 60 vs 30 FPS (robotic water sound after the first jump).
inline void Native60SlideSoundLog(const char* name, const PPCContext& ctx) {
  if (!REXCVAR_GET(doax2_native60_counter_scan) || !Native60SlideActive()) return;
  static std::atomic<uint32_t> lines{0};
  if (lines.fetch_add(1) >= 6000) return;
  REXLOG_INFO("DOAX2 native60 slide sound {}: frame={} sim={} r3={:X} r4={:X} r5={:X} r6={:X} r7={:X} r8={:X} f1={:.4g} f2={:.4g} lr={:08X}",
              name, Doax2EngineFrameCount(), native60_slide_sim_frame == Doax2EngineFrameCount() ? 1 : 0,
              ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.f1.f64, ctx.f2.f64,
              uint32_t(ctx.lr));
}
#define DOAX2_NATIVE60_SLIDE_SOUND(name) \
  REX_HOOK_RAW(name) { \
    Native60SlideSoundLog(#name, ctx); \
    __imp__##name(ctx, base); \
  }
DOAX2_NATIVE60_SLIDE_SOUND(sub_826F42B0)
DOAX2_NATIVE60_SLIDE_SOUND(sub_826F43F0)
DOAX2_NATIVE60_SLIDE_SOUND(sub_826F4518)
DOAX2_NATIVE60_SLIDE_SOUND(sub_826F4650)
DOAX2_NATIVE60_SLIDE_SOUND(sub_826F46C8)

// Slide effects (census 60 vs 30): sub_825F83D0 emits and moves the splash
// particles (825F6BA8/825F75A0/825F7138/825F7960/825F7F08 -> 82672E40 ->
// 826725F8/82672D10), a fixed step per call, once per update. During the
// paced slide it runs only on simulation updates (the original pairing):
// particles had run at double speed (choppy water, robotic splash sound).
// While midpoints are shown, game logic outside the paced step (the splash
// emitter) must see the exact step results: the halfway state stopped the
// splashes from spawning (they disappeared with the smoothing, logs 433-437).
struct Native60ExactSwap {
  struct Word { uint32_t addr, shown, exact; };
  std::vector<Word> words;
  void Add(uint8_t* base, uint32_t addr, uint32_t exact) {
    const uint32_t shown = REX_LOAD_U32(addr);
    if (shown == exact) return;
    words.push_back({addr, shown, exact});
    REX_STORE_U32(addr, exact);
  }
  void Begin(uint8_t* base) {
    auto& pacing = native60_slide_pacing;
    using P = Native60SlidePacing;
    if (pacing.midpoint_active) {
      for (uint32_t i = 0; i < pacing.player_new.size(); ++i)
        Add(base, P::kPlayer + P::kPlayerFirst + i * 4, pacing.player_new[i]);
      if (pacing.state_restore)
        for (uint32_t i = 0; i < pacing.state_new.size(); ++i) Add(base, P::kState + i * 4, pacing.state_new[i]);
      if (pacing.auto_restore) {
        size_t k = 0;
        for (const auto& [guest, n] : pacing.auto_ranges)
          for (uint32_t i = 0; i < n && k < pacing.auto_written.size(); ++i, ++k)
            if (pacing.auto_written[k] && REX_LOAD_U32(guest + i * 4) == pacing.auto_shown[k])
              Add(base, guest + i * 4, pacing.auto_new[k]);
      }
    }
    for (Native60PhaseSmoother* sm : native60_phase_smoothers) {
      if (!sm->pending) continue;
      size_t k = 0;
      for (const auto& [guest, n] : sm->ranges)
        for (uint32_t i = 0; i < n; ++i, ++k)
          if (sm->written[k] && REX_LOAD_U32(guest + i * 4) == sm->shown[k]) Add(base, guest + i * 4, sm->after[k]);
    }
  }
  void End(uint8_t* base) {
    // Put the midpoints back where the emitter left the exact value alone.
    for (const Word& w : words)
      if (REX_LOAD_U32(w.addr) == w.exact) REX_STORE_U32(w.addr, w.shown);
  }
};

REX_HOOK_RAW(sub_825F83D0) {
  const uint64_t frame = Doax2EngineFrameCount();
  // During any paced slide phase the emitter steps with that phase (the
  // results phase - the drop into the lake - had it running every update).
  const bool slide_paced = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f &&
                           native60_slide_step_frame != ~0ull && frame - native60_slide_step_frame <= 4;
  if (REXCVAR_GET(doax2_native60_counter_scan) && Native60SlideActive()) {
    static uint64_t window = 0, calls = 0, skips = 0, last_report = 0;
    ++calls;
    if (slide_paced && ((frame ^ native60_slide_step_frame) & 1)) ++skips;
    if (frame - last_report >= 120) {
      REXLOG_INFO("DOAX2 native60 splash emitter 825F83D0: frame={} calls={} skipped={} ran={} paced={} step_frame={} r3={:08X} lr={:08X}",
                  frame, calls, skips, calls - skips, slide_paced ? 1 : 0, native60_slide_step_frame, ctx.r3.u32,
                  uint32_t(ctx.lr));
      calls = skips = 0;
      last_report = frame;
      (void)window;
    }
  }
  if (slide_paced && ((frame ^ native60_slide_step_frame) & 1)) return;
  Native60ExactSwap swap;
  const int m = native60_slide_smoothing;
  const bool use_swap = slide_paced && m != 5 && !(m >= 100 && ((m - 100) & 8192));
  if (use_swap) swap.Begin(base);
  __imp__sub_825F83D0(ctx, base);
  if (use_swap) swap.End(base);
}

REX_HOOK_RAW(sub_826A4E00) {
  const uint32_t manager = ctx.r3.u32;
  Native60SlideNoteCall("slide");
  native60_slide_gameplay_started.store(true);
  auto& pacing = native60_slide_pacing;
  // Diagnostics: F8 switches the slide pacing off/on live (A/B for the
  // robotic water sound).
  static bool pacing_on = true;
  if (REXCVAR_GET(doax2_native60_counter_scan)) {
    static bool f8_was_down = false;
    const bool f8_down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8_down && !f8_was_down) {
      pacing_on = !pacing_on;
      REXLOG_INFO("DOAX2 native60 slide pacing (F8): {}", pacing_on ? "ON" : "OFF (every update, 2x)");
    }
    f8_was_down = f8_down;
  }
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f || !pacing_on) {
    __imp__sub_826A4E00(ctx, base);
    Native60SlideDumpSample(base, manager);
    return;
  }
  // A gap in the calls (slide restarted) begins on a simulation update.
  const uint64_t frame = Doax2EngineFrameCount();
  if (frame != pacing.last_frame + 1) {
    pacing.odd = false;
    pacing.have_new = false;
    pacing.midpoint_active = false;
  }
  pacing.last_frame = frame;
  using P = Native60SlidePacing;
  if (pacing.odd) {
    // In-between update: restore the exact step results; no simulation.
    pacing.odd = false;
    pacing.midpoint_active = false;
    if (pacing.have_new) {
      for (uint32_t i = 0; i < pacing.player_new.size(); ++i)
        REX_STORE_U32(P::kPlayer + P::kPlayerFirst + i * 4, pacing.player_new[i]);
      if (pacing.state_restore)
        for (uint32_t i = 0; i < pacing.state_new.size(); ++i)
          REX_STORE_U32(P::kState + i * 4, pacing.state_new[i]);
      if (pacing.auto_restore) {
        size_t k = 0;
        for (const auto& [guest, words] : pacing.auto_ranges)
          for (uint32_t i = 0; i < words && k < pacing.auto_written.size(); ++i, ++k)
            if (pacing.auto_written[k] && REX_LOAD_U32(guest + i * 4) == pacing.auto_shown[k])
              REX_STORE_U32(guest + i * 4, pacing.auto_new[k]);
      }
    }
    return;
  }
  pacing.odd = true;
  pacing.midpoint_active = false;
  native60_slide_sim_frame = frame;
  native60_slide_step_frame = frame;
  std::array<uint32_t, std::tuple_size_v<decltype(P::player_new)>> player_old{};
  std::array<uint32_t, std::tuple_size_v<decltype(P::state_new)>> state_old{};
  for (uint32_t i = 0; i < player_old.size(); ++i)
    player_old[i] = REX_LOAD_U32(P::kPlayer + P::kPlayerFirst + i * 4);
  for (uint32_t i = 0; i < state_old.size(); ++i) state_old[i] = REX_LOAD_U32(P::kState + i * 4);
  {
    std::lock_guard lock(native60_slide_auto_mutex);
    native60_slide_auto_ranges = Native60SlideStaticRanges(base);
    if (pacing.auto_ranges != native60_slide_auto_ranges) {
      pacing.auto_ranges = native60_slide_auto_ranges;
      pacing.auto_new.clear();
      pacing.auto_restore = false;
    }
  }
  std::vector<uint32_t> auto_old;
  for (const auto& [guest, words] : pacing.auto_ranges)
    for (uint32_t i = 0; i < words; ++i) auto_old.push_back(REX_LOAD_U32(guest + i * 4));
  __imp__sub_826A4E00(ctx, base);
  Native60SlideDumpSample(base, manager);
  for (uint32_t i = 0; i < pacing.player_new.size(); ++i)
    pacing.player_new[i] = REX_LOAD_U32(P::kPlayer + P::kPlayerFirst + i * 4);
  for (uint32_t i = 0; i < pacing.state_new.size(); ++i)
    pacing.state_new[i] = REX_LOAD_U32(P::kState + i * 4);
  const int smoothing = Native60SlideSmoothing();
  const bool had_auto = pacing.auto_new.size() == auto_old.size() && !auto_old.empty();
  pacing.auto_new.clear();
  for (const auto& [guest, words] : pacing.auto_ranges)
    for (uint32_t i = 0; i < words; ++i) pacing.auto_new.push_back(REX_LOAD_U32(guest + i * 4));
  const bool interpolate = pacing.have_new && smoothing != 3;
  pacing.have_new = true;
  pacing.state_restore = false;
  pacing.auto_restore = false;
  pacing.auto_written.clear();
  if (!interpolate) return;
  auto midpoint = [](uint32_t a, uint32_t b) { return Native60Midpoint(a, b); };
  for (uint32_t i = 0; i < player_old.size(); ++i)
    if (Native60InterpolatableFloat(player_old[i], pacing.player_new[i]))
      REX_STORE_U32(P::kPlayer + P::kPlayerFirst + i * 4, midpoint(player_old[i], pacing.player_new[i]));
  if (smoothing != 3) {
    pacing.state_restore = true;
    for (uint32_t i = 0; i < state_old.size(); ++i)
      if (Native60InterpolatableFloat(state_old[i], pacing.state_new[i]))
        REX_STORE_U32(P::kState + i * 4, midpoint(state_old[i], pacing.state_new[i]));
    if (had_auto || !auto_old.empty()) {
      pacing.auto_restore = true;
      if (pacing.auto_changed_prev.size() != auto_old.size()) pacing.auto_changed_prev.assign(auto_old.size(), 0);
      pacing.auto_written.assign(auto_old.size(), 0);
      pacing.auto_shown.assign(auto_old.size(), 0);
      size_t k = 0;
      for (size_t r = 0; r < pacing.auto_ranges.size(); ++r) {
        const auto [guest, words] = pacing.auto_ranges[r];
        const bool enabled = Native60SmoothRangeEnabled(r, words, smoothing);
        for (uint32_t i = 0; i < words; ++i, ++k) {
          const uint32_t a = auto_old[k], b = pacing.auto_new[k];
          const bool changed = a != b;
          const bool pointer = Native60PointerLike(a, b) && !pacing.auto_changed_prev[k];
          pacing.auto_changed_prev[k] = changed;
          if (enabled && changed && !pointer && Native60InterpolatableScanned(a, b)) {
            pacing.auto_shown[k] = midpoint(a, b);
            pacing.auto_written[k] = 1;
            REX_STORE_U32(guest + i * 4, pacing.auto_shown[k]);
          }
        }
      }
    }
  }
  pacing.midpoint_active = true;
  static uint64_t logged = 0;
  if (++logged <= 3)
    REXLOG_INFO("DOAX2 native60 slide pacing: simulation on alternate updates, midpoint shown "
                "(engine_frame={})", frame);
}

// Pool Slide hold scan (diagnostics): the whole minigame looks stuttery
// because the slide phases run on alternate updates and only player0
// +12..+44 and the 0x83957D00 block get in-between midpoints. This finds
// everything else that moves at 30 Hz: 9 consecutive per-update snapshots of
// every committed read/write guest page in 40000000-BFFFFFFF, one change bit
// per word per update; words that change on alternate updates only (pattern
// 0x55/0xAA) are written as merged ranges to logs/slide_hold_<n>.txt, with
// counts of every-update movers for comparison. Triggers: slide gameplay
// +200 and +700 updates, results (win pose) +120 updates.
namespace native60_hold_scan {
struct Region { uint32_t guest; uint32_t bytes; };
inline std::vector<Region> regions;
inline std::vector<uint32_t> prev;
inline std::vector<uint8_t> pattern;
inline int step = -1;
inline int scans = 0;
inline uint64_t frames[9]{};
inline uint8_t sims[9]{};
inline const char* label = "";
inline uint64_t gameplay_first = 0, results_first = 0, end_first = 0, last_seen = 0;
inline uint32_t done_mask = 0;
inline std::vector<uint32_t> particle_dump;
inline int particle_dump_left = 0;
}  // namespace native60_hold_scan
void Native60SlideHoldScan(uint8_t* base) {
#if defined(_WIN32)
  using namespace native60_hold_scan;
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  const uint64_t frame = Doax2EngineFrameCount();
  if (step < 0 && particle_dump_left > 0) {
    --particle_dump_left;
    for (const uint32_t block : particle_dump) {
      // 96 words from 0x40 before the first held word (record start).
      std::string line;
      for (uint32_t i = 0; i < 96; ++i) {
        const uint32_t v = REX_LOAD_U32(block - 0x40 + i * 4);
        const float fv = std::bit_cast<float>(v);
        line += (std::isfinite(fv) && std::fabs(fv) > 1e-6f && std::fabs(fv) < 1e6f) ? fmt::format(" {:.5g}", fv)
                                                                                     : fmt::format(" #{:08X}", v);
      }
      REXLOG_INFO("DOAX2 native60 particle dump frame={} sim={} block={:08X}:{}", frame,
                  native60_slide_sim_frame == frame ? 1 : 0, block - 0x40, line);
    }
  }
  if (step < 0) {
    if (!Native60SlideActive()) return;
    if (frame - last_seen > 600) {  // new slide session
      gameplay_first = results_first = end_first = 0;
      done_mask = 0;
      std::lock_guard lock(native60_slide_auto_mutex);
      native60_slide_auto_ranges.clear();
    }
    last_seen = frame;
    const bool sim_running = native60_slide_sim_frame != ~0ull && frame - native60_slide_sim_frame <= 2;
    const uint32_t mgr_state = REX_LOAD_U32(0x83957D00);  // unused if not the manager
    (void)mgr_state;
    if (sim_running && !gameplay_first) gameplay_first = frame;
    const bool results = native60_slide_results_frame != ~0ull && frame - native60_slide_results_frame <= 2;
    if (results && !results_first) results_first = frame;
    const bool ending = native60_slide_end_frame != ~0ull && frame - native60_slide_end_frame <= 2;
    if (ending && !end_first) end_first = frame;
    int which = -1;
    if (false) which = 0;
    else if (gameplay_first && sim_running && frame - gameplay_first == 700 && !(done_mask & 2)) which = 1;
    else if (results_first && results && frame - results_first == 30 && !(done_mask & 4)) which = 2;
    else if (end_first && ending && frame - end_first == 60 && !(done_mask & 8)) which = 3;
    which = -1;  // scans retired (static smoothing list); trace below instead
    if (which < 0) return;
    if (scans >= 9) return;
    done_mask |= 1u << which;
    static const char* kLabels[4] = {"gameplay+200 (before landing)", "gameplay+700 (after landing)",
                                     "results+30 (drop into the lake)", "end+60 (win pose)"};
    label = kLabels[which];
    regions.clear();
    uint64_t total = 0;
    for (uint64_t guest = 0x40000000; guest < 0xC0000000;) {
      MEMORY_BASIC_INFORMATION info{};
      if (!VirtualQuery(base + guest, &info, sizeof(info))) break;
      const uint64_t start = uint64_t(reinterpret_cast<uint8_t*>(info.BaseAddress) - base);
      const uint64_t end = std::min<uint64_t>(start + info.RegionSize, 0xC0000000);
      const bool stack = start >= 0x70000000 && start < 0x80000000;
      const bool gpu = start >= 0xB0000000;
      if (!stack && !gpu && info.State == MEM_COMMIT && (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
          !(info.Protect & PAGE_GUARD) && end > guest) {
        const uint64_t from = std::max<uint64_t>(start, guest);
        regions.push_back({uint32_t(from), uint32_t(end - from)});
        total += end - from;
      }
      guest = std::max<uint64_t>(end, guest + 0x1000);
    }
    if (total > (1ull << 30)) return;
    prev.clear();
    prev.reserve(total / 4);
    for (const Region& r : regions) {
      const uint32_t* src = reinterpret_cast<const uint32_t*>(base + r.guest);
      prev.insert(prev.end(), src, src + r.bytes / 4);
    }
    pattern.assign(prev.size(), 0);
    step = 0;
    frames[0] = frame;
    sims[0] = native60_slide_sim_frame == frame ? 1 : 0;
    REXLOG_INFO("DOAX2 native60 slide hold scan {} ({}) started at engine_frame={}: {} regions, {} MB",
                scans + 1, label, frame, regions.size(), total >> 20);
    return;
  }
  for (const Region& r : regions) {
    // Regions split and merge as neighbours change; only require every 64 KB
    // granule of the snapshot to still be committed read/write.
    MEMORY_BASIC_INFORMATION info{};
    const bool ok = VirtualQuery(base + r.guest, &info, sizeof(info)) && info.State == MEM_COMMIT &&
                    (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(info.Protect & PAGE_GUARD);
    if (!ok) {
      REXLOG_INFO("DOAX2 native60 slide hold scan aborted: region {:08X}+{:X} changed", r.guest, r.bytes);
      std::vector<uint32_t>().swap(prev);
      std::vector<uint8_t>().swap(pattern);
      step = -1;
      return;
    }
  }
  ++step;
  frames[step] = frame;
  sims[step] = native60_slide_sim_frame == frame ? 1 : 0;
  size_t w = 0;
  const uint8_t bit = uint8_t(1u << (step - 1));
  for (const Region& r : regions) {
    const uint32_t* src = reinterpret_cast<const uint32_t*>(base + r.guest);
    for (uint32_t i = 0; i < r.bytes / 4; ++i, ++w) {
      const uint32_t now = src[i];
      if (now != prev[w]) {
        pattern[w] |= bit;
        prev[w] = now;
      }
    }
  }
  if (step < 8) return;
  ++scans;
  char module[MAX_PATH]{};
  GetModuleFileNameA(nullptr, module, MAX_PATH);
  std::string dir(module);
  dir = dir.substr(0, dir.find_last_of("\\/") + 1);
  const std::string path = fmt::format("{}logs/slide_hold_{}.txt", dir, scans);
  FILE* f = std::fopen(path.c_str(), "w");
  uint64_t held = 0, every = 0, other = 0;
  if (f) {
    std::fprintf(f, "# %s\n# frames:", label);
    for (int i = 0; i <= 8; ++i) std::fprintf(f, " %llu%s", (unsigned long long)frames[i], sims[i] ? "s" : "");
    std::fprintf(f, "\n# range_start range_end words pattern sample_floats...\n");
  }
  // Merge held words (strict alternation) into ranges (gap <= 64 bytes).
  w = 0;
  uint32_t run_start = 0, run_end = 0, run_words = 0;
  uint8_t run_pattern = 0;
  auto flush = [&] {
    if (!run_words || !f) return;
    std::fprintf(f, "%08X %08X %u %02X", run_start, run_end, run_words, run_pattern);
    for (uint32_t a = run_start, n = 0; a <= run_end && n < 6; a += 4, ++n)
      std::fprintf(f, " %.4g", std::bit_cast<float>(REX_LOAD_U32(a)));
    std::fprintf(f, "\n");
  };
  for (const Region& r : regions) {
    for (uint32_t i = 0; i < r.bytes / 4; ++i, ++w) {
      const uint8_t pt = pattern[w];
      if (!pt) continue;
      if (pt == 0xFF) { ++every; continue; }
      if (pt != 0x55 && pt != 0xAA) { ++other; continue; }
      ++held;
      const uint32_t addr = r.guest + i * 4;
      if (run_words && addr - run_end <= 64 && pt == run_pattern) {
        run_end = addr;
        ++run_words;
      } else {
        flush();
        run_start = run_end = addr;
        run_words = 1;
        run_pattern = pt;
      }
    }
  }
  flush();
  // Large held ranges outside the GPU buffer areas (B7xxxxxx/B8xxxxxx twins,
  // AC0xxxxx constant ring) are candidate 30 Hz blocks: publish them for the
  // slide smoothing and search guest memory for pointers into their objects.
  {
    std::vector<std::pair<uint32_t, uint32_t>> found;
    size_t w2 = 0;
    uint32_t start = 0, end = 0, words = 0;
    auto take = [&] {
      // Skip GPU buffers (B7/B8 twins, AC0 constant ring), thread stacks
      // (70xxxxxx), the 9xxxxxxx mirror of the data segment and the fiber
      // scheduler slots (834A3258..834A3500, integer waits).
      const bool gpu = (start >= 0xAC000000 && start < 0xAD000000) || (start >= 0xB7000000 && start < 0xB9000000);
      const bool stack = start >= 0x70000000 && start < 0x80000000;
      const bool mirror = start >= 0x90000000 && start < 0xA0000000;
      const bool fibers = start >= 0x834A3258 && start < 0x834A3500;
      if (words && start >= 0x40000000 && !gpu && !stack && !mirror && !fibers)
        found.push_back({start, (end - start) / 4 + 1});
      words = 0;
    };
    for (const Region& r : regions)
      for (uint32_t i = 0; i < r.bytes / 4; ++i, ++w2) {
        const uint8_t pt = pattern[w2];
        if (pt != 0x55 && pt != 0xAA) continue;
        const uint32_t addr = r.guest + i * 4;
        if (words && addr - end <= 64) { end = addr; ++words; }
        else { take(); start = end = addr; words = 1; }
      }
    take();
    for (const auto& [guest, count] : found) {
      if (f) std::fprintf(f, "# auto-smooth range %08X words=%u\n", guest, count);
      REXLOG_INFO("DOAX2 native60 slide hold scan: auto-smooth range {:08X} words={}", guest, count);
      if (count < 1000) continue;  // pointer search only for the big blocks
      uint32_t hits = 0;
      for (const Region& r : regions) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(base + r.guest);
        for (uint32_t i = 0; i < r.bytes / 4 && hits < 40; ++i) {
          const uint32_t v = _byteswap_ulong(src[i]);
          if (v <= guest && guest - v <= 0x4000) {
            ++hits;
            if (f) std::fprintf(f, "# pointer %08X -> %08X (range-0x%X)\n", r.guest + i * 4, v, guest - v);
          }
        }
      }
      // Also the game data segment (player records, managers).
      for (uint32_t a = 0x82000000; a < 0x84000000 && hits < 60; a += 4) {
        MEMORY_BASIC_INFORMATION info{};
        if ((a & 0xFFFF) == 0) {
          if (!VirtualQuery(base + a, &info, sizeof(info)) || info.State != MEM_COMMIT) { a += 0xFFFC; continue; }
        }
        const uint32_t v = REX_LOAD_U32(a);
        if (v <= guest && guest - v <= 0x4000) {
          ++hits;
          if (f) std::fprintf(f, "# pointer %08X -> %08X (range-0x%X)\n", a, v, guest - v);
        }
      }
    }
    {
      const uint32_t anchor = REX_LOAD_U32(0x833FB0EC);
      REXLOG_INFO("DOAX2 native60 slide hold scan: anchor [833FB0EC]={:08X} +0x2FE8={:08X}", anchor,
                  anchor + 0x2FE8);
    }
    // Report only; the smoothing list is static now. Big blocks other than
    // the skeleton (splash particle pools) get a 4-update record dump.
    const uint32_t skeleton = REX_LOAD_U32(0x833FB0EC) + 0x2FE8;
    particle_dump.clear();
    for (const auto& range : found)
      if (range.second >= 1000 && (range.first < skeleton || range.first >= skeleton + 18424 * 4))
        particle_dump.push_back(range.first);
    particle_dump_left = particle_dump.empty() ? 0 : 4;
  }
  if (f) {
    std::fprintf(f, "# held=%llu every_update=%llu other=%llu\n", (unsigned long long)held,
                 (unsigned long long)every, (unsigned long long)other);
    std::fclose(f);
  }
  REXLOG_INFO("DOAX2 native60 slide hold scan {} done: held={} every_update={} other={} -> {}", scans,
              held, every, other, path);
  std::vector<uint32_t>().swap(prev);
  std::vector<uint8_t>().swap(pattern);
  step = -1;
#endif
}

// Drop / win pose trace (diagnostics): one line per update while the results
// or end phase runs, with the step flag, camera position, scene camera track
// phase, player0 position, the first skeleton bone and the heading block, to
// see which of them only move on step updates (half rate).
void Native60SlideOutroTrace(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  const uint64_t frame = Doax2EngineFrameCount();
  const bool results = native60_slide_results_frame != ~0ull && frame - native60_slide_results_frame <= 1;
  const bool ending = native60_slide_end_frame != ~0ull && frame - native60_slide_end_frame <= 1;
  if (!results && !ending) return;
  static uint32_t lines = 0;
  static uint64_t last = 0;
  if (frame - last > 600) lines = 0;
  last = frame;
  if (++lines > 400) return;
  constexpr uint32_t scene = 0x839BC0F0, camera = 0x839BC1D0;
  const uint32_t index = REX_LOAD_U32(scene + 968);
  const uint32_t objects = REX_LOAD_U32(scene + 928);
  const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
  const uint32_t skeleton = REX_LOAD_U32(0x833FB0EC) + 0x2FE8;
  auto f = [&](uint32_t a) { return double(Native60LoadFloat(base, a)); };
  REXLOG_INFO("DOAX2 native60 outro trace: frame={} phase={} step={} cam=({:.4f},{:.4f},{:.4f}) tgt=({:.4f},{:.4f},{:.4f}) "
              "track={:.3f}/{:.3f} player=({:.4f},{:.4f},{:.4f},{:.4f}) bone=({:.4f},{:.4f},{:.4f},{:.4f}) "
              "head=({:.4f},{:.4f},{:.4f}) p7c={:.4f}",
              frame, results ? "results" : "end", native60_slide_step_frame == frame ? 1 : 0,
              f(camera + 48), f(camera + 52), f(camera + 56), f(camera + 64), f(camera + 68), f(camera + 72),
              object ? f(object + 832 + 128) : 0.0, object ? f(object + 1024 + 128) : 0.0,
              f(0x833B8520 + 12), f(0x833B8520 + 16), f(0x833B8520 + 20), f(0x833B8520 + 24),
              f(skeleton), f(skeleton + 4), f(skeleton + 8), f(skeleton + 12),
              f(0x833A10E8), f(0x833A10EC), f(0x833A10F0), f(0x833B859C));
}

// Flicker trace (experimental, dev keys): per update during the slide
// gameplay, the values that place the girl as the frame is drawn: player0
// words +0..+60 and the first 32 words of the skeleton region, with the sim
// flag. A one-update spike in one of them is the flickering frame.
void Native60SlideFlickerTrace(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_dev_keys) && !REXCVAR_GET(doax2_native60_counter_scan)) return;
  const uint64_t frame = Doax2EngineFrameCount();
  if (native60_slide_sim_frame == ~0ull || frame - native60_slide_sim_frame > 4) return;
  static uint64_t lines = 0, last = 0;
  if (frame - last > 600) lines = 0;
  last = frame;
  if (++lines > 4000) return;
  std::string text;
  for (uint32_t i = 0; i < 16; ++i) text += fmt::format(" {:.5g}", Native60LoadFloat(base, 0x833B8520 + i * 4));
  text += " |";
  const uint32_t skeleton = REX_LOAD_U32(0x833FB0EC) + 0x2FE8;
  for (uint32_t i = 0; i < 32; ++i) text += fmt::format(" {:.5g}", Native60LoadFloat(base, skeleton + i * 4));
  REXLOG_INFO("DOAX2 native60 flicker trace: frame={} sim={}{}", frame, native60_slide_sim_frame == frame ? 1 : 0, text);
}

// Menu / UI transitions (title options popup, write watch log 472): a
// transition object { +0 state, +8 frame counter (+1 per update, 824C4450),
// +12 in-duration (s16), +14 out-duration (s16), +16 progress =
// blend(counter / duration) (8244EB40) } with durations authored in 30 FPS
// frames, so every pop-in / pop-out / slide ran in half the time at 60 Hz.
// The two starters 824C4128 (open) and 824C41A0 (close) - 100+ callers -
// get doubled durations: same wall time as the original, and the float
// progress advances in half steps (smoother than the original 30 FPS).
inline double Native60TransitionScale() {
  const double k = REXCVAR_GET(doax2_native60_menu_transition_scale);
  return k >= 1.0 && k <= 8.0 ? k : 2.0;
}
inline void Native60DoubleTransitionDurations(PPCContext& ctx) {
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) return;
  const double k = Native60TransitionScale();
  const int32_t in = ctx.r6.s32, out = ctx.r5.s32;
  // stored in-duration = r6 + 1: pass k*(r6 + 1) - 1.
  if (in > 0 && in < 0x1FFF) ctx.r6.s64 = int32_t(std::lround(k * (in + 1))) - 1;
  if (out > 0 && out < 0x1FFF) ctx.r5.s64 = int32_t(std::lround(k * out));
}
REX_EXTERN(__imp__sub_824C4128);
REX_EXTERN(__imp__sub_824C41A0);
inline bool Native60TransitionLogOn() {
  return REXCVAR_GET(doax2_native60_dev_keys) || REXCVAR_GET(doax2_native60_counter_scan);
}
inline void Native60TransitionStartLog(const char* name, const PPCContext& ctx, int32_t in, int32_t out) {
  if (!Native60TransitionLogOn()) return;
  static std::atomic<uint32_t> lines{0};
  if (lines.fetch_add(1) >= 2000) return;
  REXLOG_INFO("DOAX2 native60 transition start {}: frame={} obj={:08X} in={} -> {} out={} -> {} lr={:08X}", name,
              Doax2EngineFrameCount(), ctx.r3.u32, in, ctx.r6.s32, out, ctx.r5.s32, uint32_t(ctx.lr));
}
REX_HOOK_RAW(sub_824C4128) {
  const int32_t in = ctx.r6.s32, out = ctx.r5.s32;
  Native60DoubleTransitionDurations(ctx);
  Native60TransitionStartLog("open", ctx, in, out);
  __imp__sub_824C4128(ctx, base);
}
REX_HOOK_RAW(sub_824C41A0) {
  const int32_t in = ctx.r6.s32, out = ctx.r5.s32;
  Native60DoubleTransitionDurations(ctx);
  Native60TransitionStartLog("close", ctx, in, out);
  __imp__sub_824C41A0(ctx, base);
}
// Popup window transitions started inline (not via 824C4128/824C41A0, log
// 476): 825A0060 (open) and 825A37D8 (close) write the window object at
// 833B93D0 directly - transition at +164 (+172 counter = 1, +176 delay,
// +178 duration = 8 frames). Double them right after the start.
inline double Native60WindowScale() {
  const double k = REXCVAR_GET(doax2_native60_menu_window_scale);
  return k >= 1.0 && k <= 8.0 ? k : 2.0;
}
inline void Native60DoubleInlineTransition(uint8_t* base, uint32_t transition) {
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) return;
  if (REX_LOAD_U32(transition + 8) != 1) return;  // only right after a start
  for (uint32_t off : {12u, 14u}) {
    const int16_t v = int16_t(REX_LOAD_U16(transition + off));
    if (v > 0 && v < 0x1FFF) REX_STORE_U16(transition + off, uint16_t(std::lround(Native60WindowScale() * v)));
  }
}
// Popup animation scan (diagnostics): started by the popup open/close
// (825A0060 / 825A37D8). For the next 40 updates every word of
// 83300000-83CFFFFF is compared with the previous update; words that changed
// on >= 6 updates are reported with their change count, the first and last
// update they changed on and first/last values, so the 30 FPS reference and
// the 60 FPS build can be compared (animation length in updates).
namespace native60_popup_scan {
constexpr uint32_t kStart = 0x83300000, kWords = 0xA00000 / 4;
inline std::vector<uint32_t> prev, first_value;
inline std::vector<uint8_t> changes, first_change, last_change;
inline int step = -1;
inline int scans = 0;
inline const char* label = "";
inline std::chrono::steady_clock::time_point start_time;
inline void Start(const char* what) {
  if (!REXCVAR_GET(doax2_native60_counter_scan) || step >= 0 || scans >= 40) return;
  label = what;
  step = 0;
  start_time = std::chrono::steady_clock::now();
}
// Held-direction repeat: every update with the menu's up/down flags
// (pad +8 & 0x330) is logged with the real time since the previous one.
inline void LogDirection(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  // Combined pad record (824FFF20(4)), the one the menus read.
  const uint32_t flags = REX_LOAD_U32(0x8342050C + 8) & 0xFFFF;
  static uint32_t last_flags = 0, last_held = 0;
  const uint32_t held = REX_LOAD_U32(0x8342050C) & 0xFFFF;
  static uint32_t held_updates = 0;
  held_updates = (held & 0xF) && (held & 0xF) == (last_held & 0xF) ? held_updates + 1 : 0;
  if (held_updates == 60) Start("hold");
  if (held != last_held) {
    static uint32_t held_lines = 0;
    if (++held_lines <= 400)
      REXLOG_INFO("DOAX2 native60 menu held: {:04X} -> {:04X} frame={}", last_held, held, Doax2EngineFrameCount());
    last_held = held;
  }
  if ((flags & 0x30) && !(last_flags & 0x30)) Start((flags & 0x10) ? "confirm" : "cancel");
  last_flags = flags;
  static auto last = std::chrono::steady_clock::now();
  static uint32_t lines = 0;
  if (!flags || ++lines > 600) return;
  const auto now = std::chrono::steady_clock::now();
  REXLOG_INFO("DOAX2 native60 menu direction: flags={:03X} ms_since_last={:.1f} frame={}", flags,
              std::chrono::duration<double, std::milli>(now - last).count(), Doax2EngineFrameCount());
  last = now;
}
inline void Update(uint8_t* base) {
  LogDirection(base);
  if (step < 0) return;
  if (step >= 1 && step <= 60) {
    auto f = [&](uint32_t a) { return std::bit_cast<float>(REX_LOAD_U32(a)); };
    REXLOG_INFO("DOAX2 native60 popup track ({}) update {} ms={:.1f}: t164={:.3f} t184={:.3f} box0=({:.1f},{:.1f},{:.1f},{:.1f}) "
                "state164={} state184={}",
                label, step,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count(),
                f(0x833B9484), f(0x833B9498), f(0x833BE314), f(0x833BE318), f(0x833BE31C), f(0x833BE320),
                REX_LOAD_U32(0x833B9474), REX_LOAD_U32(0x833B9488));
  }
  const uint32_t* now = reinterpret_cast<const uint32_t*>(base + kStart);
  if (step == 0) {
    prev.assign(now, now + kWords);
    first_value = prev;
    changes.assign(kWords, 0);
    first_change.assign(kWords, 0);
    last_change.assign(kWords, 0);
    step = 1;
    return;
  }
  for (uint32_t i = 0; i < kWords; ++i)
    if (now[i] != prev[i]) {
      if (!changes[i]) first_change[i] = uint8_t(step);
      last_change[i] = uint8_t(step);
      ++changes[i];
      prev[i] = now[i];
    }
  if (++step <= 60) return;
  ++scans;
  uint32_t lines = 0;
  for (uint32_t i = 0; i < kWords && lines < 400; ++i) {
    if (changes[i] < 3 || (last_change[i] >= 58 && std::string_view(label) != "hold")) continue;
    const float a = std::bit_cast<float>(std::byteswap(first_value[i]));
    const float b = std::bit_cast<float>(std::byteswap(prev[i]));
    REXLOG_INFO("DOAX2 native60 popup scan {} ({}): addr={:08X} changes={} updates {}..{} value {:08X} -> {:08X} "
                "(float {:.4g} -> {:.4g})",
                scans, label, kStart + i * 4, changes[i], first_change[i], last_change[i],
                std::byteswap(first_value[i]), std::byteswap(prev[i]), a, b);
    ++lines;
  }
  REXLOG_INFO("DOAX2 native60 popup scan {} ({}) done: {} words reported, {:.1f} ms for 60 updates", scans, label,
              lines, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count());
  std::vector<uint32_t>().swap(prev);
  std::vector<uint32_t>().swap(first_value);
  step = -1;
}
}  // namespace native60_popup_scan
// Button auto-repeat (diagnostics): 8274B3E0(device r3, record r4) sets the
// repeat bits (record +12) from real time: initial delay float device+576,
// interval device+580. Log the settings and every repeat with real time.
REX_EXTERN(__imp__sub_8274B3E0);
REX_HOOK_RAW(sub_8274B3E0) {
  const uint32_t device = ctx.r3.u32, record = ctx.r4.u32;
  __imp__sub_8274B3E0(ctx, base);
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  static uint32_t lines = 0;
  static auto last = std::chrono::steady_clock::now();
  static uint32_t last_delay = 0, last_interval = 0;
  const uint32_t delay = REX_LOAD_U32(device + 576), interval = REX_LOAD_U32(device + 580);
  if ((delay != last_delay || interval != last_interval) && lines < 800) {
    ++lines;
    REXLOG_INFO("DOAX2 native60 repeat settings: device={:08X} delay={:.4f}s interval={:.4f}s", device,
                std::bit_cast<float>(delay), std::bit_cast<float>(interval));
    last_delay = delay;
    last_interval = interval;
  }
  const uint32_t repeat = REX_LOAD_U32(record + 12), held = REX_LOAD_U32(record + 0);
  if (repeat && lines < 800) {
    ++lines;
    const auto now = std::chrono::steady_clock::now();
    REXLOG_INFO("DOAX2 native60 repeat: held={:08X} repeat={:08X} ms_since_last={:.1f} frame={}", held, repeat,
                std::chrono::duration<double, std::milli>(now - last).count(), Doax2EngineFrameCount());
    last = now;
  }
}
// Menu text box 833BBB78 (824D8B98): characters are typed on when the frame
// counter +2060 is a multiple of the s16 interval +2056, so the text typed on
// twice as fast at 60. Double the interval for the call.
REX_EXTERN(__imp__sub_824D8B98);
REX_HOOK_RAW(sub_824D8B98) {
  constexpr uint32_t kInterval = 0x833BBB78 + 2056;
  const uint16_t interval = REX_LOAD_U16(kInterval);
  const bool stretch = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f && int16_t(interval) > 0 &&
                       int16_t(interval) < 0x4000;
  if (stretch) REX_STORE_U16(kInterval, uint16_t(interval * 2));
  __imp__sub_824D8B98(ctx, base);
  if (stretch && REX_LOAD_U16(kInterval) == uint16_t(interval * 2)) REX_STORE_U16(kInterval, interval);
}
// Menu held-direction auto-repeat: 8258ED90 / 8258EE40 / 8258EEF0 /
// 8258EFA0 (up / down / left / right, 58 call sites) return true on a press
// or when (frames held - delay r5) is a positive multiple of interval r4;
// each menu passes its own frame counts (title list: 5), so held scrolling
// ran twice as fast at 60. Scale both for the call.
inline void Native60MenuRepeatScale(PPCContext& ctx) {
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) return;
  double k = REXCVAR_GET(doax2_native60_menu_repeat_scale);
  if (!(k >= 1.0 && k <= 8.0)) k = 2.0;
  const int32_t interval = ctx.r4.s32, delay = ctx.r5.s32;
  if (Native60TransitionLogOn()) {
    static std::mutex mutex;
    static std::set<std::pair<int32_t, int32_t>> seen;
    std::lock_guard lock(mutex);
    if (seen.insert({interval, delay}).second && seen.size() < 64)
      REXLOG_INFO("DOAX2 native60 menu repeat: interval={} delay={} lr={:08X}", interval, delay, uint32_t(ctx.lr));
  }
  if (interval > 0 && interval < 0x4000) ctx.r4.s64 = std::max<int32_t>(1, int32_t(std::lround(k * interval)));
  if (delay > 0 && delay < 0x4000) ctx.r5.s64 = int32_t(std::lround(k * delay));
}
#define DOAX2_NATIVE60_MENU_REPEAT(name) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    Native60MenuRepeatScale(ctx); \
    __imp__##name(ctx, base); \
  }
DOAX2_NATIVE60_MENU_REPEAT(sub_8258ED90)
DOAX2_NATIVE60_MENU_REPEAT(sub_8258EE40)
DOAX2_NATIVE60_MENU_REPEAT(sub_8258EEF0)
DOAX2_NATIVE60_MENU_REPEAT(sub_8258EFA0)
// Menu item bounce: 824C6F58(window, transition, item, dt f1) steps a spring
// per item in the tables 83C0E828 (acc, vel, pos, target):
// acc = k1*(target - pos) - k2*vel; vel += acc*dt; pos += vel*dt, with a
// fixed dt (82064494) once per update (824C6038), so the options/images
// bounced twice as fast at 60. Halve the step (f1 = 0 calls only evaluate).
REX_EXTERN(__imp__sub_824C6F58);
REX_HOOK_RAW(sub_824C6F58) {
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f) ctx.f1.f64 *= 0.5;
  __imp__sub_824C6F58(ctx, base);
}
REX_EXTERN(__imp__sub_825A0060);
REX_EXTERN(__imp__sub_825A37D8);
REX_HOOK_RAW(sub_825A0060) {
  native60_popup_scan::Start("open");
  __imp__sub_825A0060(ctx, base);
}
REX_HOOK_RAW(sub_825A37D8) {
  native60_popup_scan::Start("close");
  __imp__sub_825A37D8(ctx, base);
}

// Transition step 824C4450(obj, desc): log state changes with the counter and
// how many times the object stepped in this update.
REX_EXTERN(__imp__sub_824C4450);
// Menu window records 833B93D0 + n*232 carry their open/close transition at
// +164; about 20 inline starters (825A0060, 825A37D8, ...) set its counter to
// 1 with fixed durations (8 frames), and +196 (the eased value driving the
// window's elements: sub-option fades, popup boxes) follows it. Stretch every
// window transition on its first step after a start (counter still 1), so
// all menus pop in/out at the 30 FPS timing (logs 484-491).
inline bool Native60IsWindowTransition(uint32_t obj) {
  constexpr uint32_t kFirst = 0x833B93D0 + 164, kStride = 232;
  return obj >= kFirst && obj < kFirst + 16 * kStride && (obj - kFirst) % kStride == 0;
}
REX_HOOK_RAW(sub_824C4450) {
  const uint32_t obj = ctx.r3.u32;
  if (Native60IsWindowTransition(obj) && REX_LOAD_U32(obj + 8) == 1) {
    const uint32_t state = REX_LOAD_U32(obj + 0);
    if (state == 0 || state == 1 || state == 3 || state == 4) Native60DoubleInlineTransition(base, obj);
  }
  const uint32_t state_before = REX_LOAD_U32(obj + 0);
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_824C4450(ctx, base);
  if (!Native60TransitionLogOn()) return;
  const uint32_t state_after = REX_LOAD_U32(obj + 0);
  static std::mutex mutex;
  static std::map<uint32_t, std::pair<uint64_t, uint32_t>> steps;  // obj -> (frame, count)
  const uint64_t frame = Doax2EngineFrameCount();
  uint32_t per_update;
  {
    std::lock_guard lock(mutex);
    auto& e = steps[obj];
    if (e.first != frame) e = {frame, 0};
    per_update = ++e.second;
  }
  static std::atomic<uint32_t> lines{0};
  if ((state_after != state_before || per_update == 2) && lines.fetch_add(1) < 3000)
    REXLOG_INFO("DOAX2 native60 transition step: frame={} obj={:08X} state {} -> {} counter={} in={} out={} progress={:.3f} calls_this_update={} lr={:08X}",
                frame, obj, state_before, state_after, int32_t(REX_LOAD_U32(obj + 8)),
                int16_t(REX_LOAD_U16(obj + 12)), int16_t(REX_LOAD_U16(obj + 14)),
                Native60LoadFloat(base, obj + 16), per_update, lr);
}

// Winnings multiplier (launcher Settings, doax2_money_multiplier). Each girl
// has her own money: u32 array 83986D9C + girl * 4 (9 girls, capped at
// 99,999,999 by the game). Bets, losses and purchases are never touched;
// only increases made by the payout code below are multiplied:
// - casino payouts (money probe logs 2026-10-22): blackjack 82435360,
//   slots 82455330, poker / roulette 82447830, 82448760, 82448C60,
//   82459010, 82459698, 8245D4E0, 8245DE40;
// - minigame prizes: the Pool Slide results 826A7748 (direct write) and the
//   game's add-money routine 826CAAA8 (positive amounts only; minigame
//   prizes, purchases pass a negative amount).
// The wrappers compare every girl's money before and after the call; nested
// wrapped calls are applied once, by the outermost.
namespace doax2_money {
constexpr uint32_t kMoney = 0x83986D9C;
constexpr int kGirls = 9;
constexpr uint32_t kMax = 99999999;
inline thread_local int depth = 0;
inline uint32_t Multiplier() {
  const int32_t k = REXCVAR_GET(doax2_money_multiplier);
  return k == 10 || k == 15 || k == 20 || k == 100 ? uint32_t(k) : 1u;
}
template <typename F>
inline void Payout(const char* name, uint8_t* base, F&& call) {
  const uint32_t k = Multiplier();
  if (k == 1) {
    call();
    return;
  }
  std::array<uint32_t, kGirls> before{};
  for (int g = 0; g < kGirls; ++g) before[g] = REX_LOAD_U32(kMoney + g * 4);
  ++depth;
  call();
  --depth;
  if (depth) return;
  for (int g = 0; g < kGirls; ++g) {
    const uint32_t after = REX_LOAD_U32(kMoney + g * 4);
    if (after <= before[g] || after > kMax) continue;
    const uint64_t gain = after - before[g];
    const uint32_t boosted = uint32_t(std::min<uint64_t>(uint64_t(before[g]) + gain * k, kMax));
    REX_STORE_U32(kMoney + g * 4, boosted);
    REXLOG_INFO("DOAX2 winnings x{} ({}): girl {} +{} -> +{}", k, name, g, gain, boosted - before[g]);
  }
}
}  // namespace doax2_money
#define DOAX2_MONEY_PAYOUT(name)                                                  \
  REX_EXTERN(__imp__##name);                                                     \
  REX_HOOK_RAW(name) {                                                           \
    doax2_money::Payout(#name, base, [&] { __imp__##name(ctx, base); });         \
  }
DOAX2_MONEY_PAYOUT(sub_82435360)
DOAX2_MONEY_PAYOUT(sub_82455330)
DOAX2_MONEY_PAYOUT(sub_82447830)
DOAX2_MONEY_PAYOUT(sub_82448760)
DOAX2_MONEY_PAYOUT(sub_82448C60)
DOAX2_MONEY_PAYOUT(sub_82459010)
DOAX2_MONEY_PAYOUT(sub_82459698)
DOAX2_MONEY_PAYOUT(sub_8245D4E0)
DOAX2_MONEY_PAYOUT(sub_8245DE40)
DOAX2_MONEY_PAYOUT(sub_826A7748)
// Add money (r3 = amount): positive amounts outside the wrapped payout code
// are minigame prizes.
REX_EXTERN(__imp__sub_826CAAA8);
REX_HOOK_RAW(sub_826CAAA8) {
  const uint32_t k = doax2_money::Multiplier();
  const int32_t amount = ctx.r3.s32;
  if (k > 1 && amount > 0 && doax2_money::depth == 0) {
    const int64_t boosted = std::min<int64_t>(int64_t(amount) * k, doax2_money::kMax);
    ctx.r3.s64 = boosted;
    REXLOG_INFO("DOAX2 winnings x{} (add money): +{} -> +{}", k, amount, boosted);
  }
  __imp__sub_826CAAA8(ctx, base);
}

void ReportNative60GameStep(uint8_t* base) {
  native60_popup_scan::Update(base);
  Native60SlideSmoothing();
  Native60SlideOutroTrace(base);
  Native60SlideFlickerTrace(base);
  if (native60_slide_main_frame != ~0ull && Doax2EngineFrameCount() - native60_slide_main_frame >= 2)
    Native60SlideFlushMidpoints(base);
  ApplyNative60JetIntroPacing(base);
  Native60JetIntroAutoCapture(base);
  Native60WriteWatchUpdate(base);
  if (REXCVAR_GET(doax2_native60_counter_scan)) {
    Native60JetCameraDump(base);
    Native60JetIntroDump(base);
    Native60SlideDumpTick(base);
    Native60SlideHoldScan(base);
    Native60JetIntroRateScan(base);
    Native60JetTransitionScan(base);
    Native60JetHeapScan(base);
    Native60CountModeUpdate(base);
    static uint64_t writer_updates = 0;
    if (++writer_updates % 600 == 0) ReportNative60PosWriters();
  }
  ApplyNative60PhysicsStep(base);
  if (REXCVAR_GET(doax2_native60_counter_scan)) ReportNative60StaticRegions(base);
  DumpNative60Census(base);
  if (REXCVAR_GET(doax2_native60_counter_scan)) ReportNative60HitWindows(base);
  {
    constexpr uint32_t scene = 0x839BC0F0;
    const uint32_t index = REX_LOAD_U32(scene + 968);
    const uint32_t objects = REX_LOAD_U32(scene + 928);
    const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
    ScanNative60Counters(base, object ? REX_LOAD_U32(object + 3580) : 0, index);
  }
  ReportNative60CameraState(base);
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) == 1.0f) return;
  static uint64_t frames = 0;
  if (++frames != 1 && frames % 600 != 0) return;
  REXLOG_INFO("DOAX2 native60 game step: frames={} base={} modifier={} "
              "step={} tick_unit={}",
              frames, Native60LoadFloat(base, kNative60BaseSpeed),
              Native60LoadFloat(base, kNative60SpeedModifier),
              Native60LoadFloat(base, kNative60GameStep),
              Native60LoadFloat(base, kNative60TickUnit));
}

// Immediate children of 8258E500. Each wrapper only compares the selected
// recurring manager's phase before and after the original guest call. This
// localizes the +1 write without altering update order or guest memory.
#define DOAX2_NATIVE60_CHILD_PROBE(name) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    Native60WriterProbeScope native60_probe_scope(base, #name); \
    const auto native60_phase_before = SnapshotNative60Phase(base); \
    __imp__##name(ctx, base); \
    ReportNative60PhaseBoundary(base, #name, native60_phase_before); \
  }
DOAX2_NATIVE60_CHILD_PROBE(sub_82782F78)
DOAX2_NATIVE60_CHILD_PROBE(sub_82782C60)
// Fiber scheduler 8258CC00: 24 slots at 834A3258 (28 bytes; +0 flags, +4
// wait count decremented once per call while positive). Scripted fibers
// sleep "N frames" authored at 30 Hz (lead-in camera cuts ran 44 updates
// where the original takes 49 frames = 1.64 s). With the base speed scaled,
// hold decrements of waits > 1 on alternate calls; waits of 1 ("resume next
// frame") are untouched so per-frame fibers keep running at 60 Hz.
struct Native60FiberWaits {
  std::array<uint32_t, 24> before{};
  bool active = false;
};
Native60FiberWaits Native60FiberWaitsBegin(uint8_t* base) {
  Native60FiberWaits waits;
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) return waits;
  waits.active = true;
  for (uint32_t i = 0; i < 24; ++i)
    waits.before[i] = REX_LOAD_U32(0x834A3258 + i * 28 + 4);
  return waits;
}
void Native60FiberWaitsEnd(uint8_t* base, const Native60FiberWaits& waits) {
  if (!waits.active) return;
  static bool hold = false;
  hold = !hold;
  if (!hold) return;
  uint32_t held = 0;
  for (uint32_t i = 0; i < 24; ++i) {
    const int32_t before = int32_t(waits.before[i]);
    const uint32_t address = 0x834A3258 + i * 28 + 4;
    if (before > 1 && int32_t(REX_LOAD_U32(address)) == before - 1) {
      REX_STORE_U32(address, uint32_t(before));
      ++held;
    }
  }
  static uint64_t total = 0, lines = 0;
  total += held;
  if (held && (++lines <= 8 || lines % 600 == 0))
    REXLOG_INFO("DOAX2 native60 fiber waits held: held={} total={}", held,
                total);
}

// Fiber slot census (diagnostics): which scheduler slots are active, their
// fiber handles, and how often each resumes, to separate scripted-sequence
// fibers (intros/lead-ins that count frames themselves with wait 1) from
// per-frame system fibers such as the camera.
void ReportNative60FiberSlots(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  constexpr uint32_t kTable = 0x834A3258;
  static std::array<uint32_t, 24> last_fiber{}, runs{};
  static std::array<uint32_t, 24> last_wait{};
  static uint32_t calls = 0;
  ++calls;
  for (uint32_t i = 0; i < 24; ++i) {
    const uint32_t flags = REX_LOAD_U32(kTable + i * 28);
    const uint32_t wait = REX_LOAD_U32(kTable + i * 28 + 4);
    const uint32_t fiber = (flags & 1) ? REX_LOAD_U32(kTable + i * 28 + 8) : 0;
    if (fiber != last_fiber[i]) {
      REXLOG_INFO("DOAX2 native60 fiber slot {} {}: fiber={:08X} flags={:08X} "
                  "engine_frame={}", i, fiber ? "started" : "stopped",
                  fiber ? fiber : last_fiber[i], flags, Doax2EngineFrameCount());
      last_fiber[i] = fiber;
      runs[i] = 0;
    }
    // A slot that shows wait 0 after the scheduler ran this update resumed.
    if (fiber && wait == 0) ++runs[i];
    last_wait[i] = wait;
  }
  if (calls % 300 != 0) return;
  std::string line;
  for (uint32_t i = 0; i < 24; ++i)
    if (last_fiber[i])
      line += fmt::format(" [{}:{:08X} runs={} wait={}]", i, last_fiber[i], runs[i],
                          last_wait[i]);
  REXLOG_INFO("DOAX2 native60 fiber slots (300 updates):{}", line);
  runs = {};
}

REX_EXTERN(__imp__sub_8258CC00);
REX_HOOK_RAW(sub_8258CC00) {
  // Test G (intro bisect 7): during the Niki Marina intro (racer 0 race
  // state 0) the intro-time script slots 1, 9 and 12 (frame-counting waits
  // 826CC5CC / 82511C24) resume only on alternate updates.
  if (REXCVAR_GET(doax2_native60_intro_bisect) == 7 &&
      float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f && Native60JetRaceActive() &&
      REX_LOAD_U32(0x834D8CA0 + 16) == 0) {
    static bool hold_g = false;
    hold_g = !hold_g;
    if (hold_g) {
      for (uint32_t i : {1u, 9u, 12u}) {
        const uint32_t entry = kNative60FiberTable + i * 28;
        const uint32_t flags = REX_LOAD_U32(entry);
        if (!(flags & 1) || (flags & 2)) continue;
        if (int32_t(REX_LOAD_U32(entry + 4)) <= 1) REX_STORE_U32(entry + 4, 2);
      }
    }
  }
  if (Native60ScriptFibersThrottled()) {
    // On alternate updates, keep script fibers that would resume now asleep
    // for one more update (wait 1 -> 2), so they run at the original 30 Hz.
    static bool hold = false;
    hold = !hold;
    if (hold) {
      uint32_t delayed = 0;
      for (uint32_t i = 0; i < 24; ++i) {
        if (Native60IsSystemFiberSlot(i)) continue;
        const uint32_t entry = kNative60FiberTable + i * 28;
        const uint32_t flags = REX_LOAD_U32(entry);
        if (!(flags & 1) || (flags & 2)) continue;
        const int32_t wait = int32_t(REX_LOAD_U32(entry + 4));
        if (wait > 1) continue;
        REX_STORE_U32(entry + 4, 2);
        ++delayed;
      }
      static uint64_t lines = 0;
      if (delayed && (++lines <= 4 || lines % 1800 == 0))
        REXLOG_INFO("DOAX2 native60 script fibers paced to 30 Hz: delayed={}",
                    delayed);
    }
  }
  const auto native60_fiber_waits = Native60FiberWaitsBegin(base);
  const auto phase_before = SnapshotNative60Phase(base);
  const auto scheduler_before = SnapshotNative60Scheduler(base);
  const auto camera_before = SnapshotNative60Camera(base);
  const uint32_t native60_index_before_sub_8258CC00 = Native60SceneIndex(base);
  __imp__sub_8258CC00(ctx, base);
  Native60SceneIndexCheck(base, "sub_8258CC00", native60_index_before_sub_8258CC00);
  Native60FiberWaitsEnd(base, native60_fiber_waits);
  ReportNative60FiberSlots(base);
  ReportNative60Scheduler(base, scheduler_before);
  ReportNative60Camera(base, "sub_8258CC00", camera_before, 0);
  ReportNative60PhaseBoundary(base, "sub_8258CC00", phase_before);
}
REX_EXTERN(__imp__sub_82783210);
REX_HOOK_RAW(sub_82783210) {
  // Yield-site census (diagnostics): a yield is a switch from inside a
  // fiber slot back to the scheduler fiber (834A3254). The return address
  // identifies the wait loop; intro/READY-GO loops can then be paced
  // individually without slowing the gameplay fibers.
  if (REXCVAR_GET(doax2_native60_counter_scan)) {
    const int32_t slot = int32_t(REX_LOAD_U32(kNative60FiberCurrent));
    if (slot >= 0 && slot < 24 && ctx.r3.u32 == REX_LOAD_U32(0x834A3254)) {
      const uint32_t site = uint32_t(ctx.lr);
      const uint32_t wait = REX_LOAD_U32(kNative60FiberTable + slot * 28 + 4);
      static std::mutex mutex;
      static std::map<std::pair<int32_t, uint32_t>, uint64_t> counts;
      static std::set<std::pair<int32_t, uint32_t>> seen;
      std::lock_guard lock(mutex);
      const auto key = std::make_pair(slot, site);
      ++counts[key];
      if (seen.insert(key).second) {
        constexpr uint32_t scene = 0x839BC0F0;
        const uint32_t index = REX_LOAD_U32(scene + 968);
        const uint32_t objects = REX_LOAD_U32(scene + 928);
        const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
        REXLOG_INFO("DOAX2 native60 yield site new: slot={} site={:08X} wait={} "
                    "camera_mode={} camera_index={} engine_frame={}",
                    slot, site, int32_t(wait),
                    object ? REX_LOAD_U32(object + 3580) : 0, index,
                    Doax2EngineFrameCount());
      }
      static uint64_t last_report = 0;
      const uint64_t frame = Doax2EngineFrameCount();
      if (frame >= last_report + 300) {
        last_report = frame;
        std::string line;
        for (const auto& [k, v] : counts)
          line += fmt::format(" [{}:{:08X}={}]", k.first, k.second, v);
        REXLOG_INFO("DOAX2 native60 yield sites (300 updates):{}", line);
        counts.clear();
      }
    }
  }
  const bool probe = REXCVAR_GET(doax2_time_source_probe);
  const auto before = SnapshotNative60Camera(base);
  uint32_t slot_index = 24, callback = 0, wait = 0;
  if (probe) {
    for (uint32_t i = 0; i < 24; ++i) {
      constexpr uint32_t table = 0x834A3258;
      const uint32_t slot = table + i * 28;
      if (REX_LOAD_U32(slot + 8) != ctx.r3.u32) continue;
      slot_index = i;
      callback = REX_LOAD_U32(slot + 12);
      wait = REX_LOAD_U32(slot + 4);
      break;
    }
  }
  __imp__sub_82783210(ctx, base);
  if (!probe || slot_index == 24) return;
  const auto after = SnapshotNative60Camera(base);
  if (before.first == after.first && before.second == after.second) return;
  static thread_local uint64_t camera_writes = 0;
  ++camera_writes;
  if (camera_writes <= 8 || camera_writes % 120 == 0)
    REXLOG_INFO("DOAX2 native60 fiber camera write: changes={} slot={} "
                "callback={:08X} wait_at_resume={} first_changed={} "
                "second_changed={} first_pos_before={} first_pos_after={}; "
                "guest callback identity only",
                camera_writes, slot_index, callback, static_cast<int32_t>(wait),
                before.first != after.first, before.second != after.second,
                std::bit_cast<float>(before.first_position_bits),
                std::bit_cast<float>(after.first_position_bits));
}
#define DOAX2_NATIVE60_CAMERA_CHILD(name, site) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    Native60WriterProbeScope native60_probe_scope(base, #name); \
    const auto camera_before = SnapshotNative60Camera(base); \
    const uint32_t native60_index_before = Native60SceneIndex(base); \
    __imp__##name(ctx, base); \
    Native60SceneIndexCheck(base, #name, native60_index_before); \
    ReportNative60Camera(base, #name, camera_before, site); \
  }
DOAX2_NATIVE60_CAMERA_CHILD(sub_82418280, 2)
DOAX2_NATIVE60_CAMERA_CHILD(sub_82418AA8, 3)
REX_EXTERN(__imp__sub_82417ED8);
REX_HOOK_RAW(sub_82417ED8) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_82417ED8");
  const auto camera_before = SnapshotNative60Camera(base);
  const uint32_t record = ctx.r3.u32;
  const uint32_t table = REX_LOAD_U32(0x839BC0F0 + 932);
  const uint32_t offset = record - table;
  const bool correct = REXCVAR_GET(doax2_native60_scene_counters_half) &&
                       table && record >= table && offset < 4 * 60 &&
                       offset % 60 == 0;
  static thread_local doax2::Native60SceneCounterCadence cadence;
  const auto advance = correct ? cadence.Advance(record) : std::nullopt;
  std::array<uint32_t, 14> counters_before{};
  if (advance == false)
    for (uint32_t i = 0; i < counters_before.size(); ++i)
      counters_before[i] = REX_LOAD_U32(record + i * 4);
  __imp__sub_82417ED8(ctx, base);
  uint32_t held = 0;
  if (advance == false) {
    for (uint32_t i = 0; i < counters_before.size(); ++i) {
      const uint32_t address = record + i * 4;
      if (REX_LOAD_U32(address) == counters_before[i] + 1) {
        REX_STORE_U32(address, counters_before[i]);
        ++held;
      }
    }
  }
  if (advance.has_value()) {
    static thread_local uint64_t calls = 0, held_calls = 0,
                                 held_fields = 0;
    ++calls;
    held_calls += !*advance;
    held_fields += held;
    if (calls <= 8 || calls % 480 == 0)
      REXLOG_INFO("DOAX2 native60 scene counter correction: calls={} "
                  "held_calls={} held_fields={} record={:08X} "
                  "advance_this_call={}; opt-in original function executed",
                  calls, held_calls, held_fields, record, *advance);
  }
  ReportNative60Camera(base, "sub_82417ED8", camera_before, 4);
}
DOAX2_NATIVE60_CAMERA_CHILD(sub_824183F0, 5)
REX_EXTERN(__imp__sub_8241A630);
REX_HOOK_RAW(sub_8241A630) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_8241A630");
  const auto camera_before = SnapshotNative60Camera(base);
  const bool probe = REXCVAR_GET(doax2_time_source_probe);
  const uint32_t object = ctx.r3.u32;
  const uint32_t timeline_before = probe ? REX_LOAD_U32(object + 3540) : 0;
  const uint32_t active = probe ? REX_LOAD_U32(object + 3620) : 0;
  const uint32_t mode = probe ? REX_LOAD_U32(object + 3580) : 0;
  __imp__sub_8241A630(ctx, base);
  ReportNative60Camera(base, "sub_8241A630", camera_before, 6);
  if (!probe || !active) return;
  const uint32_t timeline_after = REX_LOAD_U32(object + 3540);
  static thread_local uint64_t calls = 0, changes = 0;
  ++calls;
  changes += timeline_before != timeline_after;
  if (calls <= 8 || calls % 512 == 0)
    REXLOG_INFO("DOAX2 native60 scene object timeline: calls={} changed={} "
                "object={:08X} mode={} before={} after={} camera_output_changed={}; "
                "read-only",
                calls, changes, object, mode,
                std::bit_cast<float>(timeline_before),
                std::bit_cast<float>(timeline_after),
                camera_before.first != SnapshotNative60Camera(base).first);
}
DOAX2_NATIVE60_CAMERA_CHILD(sub_824187E0, 7)
DOAX2_NATIVE60_CAMERA_CHILD(sub_824186C8, 8)
#undef DOAX2_NATIVE60_CAMERA_CHILD
#define DOAX2_NATIVE60_OBJECT_CHILD(name, site) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    Native60WriterProbeScope native60_probe_scope(base, #name); \
    const uint32_t selected = Native60SelectedCameraObject(base); \
    const uint32_t object = selected == ctx.r3.u32 ? selected : 0; \
    const auto before = SnapshotNative60ObjectCamera(base, object); \
    __imp__##name(ctx, base); \
    ReportNative60ObjectCamera(base, #name, object, before, site); \
  }
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241CD58, 0)
REX_EXTERN(__imp__sub_8241D038);
REX_HOOK_RAW(sub_8241D038) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_8241D038");
  const uint32_t object = ctx.r3.u32;
  const bool correct = REXCVAR_GET(doax2_native60_scene_timeline_half) &&
                       IsNative60SceneObject(base, object);
  const uint32_t before_bits = correct ? REX_LOAD_U32(object + 3540) : 0;
  const uint32_t selected = Native60SelectedCameraObject(base);
  const uint32_t observed = selected == object ? selected : 0;
  const auto before = SnapshotNative60ObjectCamera(base, observed);
  __imp__sub_8241D038(ctx, base);
  if (correct) {
    const float native_after =
        std::bit_cast<float>(REX_LOAD_U32(object + 3540));
    const auto corrected = doax2::HalfExpectedNativePhaseStep(
        std::bit_cast<float>(before_bits), native_after, 1.0f);
    if (corrected) {
      REX_STORE_U32(object + 3540, std::bit_cast<uint32_t>(*corrected));
      static std::atomic<uint64_t> corrections{0};
      const auto count = corrections.fetch_add(1, std::memory_order_relaxed) + 1;
      if (count <= 8 || count % 512 == 0)
        REXLOG_INFO("DOAX2 native60 scene timeline correction: calls={} "
                    "object={:08X} before={} native_after={} corrected_after={}; "
                    "original update and later pose evaluation still run",
                    count, object, std::bit_cast<float>(before_bits),
                    native_after, *corrected);
    }
  }
  ReportNative60ObjectCamera(base, "sub_8241D038", observed, before, 1);
}
DOAX2_NATIVE60_OBJECT_CHILD(sub_82431050, 2)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241B1E0, 3)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241B640, 4)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241EB78, 5)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241B748, 6)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241D278, 7)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241E330, 8)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241BFE0, 9)
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241BC30, 10)
REX_EXTERN(__imp__sub_8241C360);
REX_HOOK_RAW(sub_8241C360) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_8241C360");
  const uint32_t selected = Native60SelectedCameraObject(base);
  const uint32_t object = selected == ctx.r3.u32 ? selected : 0;
  const auto before = SnapshotNative60ObjectCamera(base, object);
  std::array<uint32_t, 16> words_before{};
  if (object)
    for (uint32_t i = 0; i < words_before.size(); ++i)
      words_before[i] = REX_LOAD_U32(object + 192 + i * 4);
  __imp__sub_8241C360(ctx, base);
  ReportNative60ObjectCamera(base, "sub_8241C360", object, before, 11);
  if (!object) return;
  uint32_t changed_mask = 0, first_changed = 16, first_after = 0;
  for (uint32_t i = 0; i < words_before.size(); ++i) {
    const uint32_t after = REX_LOAD_U32(object + 192 + i * 4);
    if (after == words_before[i]) continue;
    changed_mask |= 1u << i;
    if (first_changed == 16) {
      first_changed = i;
      first_after = after;
    }
  }
  static thread_local uint64_t calls = 0, changed_calls = 0;
  ++calls;
  changed_calls += changed_mask != 0;
  if (calls <= 8 || calls % 60 == 0)
    REXLOG_INFO("DOAX2 native60 camera recurrence: calls={} changed_calls={} "
                "object={:08X} changed_mask={:04X} first_word={} "
                "first_before={} first_after={}; "
                "read-only",
                calls, changed_calls, object, changed_mask, first_changed,
                first_changed == 16 ? 0.0f :
                    std::bit_cast<float>(words_before[first_changed]),
                std::bit_cast<float>(first_after));
}
REX_EXTERN(__imp__sub_8241AF08);
REX_HOOK_RAW(sub_8241AF08) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_8241AF08");
  const uint32_t selected = Native60SelectedCameraObject(base);
  const uint32_t object = selected == ctx.r3.u32 ? selected : 0;
  const auto before = SnapshotNative60ObjectCamera(base, object);
  static thread_local uint32_t last_object = 0;
  static thread_local std::array<uint32_t, 16> last_evaluated{};
  static thread_local std::array<uint32_t, 16> last_auxiliary{};
  static thread_local Native60ObjectCameraSnapshot last_before{};
  static thread_local uint64_t selected_calls = 0;
  static thread_local uint64_t source_changed_calls = 0;
  static thread_local uint64_t evaluated_changed_calls = 0;
  static thread_local uint64_t auxiliary_changed_calls = 0;
  static thread_local uint64_t motion_changed_calls = 0;
  static thread_local uint32_t evaluated_changed_words = 0;
  static thread_local uint32_t auxiliary_changed_words = 0;
  uint32_t evaluated_cross_mask = 0, auxiliary_cross_mask = 0;
  std::array<uint32_t, 16> current_evaluated{}, current_auxiliary{};
  if (object) {
    for (uint32_t i = 0; i < 16; ++i) {
      current_evaluated[i] = REX_LOAD_U32(object + 192 + i * 4);
      current_auxiliary[i] = REX_LOAD_U32(object + 704 + i * 4);
      if (object == last_object &&
          current_evaluated[i] != last_evaluated[i])
        evaluated_cross_mask |= 1u << i;
      if (object == last_object &&
          current_auxiliary[i] != last_auxiliary[i])
        auxiliary_cross_mask |= 1u << i;
    }
  }
  __imp__sub_8241AF08(ctx, base);
  ReportNative60ObjectCamera(base, "sub_8241AF08", object, before, 12);
  if (!object) return;
  ++selected_calls;
  if (object == last_object) {
    source_changed_calls += before.source != last_before.source;
    evaluated_changed_calls += before.evaluated != last_before.evaluated;
    auxiliary_changed_calls += before.auxiliary != last_before.auxiliary;
    motion_changed_calls += before.motion_inputs != last_before.motion_inputs;
    evaluated_changed_words |= evaluated_cross_mask;
    auxiliary_changed_words |= auxiliary_cross_mask;
  }
  if (selected_calls <= 8 || selected_calls % 120 == 0)
    REXLOG_INFO("DOAX2 native60 camera inputs: calls={} object={:08X} "
                "evaluated_cross_mask={:04X} auxiliary_cross_mask={:04X} "
                "cumulative_source_changes={} evaluated_changes={} "
                "auxiliary_changes={} motion_changes={} "
                "evaluated_words={:04X} auxiliary_words={:04X} "
                "source_hash={:016X} evaluated_hash={:016X} "
                "auxiliary_hash={:016X}; read-only",
                selected_calls, object, evaluated_cross_mask,
                auxiliary_cross_mask, source_changed_calls,
                evaluated_changed_calls, auxiliary_changed_calls,
                motion_changed_calls, evaluated_changed_words,
                auxiliary_changed_words, before.source, before.evaluated,
                before.auxiliary);
  last_object = object;
  last_before = before;
  last_evaluated = current_evaluated;
  last_auxiliary = current_auxiliary;
}
DOAX2_NATIVE60_OBJECT_CHILD(sub_8241D600, 13)
DOAX2_NATIVE60_OBJECT_CHILD(sub_82421118, 14)
DOAX2_NATIVE60_OBJECT_CHILD(sub_82421680, 15)
struct Native60TrackSampleContext {
  uint32_t phase_index = 0;
  uint32_t limit = 0;
  float fraction = 0;
};
static thread_local Native60TrackSampleContext native60_track_sample;
REX_EXTERN(__imp__sub_82417810);
REX_HOOK_RAW(sub_82417810) {
  const auto sample = native60_track_sample;
  const bool fractional = sample.fraction > 0 &&
                          ctx.r3.u32 == sample.phase_index &&
                          ctx.r4.u32 == sample.limit &&
                          sample.phase_index + 1 < sample.limit;
  if (!fractional) {
    __imp__sub_82417810(ctx, base);
    return;
  }
  PPCContext next = ctx;
  next.r3.u64 = sample.phase_index + 1;
  // Run the real guest invocation last so its stack/register side effects
  // remain the authoritative ones for the caller.
  __imp__sub_82417810(next, base);
  __imp__sub_82417810(ctx, base);
  const double current_value = ctx.f1.f64;
  ctx.f1.f64 = double(float(current_value + sample.fraction *
                          (next.f1.f64 - current_value)));
  static thread_local uint64_t samples = 0;
  if (++samples <= 6 || samples % 120 == 0)
    REXLOG_INFO("DOAX2 native60 fractional track sample: calls={} "
                "index={} fraction={} limit={} native={} next={} output={}; "
                "original track curve evaluated twice, no frame replay",
                samples, sample.phase_index, sample.fraction, sample.limit,
                current_value, next.f1.f64, ctx.f1.f64);
}
REX_EXTERN(__imp__sub_82421CF8);
REX_HOOK_RAW(sub_82421CF8) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_82421CF8");
  const uint32_t object = Native60SelectedCameraObject(base);
  const uint32_t track = ctx.r4.u32;
  const uint32_t slot = object && track == object + 832 ? 0 :
                        object && track == object + 1024 ? 1 : 2;
  const uint32_t observed = slot < 2 && object == ctx.r3.u32 ? object : 0;
  const auto camera_before = SnapshotNative60ObjectCamera(base, observed);
  constexpr std::array<uint32_t, 9> offsets =
      {64, 68, 72, 112, 124, 128, 132, 136, 140};
  std::array<uint32_t, offsets.size()> before{};
  if (observed)
    for (uint32_t i = 0; i < offsets.size(); ++i)
      before[i] = REX_LOAD_U32(track + offsets[i]);
  const auto prior_sample = native60_track_sample;
  native60_track_sample = {};
  // With the base speed scaled, 82421CF8 itself advances phase by the
  // fractional game step, but its curve lookup truncates to an integer
  // index. Sample the authored curve fractionally for every scene object's
  // tracks so motion changes each 60 Hz update instead of every other one.
  const bool scaled_speed = float(REXCVAR_GET(doax2_native60_tick_scale)) != 1.0f;
  const uint32_t scene_object = ctx.r3.u32;
  if (scaled_speed && IsNative60SceneObject(base, scene_object) &&
      (track == scene_object + 832 || track == scene_object + 1024)) {
    const float phase = std::bit_cast<float>(REX_LOAD_U32(track + 128));
    const float phase_floor = std::floor(phase);
    const uint32_t limit = REX_LOAD_U32(track + 124);
    if (std::isfinite(phase) && phase >= 0 && phase < limit && limit <= 4096)
      native60_track_sample = {static_cast<uint32_t>(phase_floor), limit,
                               phase - phase_floor};
  } else if (observed && REXCVAR_GET(doax2_native60_preview_motion_half) &&
      REX_LOAD_U32(observed + 3580) == 3) {
    const float phase = std::bit_cast<float>(before[5]);
    const float phase_floor = std::floor(phase);
    const uint32_t limit = before[4];
    if (std::isfinite(phase) && phase >= 0 && phase < limit && limit <= 4096)
      native60_track_sample = {static_cast<uint32_t>(phase_floor), limit,
                               phase - phase_floor};
  }
  const bool scene_track = scaled_speed && IsNative60SceneObject(base, scene_object) &&
                           (track == scene_object + 832 || track == scene_object + 1024);
  const float remaining_before = scene_track ? Native60LoadFloat(base, track + 136) : 0.0f;
  const float phase_before = scene_track ? Native60LoadFloat(base, track + 128) : 0.0f;
  {
    // Test F (intro bisect 6): only scene camera objects get the unscaled
    // step; other callers (e.g. jet model tracks) run at the scaled step.
    const bool camera_object = IsNative60SceneObject(base, scene_object);
    if (!camera_object && REXCVAR_GET(doax2_native60_counter_scan)) {
      static std::mutex mutex;
      static std::set<uint32_t> seen;
      std::lock_guard lock(mutex);
      if (seen.size() < 64 && seen.insert(scene_object).second)
        REXLOG_INFO("DOAX2 native60 track user (non-camera): object={:08X} track={:08X} lr={:08X}",
                    scene_object, track, uint32_t(ctx.lr));
    }
    if (!camera_object && REXCVAR_GET(doax2_native60_intro_bisect) == 6) {
      __imp__sub_82421CF8(ctx, base);
    } else {
      Native60UnscaledStep unscaled(base);
      __imp__sub_82421CF8(ctx, base);
    }
  }
  // Camera-script waits (+136) count down by the (unscaled) step once per
  // update. Step-sized shots start at limit - 1 (599 of 600) and need that,
  // but the jet-ski intro shot waits an authored 220 frames on a 240-entry
  // table (log 201), so the race and READY/GO began after 220 updates:
  // 3.7 s instead of 7.3 s. Halve single-step decrements of such authored
  // waits during the jet race (other minigames were fixed and confirmed).
  if (scene_track) {
    struct Wait { uint32_t track = 0; float start = 0; uint32_t limit = 0; bool authored = false; };
    static std::array<Wait, 16> waits{};
    const uint32_t objects = REX_LOAD_U32(0x839BC0F0 + 928);
    const uint32_t index = ((scene_object - objects) / 4144) * 2 + (track == scene_object + 1024);
    const float remaining = Native60LoadFloat(base, track + 136);
    const uint32_t limit = REX_LOAD_U32(track + 124);
    if (index < waits.size()) {
      Wait& wait = waits[index];
      if (wait.track != track || limit != wait.limit || remaining > remaining_before) {
        // New shot (the call itself already applied its first decrement).
        wait = {track, remaining + 1.0f, limit,
                std::fabs(remaining + 1.0f - float(limit)) > 1.5f};
        static uint64_t shots = 0;
        if (wait.authored && Native60JetRaceActive() && ++shots <= 64)
          REXLOG_INFO("DOAX2 native60 jet authored camera wait: object={} "
                      "track_offset={} wait={} limit={} engine_frame={}",
                      index / 2, track - scene_object, wait.start, limit,
                      Doax2EngineFrameCount());
      } else if ((wait.authored && Native60JetRaceActive()) ||
                 (native60_slide_sim_frame != ~0ull && Doax2EngineFrameCount() - native60_slide_sim_frame <= 4)) {
        const float delta = remaining_before - remaining;
        if (delta > 0.0f && delta <= 1.01f)
          REX_STORE_U32(track + 136,
                        std::bit_cast<uint32_t>(remaining_before - 0.5f * delta));
        // The shot's phase also advances by the unscaled step (the fly-over
        // reached phase 211 of 240 in the 7 s intro at 30 Hz); halve it so
        // the 60 Hz intro (now full length, race state pacing) matches.
        const float phase = Native60LoadFloat(base, track + 128);
        const float advance = phase - phase_before;
        if (advance > 0.0f && advance <= 1.01f)
          REX_STORE_U32(track + 128, std::bit_cast<uint32_t>(phase_before + 0.5f * advance));
      }
    }
  }
  if (IsNative60SceneObject(base, scene_object)) {
    // Shot census (both rates: 30 FPS reference comparison): log each new track length so step-sized tables (600 at
    // step 0.5) can be told apart from fixed-length cutscene tracks.
    static std::array<uint32_t, 16> last_limit{};
    const uint32_t objects = REX_LOAD_U32(0x839BC0F0 + 928);
    const uint32_t slot_index =
        ((scene_object - objects) / 4144) * 2 + (track == scene_object + 1024);
    const uint32_t limit = REX_LOAD_U32(track + 124);
    static uint64_t shots = 0;
    if (slot_index < last_limit.size() && limit != last_limit[slot_index] &&
        ++shots <= 400) {
      last_limit[slot_index] = limit;
      REXLOG_INFO("DOAX2 native60 camera shot: object_index={} track_offset={} "
                  "limit={} phase={} remaining={} mode={} step={} engine_frame={}",
                  slot_index / 2, track - scene_object, int32_t(limit),
                  Native60LoadFloat(base, track + 128),
                  Native60LoadFloat(base, track + 136),
                  REX_LOAD_U32(scene_object + 3580),
                  Native60LoadFloat(base, kNative60GameStep),
                  Doax2EngineFrameCount());
    }
  }
  native60_track_sample = prior_sample;
  if (observed && REXCVAR_GET(doax2_native60_preview_motion_half) &&
      IsNative60SceneObject(base, observed) &&
      REX_LOAD_U32(observed + 3580) == 3) {
    const auto phase = doax2::HalfExpectedNativePhaseStep(
        std::bit_cast<float>(before[5]),
        std::bit_cast<float>(REX_LOAD_U32(track + 128)), 1.0f);
    const auto remaining = doax2::HalfExpectedNativePhaseStep(
        std::bit_cast<float>(before[7]),
        std::bit_cast<float>(REX_LOAD_U32(track + 136)), -1.0f);
    if (phase && remaining) {
      REX_STORE_U32(track + 128, std::bit_cast<uint32_t>(*phase));
      REX_STORE_U32(track + 136, std::bit_cast<uint32_t>(*remaining));
      static thread_local std::array<uint64_t, 2> corrections{};
      const uint64_t count = ++corrections[slot];
      if (count <= 6 || count % 120 == 0)
        REXLOG_INFO("DOAX2 native60 preview motion correction: slot={} "
                    "calls={} track={:08X} phase={} remaining={}; "
                    "original track evaluation executed",
                    slot, count, track, *phase, *remaining);
    }
  }
  ReportNative60ObjectCamera(base, "sub_82421CF8", observed,
                             camera_before, 16);
  if (!observed) return;
  std::array<uint32_t, offsets.size()> after{};
  uint32_t changed_mask = 0;
  for (uint32_t i = 0; i < offsets.size(); ++i) {
    after[i] = REX_LOAD_U32(track + offsets[i]);
    changed_mask |= (before[i] != after[i]) << i;
  }
  static thread_local std::array<uint64_t, 2> calls{}, phase_changes{};
  const uint64_t count = ++calls[slot];
  phase_changes[slot] += (changed_mask & (1u << 5)) != 0;
  if (count <= 6 || count % 120 == 0)
    REXLOG_INFO("DOAX2 native60 motion track: slot={} calls={} "
                "phase_changes={} changed_mask={:03X} object={:08X} "
                "track={:08X} phase={} -> {} fraction={} -> {} "
                "reverse={} -> {} reverse_fraction={} -> {} "
                "limit={} status={} -> {}; read-only",
                slot, count, phase_changes[slot], changed_mask, object,
                track, std::bit_cast<float>(before[5]),
                std::bit_cast<float>(after[5]),
                std::bit_cast<float>(before[6]),
                std::bit_cast<float>(after[6]),
                std::bit_cast<float>(before[7]),
                std::bit_cast<float>(after[7]),
                std::bit_cast<float>(before[8]),
                std::bit_cast<float>(after[8]),
                static_cast<int32_t>(after[4]), before[3], after[3]);
}
#undef DOAX2_NATIVE60_OBJECT_CHILD

// Volleyball ball (8394E878): 82685040 evaluates an analytic flight from
// time +7364, which 82681628 advances by the game step. The launch helpers
// below set a new flight; 82684B08/82684C30 take the planned flight time in
// f1. Logging planned versus elapsed ball time at the next launch shows
// whether hitters contact the ball when the plan expected (read-only).
constexpr uint32_t kNative60Ball = 0x8394E878;
std::array<uint64_t, 4> native60_motion_start_update{};
std::array<float, 4> native60_queue_target{};
struct Native60BallFlight {
  const char* kind = "none";
  uint32_t caller = 0;
  float planned = 0;
};
Native60BallFlight native60_ball_flight;
uint64_t native60_ball_launches = 0;

void ReportNative60BallLaunch(uint8_t* base, const char* kind, uint32_t caller,
                              float planned, const PPCContext& ctx) {
  const float elapsed = Native60LoadFloat(base, kNative60Ball + 7364);
  const auto previous = native60_ball_flight;
  native60_ball_flight = {kind, caller, planned};
  if (++native60_ball_launches > 2000) return;
  REXLOG_INFO("DOAX2 native60 ball launch: n={} kind={} caller={:08X} "
              "planned_T={} args=({},{},{}) | prev kind={} caller={:08X} "
              "planned_T={} elapsed={} ratio={} | pos=({:.2f},{:.2f},{:.2f}) "
              "target=({:.2f},{:.2f},{:.2f}) step={} engine_frame={}",
              native60_ball_launches, kind, caller, planned, ctx.f1.f64,
              ctx.f2.f64, ctx.f3.f64, previous.kind, previous.caller,
              previous.planned, elapsed,
              previous.planned > 0 ? elapsed / previous.planned : 0.0f,
              Native60LoadFloat(base, kNative60Ball + 0),
              Native60LoadFloat(base, kNative60Ball + 4),
              Native60LoadFloat(base, kNative60Ball + 8),
              Native60LoadFloat(base, kNative60Ball + 24),
              Native60LoadFloat(base, kNative60Ball + 28),
              Native60LoadFloat(base, kNative60Ball + 32),
              Native60LoadFloat(base, kNative60GameStep),
              Doax2EngineFrameCount());
}

// Object delta probe: diff the first 1 KiB of the object passed in r3
// across one guest call and tally per-offset changes of exactly +-1 (int
// or float) versus +-0.5, reporting every 600 calls. Beach Flags state is
// heap allocated and invisible to the static counter scan.
struct Native60ObjectDelta {
  const char* name;
  uint32_t object = 0;
  uint64_t calls = 0;
  std::array<uint32_t, 256> before{};
  std::array<uint16_t, 256> int_hits{}, float_hits{}, half_hits{}, changes{};
};
void Native60ObjectDeltaBegin(uint8_t* base, Native60ObjectDelta& probe,
                              uint32_t object) {
  if (!REXCVAR_GET(doax2_native60_counter_scan) || object < 0x40000000) {
    probe.object = 0;
    return;
  }
  if (object != probe.object) {
    probe.int_hits = {}; probe.float_hits = {};
    probe.half_hits = {}; probe.changes = {};
    probe.calls = 0;
  }
  probe.object = object;
  for (uint32_t i = 0; i < 256; ++i) probe.before[i] = REX_LOAD_U32(object + i * 4);
}
void Native60ObjectDeltaEnd(uint8_t* base, Native60ObjectDelta& probe) {
  if (!probe.object) return;
  const uint32_t object = probe.object;
  ++probe.calls;
  for (uint32_t i = 0; i < 256; ++i) {
    const uint32_t now = REX_LOAD_U32(object + i * 4), old = probe.before[i];
    if (now == old) continue;
    ++probe.changes[i];
    const int32_t int_delta = int32_t(now - old);
    if (int_delta == 1 || int_delta == -1) ++probe.int_hits[i];
    const float delta = std::bit_cast<float>(now) - std::bit_cast<float>(old);
    if (delta == 1.0f || delta == -1.0f) ++probe.float_hits[i];
    else if (delta == 0.5f || delta == -0.5f) ++probe.half_hits[i];
  }
  if (probe.calls % 600 != 0) return;
  static uint32_t reports = 0;
  if (++reports > 200) return;
  for (uint32_t i = 0; i < 256; ++i) {
    if (probe.changes[i] < probe.calls / 4) continue;
    const uint32_t value = REX_LOAD_U32(object + i * 4);
    REXLOG_INFO("DOAX2 native60 object delta: fn={} object={:08X} calls={} "
                "offset={} changes={} int1={} float1={} half={} int={} float={}",
                probe.name, object, probe.calls, i * 4, probe.changes[i],
                probe.int_hits[i], probe.float_hits[i], probe.half_hits[i],
                int32_t(value), std::bit_cast<float>(value));
  }
  probe.int_hits = {}; probe.float_hits = {};
  probe.half_hits = {}; probe.changes = {};
}
Native60ObjectDelta native60_flags_a{"826AC790"}, native60_flags_b{"826ACB20"};
// Beach Flags stutter dump (diagnostics): the F10 bursts show the race view
// stepping back for one update every ~20 updates while the camera position
// (839BC1D0+48) is smooth. Once per race, 200 updates after the race camera
// (mode 13) starts, record up to 300 updates (written early if the race ends) of every changing word in the camera,
// the race object, both runner records and both pose records, so the field
// that steps back can be found. Floats are logged as values, others as hex.
void Native60FlagsCameraDump(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  constexpr uint32_t scene = 0x839BC0F0;
  const uint32_t index = REX_LOAD_U32(scene + 968);
  const uint32_t objects = REX_LOAD_U32(scene + 928);
  const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
  const bool race = object && REX_LOAD_U32(object + 3580) == 13;
  constexpr uint32_t kSamples = 300, kSkip = 200;
  struct Block { const char* name; uint32_t address; uint32_t bytes; };
  static uint32_t race_updates = 0;
  static bool dumped_this_race = false;
  static std::vector<Block> blocks;
  static std::vector<std::vector<uint32_t>> samples;
  static uint32_t collected = 0;
  auto dump = [&]() {
    dumped_this_race = true;
    static uint32_t dumps = 0;
    ++dumps;
    REXLOG_INFO("DOAX2 native60 flags dump {}: {} updates ending at engine frame {}", dumps,
                collected, Doax2EngineFrameCount());
    for (size_t b = 0; b < blocks.size(); ++b) {
      const uint32_t words = blocks[b].bytes / 4;
      for (uint32_t w = 0; w < words; ++w) {
        bool changed = false, is_float = true;
        for (uint32_t s = 0; s < collected; ++s) {
          const uint32_t v = samples[b][s * words + w];
          if (s && v != samples[b][(s - 1) * words + w]) changed = true;
          const float f = std::bit_cast<float>(v);
          if (!std::isfinite(f) || (v && std::fabs(f) < 1e-6f) || std::fabs(f) > 1e6f)
            is_float = false;
        }
        if (!changed) continue;
        std::string line;
        for (uint32_t s = 0; s < collected; ++s) {
          const uint32_t v = samples[b][s * words + w];
          line += is_float ? fmt::format(" {:.5g}", std::bit_cast<float>(v))
                           : fmt::format(" {:08X}", v);
        }
        REXLOG_INFO("DOAX2 native60 flags dump {} {}+{}:{}", dumps, blocks[b].name, w * 4,
                    line);
      }
    }
  };
  if (!race) {
    if (!dumped_this_race && collected >= 60) dump();
    race_updates = 0;
    collected = 0;
    dumped_this_race = false;
    return;
  }
  if (dumped_this_race || ++race_updates <= kSkip) return;
  if (race_updates == kSkip + 1) {
    const uint32_t runners = REX_LOAD_U32(0x8398A4A0 + 32);
    blocks = {{"camera", 0x839BC1D0, 256},
              {"race", 0x8398A4A0, 256},
              {"pose0", 0x834271A0, 288},
              {"pose1", 0x834272C0, 288}};
    if (runners >= 0x40000000 && runners < 0xA0000000) {
      blocks.push_back({"runner0", runners, 192});
      blocks.push_back({"runner1", runners + 192, 192});
    }
    samples.assign(blocks.size(), {});
    collected = 0;
  }
  for (size_t b = 0; b < blocks.size(); ++b)
    for (uint32_t offset = 0; offset < blocks[b].bytes; offset += 4)
      samples[b].push_back(REX_LOAD_U32(blocks[b].address + offset));
  if (++collected >= kSamples) dump();
}
// Same dump during the jet-ski race (camera mode 9): camera turn stepping
// in two-frame pairs while steering (F10 bursts, 2026-10-05).
void Native60JetCameraDump(uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  constexpr uint32_t scene = 0x839BC0F0;
  const uint32_t index = REX_LOAD_U32(scene + 968);
  const uint32_t objects = REX_LOAD_U32(scene + 928);
  const uint32_t object = objects && index < 8 ? objects + index * 4144 : 0;
  const bool race = object && REX_LOAD_U32(object + 3580) == 9;
  // Same real-time window at 60 and 30 updates per second (3.3 s .. 8.3 s).
  const bool half = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f;
  const uint32_t kSamples = half ? 300 : 150, kSkip = half ? 200 : 100;
  struct Block { const char* name; uint32_t address; uint32_t bytes; };
  static uint32_t race_updates = 0;
  static bool dumped_this_race = false;
  static std::vector<Block> blocks;
  static std::vector<std::vector<uint32_t>> samples;
  static uint32_t collected = 0;
  auto dump = [&]() {
    dumped_this_race = true;
    static uint32_t dumps = 0;
    ++dumps;
    REXLOG_INFO("DOAX2 native60 jet dump {}: {} updates ending at engine frame {}", dumps,
                collected, Doax2EngineFrameCount());
    for (size_t b = 0; b < blocks.size(); ++b) {
      const uint32_t words = blocks[b].bytes / 4;
      for (uint32_t w = 0; w < words; ++w) {
        bool changed = false, is_float = true;
        for (uint32_t s = 0; s < collected; ++s) {
          const uint32_t v = samples[b][s * words + w];
          if (s && v != samples[b][(s - 1) * words + w]) changed = true;
          const float f = std::bit_cast<float>(v);
          if (!std::isfinite(f) || (v && std::fabs(f) < 1e-6f) || std::fabs(f) > 1e6f)
            is_float = false;
        }
        if (!changed) continue;
        std::string line;
        for (uint32_t s = 0; s < collected; ++s) {
          const uint32_t v = samples[b][s * words + w];
          line += is_float ? fmt::format(" {:.9g}", std::bit_cast<float>(v))
                           : fmt::format(" {:08X}", v);
        }
        REXLOG_INFO("DOAX2 native60 jet dump {} {}+{}:{}", dumps, blocks[b].name, w * 4,
                    line);
      }
    }
  };
  if (!race) {
    if (!dumped_this_race && collected >= 60) dump();
    race_updates = 0;
    collected = 0;
    dumped_this_race = false;
    return;
  }
  if (dumped_this_race || ++race_updates <= kSkip) return;
  if (race_updates == kSkip + 1) {
    const uint32_t runners = REX_LOAD_U32(0x8398A4A0 + 32);
    blocks = {{"camera", 0x839BC1D0, 256},
              {"camobj", object, 4144},
              {"player0", 0x833B8520, 152},
              {"jetphys", 0x834CDB00, 0x344 * 4},
              {"jetrec", 0x834F7E00, 0xA0 * 4},
              {"jetdt", 0x823EC120, 16},
              {"jetbody", 0x834D99C0, 208 * 4},
              {"jetrig", 0x83CA3050, 736 * 2}};
    samples.assign(blocks.size(), {});
    collected = 0;
  }
  for (size_t b = 0; b < blocks.size(); ++b)
    for (uint32_t offset = 0; offset < blocks[b].bytes; offset += 4)
      samples[b].push_back(REX_LOAD_U32(blocks[b].address + offset));
  if (++collected >= kSamples) dump();
}
// Player-state position writer probe (diagnostics, Beach Flags run stutter):
// the runner record copies its position from the player state table
// 833B8520 (+12/+16/+20, 152 bytes per player) in 826AF880, so the stall at
// run-motion switches happens in whatever writes the player state. Every
// function referencing the table (447, minus those hooked elsewhere) is
// wrapped; a call counts as a write only when it changed a player position
// and no nested probed call did (innermost writer).
constexpr uint32_t kNative60PlayerStates = 0x833B8520;
using Native60PosSnapshot = std::array<uint32_t, 12>;
inline Native60PosSnapshot Native60PosSnap(uint8_t* base) {
  Native60PosSnapshot s;
  for (uint32_t p = 0; p < 4; ++p)
    for (uint32_t c = 0; c < 3; ++c)
      s[p * 3 + c] = REX_LOAD_U32(kNative60PlayerStates + p * 152 + 12 + c * 4);
  return s;
}
// Pose-camera stutter (volleyball between-point/win poses, Beach Flags win
// pose): the scene camera block 839BC1D0 (rotation + position, 16 words)
// changes only every other update in those modes (log 205 alternation
// scan). The same probes also attribute its innermost writer per camera mode.
// Groups: 0 view matrix (object+512), 1 object+32..+63 and +96..+127,
// 2 eye/target block object+192..+255, 3 character point 838FB650..+60.
using Native60CamSnapshot = std::array<uint32_t, 64>;
inline uint32_t Native60ActiveCameraObject(uint8_t* base) {
  const uint32_t index = REX_LOAD_U32(0x839BC0F0 + 968);
  const uint32_t objects = REX_LOAD_U32(0x839BC0F0 + 928);
  return objects && index < 8 ? objects + index * 4144 : 0;
}
// Watches the active camera object's view matrix (object + 512); 824187E0
// copies it into the scene camera block 839BC1D0 every update (log 231).
inline Native60CamSnapshot Native60CamSnap(uint8_t* base) {
  Native60CamSnapshot s{};
  const uint32_t object = Native60ActiveCameraObject(base);
  for (uint32_t i = 0; i < 16; ++i) s[48 + i] = REX_LOAD_U32(0x838FB650 + i * 4);
  if (!object) return s;
  for (uint32_t i = 0; i < 16; ++i) {
    s[i] = REX_LOAD_U32(object + 512 + i * 4);
    s[16 + i] = REX_LOAD_U32(object + (i < 8 ? 32 + i * 4 : 96 + (i - 8) * 4));
    s[32 + i] = REX_LOAD_U32(object + 192 + i * 4);
  }
  return s;
}
inline uint32_t Native60CamGroups(const Native60CamSnapshot& a, const Native60CamSnapshot& b) {
  uint32_t mask = 0;
  for (uint32_t i = 0; i < 64; ++i)
    if (a[i] != b[i]) mask |= 1u << (i / 16);
  return mask;
}
inline uint32_t Native60CameraModeNow(uint8_t* base) {
  const uint32_t index = REX_LOAD_U32(0x839BC0F0 + 968);
  const uint32_t objects = REX_LOAD_U32(0x839BC0F0 + 928);
  return objects && index < 8 ? REX_LOAD_U32(objects + index * 4144 + 3580) : 0;
}
// Watch list (Butt Battle, log 247): shared character timers still counting
// once per update during the match (action/knock-down durations).
constexpr std::array<uint32_t, 16> kNative60WatchAddresses = {
    0x833A2FC4, 0x833A30C8, 0x833A3210, 0x833A3314, 0x833A313C, 0x833A3388,
    0x833B3BDC, 0x833B3C8C, 0x833B3D3C, 0x838FCD14, 0x833B4338, 0x833B433C,
    0x833B4368, 0x833A2F48, 0x833A3298, 0x833B3B44};
using Native60WatchSnapshot = std::array<uint32_t, 16>;
inline Native60WatchSnapshot Native60WatchSnap(uint8_t* base) {
  Native60WatchSnapshot s;
  for (uint32_t i = 0; i < 16; ++i) s[i] = REX_LOAD_U32(kNative60WatchAddresses[i]);
  return s;
}
inline uint32_t Native60WatchMask(const Native60WatchSnapshot& a, const Native60WatchSnapshot& b) {
  uint32_t mask = 0;
  for (uint32_t i = 0; i < 16; ++i)
    if (a[i] != b[i]) mask |= 1u << i;
  return mask;
}
struct Native60PosWriter {
  uint64_t calls = 0, writes = 0, frames = 0, last_frame = ~0ull;
};
inline std::mutex native60_pos_mutex;
inline std::map<const char*, Native60PosWriter> native60_pos_writers;
inline std::map<std::tuple<const char*, uint32_t, uint32_t>, Native60PosWriter> native60_cam_writers;
inline thread_local int native60_pos_depth = 0;
inline thread_local std::array<bool, 66> native60_pos_child_wrote{};
inline thread_local std::array<uint32_t, 66> native60_cam_child_wrote{};
inline thread_local std::array<uint32_t, 66> native60_watch_child_wrote{};
inline std::map<std::pair<const char*, uint32_t>, Native60PosWriter> native60_watch_writers;
inline void Native60WriterCount(Native60PosWriter& w, bool write) {
  ++w.calls;
  if (!write) return;
  ++w.writes;
  const uint64_t frame = Doax2EngineFrameCount();
  if (frame != w.last_frame) {
    ++w.frames;
    w.last_frame = frame;
  }
}
inline void Native60PosRecord(const char* name, bool pos_write, uint32_t cam_groups, uint32_t mode) {
  std::lock_guard lock(native60_pos_mutex);
  Native60WriterCount(native60_pos_writers[name], pos_write);
  for (uint32_t g = 0; g < 4; ++g)
    if (cam_groups & (1u << g)) Native60WriterCount(native60_cam_writers[{name, mode, g}], true);
}
inline void Native60WatchRecord(const char* name, uint32_t mask) {
  if (!mask) return;
  std::lock_guard lock(native60_pos_mutex);
  for (uint32_t i = 0; i < 16; ++i)
    if (mask & (1u << i)) Native60WriterCount(native60_watch_writers[{name, i}], true);
}
inline std::map<uint32_t, uint64_t> native60_mode_updates;
void Native60CountModeUpdate(uint8_t* base) {
  std::lock_guard lock(native60_pos_mutex);
  ++native60_mode_updates[Native60CameraModeNow(base)];
}
void ReportNative60PosWriters() {
  std::lock_guard lock(native60_pos_mutex);
  std::string line;
  for (auto& [mode, updates] : native60_mode_updates)
    line += fmt::format(" [mode={} updates={}]", mode, updates);
  native60_mode_updates.clear();
  REXLOG_INFO("DOAX2 native60 camera mode updates (600 updates):{}", line);
  line.clear();
  for (auto& [name, w] : native60_pos_writers) {
    if (w.writes) line += fmt::format(" [{} calls={} writes={} frames={}]", name, w.calls,
                                      w.writes, w.frames);
    w = {};
  }
  if (!line.empty())
    REXLOG_INFO("DOAX2 native60 player position writers (600 updates):{}", line);
  line.clear();
  for (auto& [key, w] : native60_cam_writers) {
    if (w.writes) line += fmt::format(" [{} mode={} group={} writes={} frames={}]",
                                      std::get<0>(key), std::get<1>(key), std::get<2>(key),
                                      w.writes, w.frames);
    w = {};
  }
  if (!line.empty())
    REXLOG_INFO("DOAX2 native60 camera object matrix writers (600 updates):{}", line);
  line.clear();
  for (auto& [key, w] : native60_watch_writers) {
    if (w.writes) line += fmt::format(" [{} addr={:08X} writes={} frames={}]", key.first,
                                      kNative60WatchAddresses[key.second], w.writes, w.frames);
    w = {};
  }
  if (!line.empty())
    REXLOG_INFO("DOAX2 native60 watched timer writers (600 updates):{}", line);
}
Native60WriterProbeState Native60WriterProbeBegin(uint8_t* base) {
  Native60WriterProbeState state;
  if (!REXCVAR_GET(doax2_native60_counter_scan) || native60_pos_depth >= 64) return state;
  state.active = true;
  state.pos = Native60PosSnap(base);
  state.cam = Native60CamSnap(base);
  state.watch = Native60WatchSnap(base);
  state.depth = ++native60_pos_depth;
  native60_pos_child_wrote[state.depth] = false;
  native60_cam_child_wrote[state.depth] = 0;
  native60_watch_child_wrote[state.depth] = 0;
  return state;
}
void Native60WriterProbeEnd(uint8_t* base, const char* name, const Native60WriterProbeState& state) {
  if (!state.active) return;
  --native60_pos_depth;
  const bool wrote = Native60PosSnap(base) != state.pos;
  const uint32_t cam_groups = Native60CamGroups(Native60CamSnap(base), state.cam);
  Native60PosRecord(name, wrote && !native60_pos_child_wrote[state.depth],
                    cam_groups & ~native60_cam_child_wrote[state.depth],
                    Native60CameraModeNow(base));
  if (wrote) native60_pos_child_wrote[state.depth - 1] = true;
  native60_cam_child_wrote[state.depth - 1] |= cam_groups;
  const uint32_t watch_mask = Native60WatchMask(Native60WatchSnap(base), state.watch);
  Native60WatchRecord(name, watch_mask & ~native60_watch_child_wrote[state.depth]);
  native60_watch_child_wrote[state.depth - 1] |= watch_mask;
}
#define DOAX2_NATIVE60_POS_PROBE(name) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    if (!REXCVAR_GET(doax2_native60_counter_scan) || native60_pos_depth >= 64) { \
      __imp__##name(ctx, base); \
      return; \
    } \
    const auto native60_pos_before = Native60PosSnap(base); \
    const auto native60_cam_before = Native60CamSnap(base); \
    const auto native60_watch_before = Native60WatchSnap(base); \
    const int native60_depth = ++native60_pos_depth; \
    native60_pos_child_wrote[native60_depth] = false; \
    native60_cam_child_wrote[native60_depth] = 0; \
    native60_watch_child_wrote[native60_depth] = 0; \
    __imp__##name(ctx, base); \
    --native60_pos_depth; \
    const bool native60_wrote = Native60PosSnap(base) != native60_pos_before; \
    const uint32_t native60_cam_groups = Native60CamGroups(Native60CamSnap(base), native60_cam_before); \
    Native60PosRecord(#name, native60_wrote && !native60_pos_child_wrote[native60_depth], \
                      native60_cam_groups & ~native60_cam_child_wrote[native60_depth], \
                      Native60CameraModeNow(base)); \
    if (native60_wrote) native60_pos_child_wrote[native60_depth - 1] = true; \
    native60_cam_child_wrote[native60_depth - 1] |= native60_cam_groups; \
    { const uint32_t native60_watch_mask = Native60WatchMask(Native60WatchSnap(base), native60_watch_before); \
      Native60WatchRecord(#name, native60_watch_mask & ~native60_watch_child_wrote[native60_depth]); \
      native60_watch_child_wrote[native60_depth - 1] |= native60_watch_mask; } \
  }
DOAX2_NATIVE60_POS_PROBE(sub_8241EAE8)
DOAX2_NATIVE60_POS_PROBE(sub_8241EEE8)
DOAX2_NATIVE60_POS_PROBE(sub_82420028)
DOAX2_NATIVE60_POS_PROBE(sub_82420358)
DOAX2_NATIVE60_POS_PROBE(sub_82420440)
DOAX2_NATIVE60_POS_PROBE(sub_82420C50)
DOAX2_NATIVE60_POS_PROBE(sub_82421348)
DOAX2_NATIVE60_POS_PROBE(sub_82422CC8)
DOAX2_NATIVE60_POS_PROBE(sub_82423B48)
DOAX2_NATIVE60_POS_PROBE(sub_82424820)
DOAX2_NATIVE60_POS_PROBE(sub_82426C98)
DOAX2_NATIVE60_POS_PROBE(sub_82429CA8)
DOAX2_NATIVE60_POS_PROBE(sub_8242AD90)
DOAX2_NATIVE60_POS_PROBE(sub_8242D1B8)
DOAX2_NATIVE60_POS_PROBE(sub_8242F0D0)
DOAX2_NATIVE60_POS_PROBE(sub_82430E30)
DOAX2_NATIVE60_POS_PROBE(sub_82431348)
DOAX2_NATIVE60_POS_PROBE(sub_82441148)
DOAX2_NATIVE60_POS_PROBE(sub_82441438)
DOAX2_NATIVE60_POS_PROBE(sub_82441A60)
DOAX2_NATIVE60_POS_PROBE(sub_82441B58)
DOAX2_NATIVE60_POS_PROBE(sub_82441EC8)
DOAX2_NATIVE60_POS_PROBE(sub_82447620)
DOAX2_NATIVE60_POS_PROBE(sub_82448320)
DOAX2_NATIVE60_POS_PROBE(sub_8244CAB8)
DOAX2_NATIVE60_POS_PROBE(sub_82454E38)
DOAX2_NATIVE60_POS_PROBE(sub_824585F8)
DOAX2_NATIVE60_POS_PROBE(sub_82458938)
DOAX2_NATIVE60_POS_PROBE(sub_824735E0)
DOAX2_NATIVE60_POS_PROBE(sub_82473718)
DOAX2_NATIVE60_POS_PROBE(sub_82473870)
DOAX2_NATIVE60_POS_PROBE(sub_824747D8)
DOAX2_NATIVE60_POS_PROBE(sub_82474A20)
DOAX2_NATIVE60_POS_PROBE(sub_82474F28)
DOAX2_NATIVE60_POS_PROBE(sub_824750E0)
DOAX2_NATIVE60_POS_PROBE(sub_82475280)
DOAX2_NATIVE60_POS_PROBE(sub_824788E8)
DOAX2_NATIVE60_POS_PROBE(sub_82483EA0)
DOAX2_NATIVE60_POS_PROBE(sub_8248A640)
DOAX2_NATIVE60_POS_PROBE(sub_8248A850)
DOAX2_NATIVE60_POS_PROBE(sub_8248A8F0)
DOAX2_NATIVE60_POS_PROBE(sub_8248BC00)
DOAX2_NATIVE60_POS_PROBE(sub_8248DF68)
DOAX2_NATIVE60_POS_PROBE(sub_8248E9E0)
DOAX2_NATIVE60_POS_PROBE(sub_8248F500)
DOAX2_NATIVE60_POS_PROBE(sub_8248FB60)
DOAX2_NATIVE60_POS_PROBE(sub_824908E0)
DOAX2_NATIVE60_POS_PROBE(sub_824B0490)
DOAX2_NATIVE60_POS_PROBE(sub_824B1D40)
DOAX2_NATIVE60_POS_PROBE(sub_824B1EA0)
DOAX2_NATIVE60_POS_PROBE(sub_824B1FC8)
DOAX2_NATIVE60_POS_PROBE(sub_824B25A0)
DOAX2_NATIVE60_POS_PROBE(sub_824B26B8)
DOAX2_NATIVE60_POS_PROBE(sub_824B27F0)
DOAX2_NATIVE60_POS_PROBE(sub_824B8D58)
DOAX2_NATIVE60_POS_PROBE(sub_824B91A0)
DOAX2_NATIVE60_POS_PROBE(sub_824B9BA0)
DOAX2_NATIVE60_POS_PROBE(sub_824BA0E0)
DOAX2_NATIVE60_POS_PROBE(sub_824BA2C0)
DOAX2_NATIVE60_POS_PROBE(sub_824BA360)
DOAX2_NATIVE60_POS_PROBE(sub_824BA480)
DOAX2_NATIVE60_POS_PROBE(sub_824BA508)
DOAX2_NATIVE60_POS_PROBE(sub_824BA6C8)
DOAX2_NATIVE60_POS_PROBE(sub_824BA758)
DOAX2_NATIVE60_POS_PROBE(sub_824BA820)
DOAX2_NATIVE60_POS_PROBE(sub_824BA998)
DOAX2_NATIVE60_POS_PROBE(sub_824BAA98)
DOAX2_NATIVE60_POS_PROBE(sub_824BAC88)
DOAX2_NATIVE60_POS_PROBE(sub_824BAD20)
DOAX2_NATIVE60_POS_PROBE(sub_824BB028)
DOAX2_NATIVE60_POS_PROBE(sub_824BB200)
DOAX2_NATIVE60_POS_PROBE(sub_824BB270)
DOAX2_NATIVE60_POS_PROBE(sub_824BB340)
DOAX2_NATIVE60_POS_PROBE(sub_824BB418)
DOAX2_NATIVE60_POS_PROBE(sub_824BB4E0)
DOAX2_NATIVE60_POS_PROBE(sub_824BB600)
DOAX2_NATIVE60_POS_PROBE(sub_824BB6D0)
DOAX2_NATIVE60_POS_PROBE(sub_824BB750)
DOAX2_NATIVE60_POS_PROBE(sub_824BB838)
DOAX2_NATIVE60_POS_PROBE(sub_824BB908)
DOAX2_NATIVE60_POS_PROBE(sub_824BBA30)
DOAX2_NATIVE60_POS_PROBE(sub_824BBB10)
DOAX2_NATIVE60_POS_PROBE(sub_824BBC10)
DOAX2_NATIVE60_POS_PROBE(sub_824BBC98)
DOAX2_NATIVE60_POS_PROBE(sub_824BD970)
DOAX2_NATIVE60_POS_PROBE(sub_824BE7D0)
DOAX2_NATIVE60_POS_PROBE(sub_824BFA58)
DOAX2_NATIVE60_POS_PROBE(sub_824C0098)
DOAX2_NATIVE60_POS_PROBE(sub_824C1160)
DOAX2_NATIVE60_POS_PROBE(sub_824C1208)
DOAX2_NATIVE60_POS_PROBE(sub_824C1958)
DOAX2_NATIVE60_POS_PROBE(sub_824F1560)
DOAX2_NATIVE60_POS_PROBE(sub_824FE718)
DOAX2_NATIVE60_POS_PROBE(sub_824FE830)
DOAX2_NATIVE60_POS_PROBE(sub_824FEC88)
DOAX2_NATIVE60_POS_PROBE(sub_824FED98)
DOAX2_NATIVE60_POS_PROBE(sub_824FEE70)
DOAX2_NATIVE60_POS_PROBE(sub_824FF338)
DOAX2_NATIVE60_POS_PROBE(sub_824FFAE0)
DOAX2_NATIVE60_POS_PROBE(sub_82501A18)
DOAX2_NATIVE60_POS_PROBE(sub_82502610)
DOAX2_NATIVE60_POS_PROBE(sub_825077C8)
DOAX2_NATIVE60_POS_PROBE(sub_825099F0)
DOAX2_NATIVE60_POS_PROBE(sub_82509F40)
DOAX2_NATIVE60_POS_PROBE(sub_8250A568)
DOAX2_NATIVE60_POS_PROBE(sub_8250A728)
DOAX2_NATIVE60_POS_PROBE(sub_8250BEB0)
DOAX2_NATIVE60_POS_PROBE(sub_8250D530)
DOAX2_NATIVE60_POS_PROBE(sub_8250D5F8)
DOAX2_NATIVE60_POS_PROBE(sub_8250D7A0)
DOAX2_NATIVE60_POS_PROBE(sub_8250DCF0)
DOAX2_NATIVE60_POS_PROBE(sub_8250DF88)
DOAX2_NATIVE60_POS_PROBE(sub_8250E260)
DOAX2_NATIVE60_POS_PROBE(sub_8250E7A0)
DOAX2_NATIVE60_POS_PROBE(sub_8250EAB0)
DOAX2_NATIVE60_POS_PROBE(sub_8250F890)
DOAX2_NATIVE60_POS_PROBE(sub_8250FA38)
DOAX2_NATIVE60_POS_PROBE(sub_8250FC50)
DOAX2_NATIVE60_POS_PROBE(sub_8250FE28)
// Root-motion step 82510218(player): adds the animation's root-motion delta
// for this update (83426FD0 + player*116: local x/y/z, cleared afterwards),
// rotated by the facing, to the player position (833B8520 + player*152
// +12/+16/+20). In Beach Flags the animation leaves that delta at zero on the
// update where the run motion loops/switches and doubles it on the next, or
// sometimes just doubles it (log 217/223), so the girls stall and jump about
// three times a second (also at 30 FPS); the race camera follows the runner.
// During steady running in Beach Flags: a zero delta takes the previous
// delta and the doubled one gives it back; a lone double step keeps one step
// and feeds the extra back over the next 8 updates (total travel unchanged).
inline std::atomic<uint64_t> native60_flags_frame{~0ull};
REX_EXTERN(__imp__sub_82510218);
REX_HOOK_RAW(sub_82510218) {
  struct Player {
    float last_x = 0, last_z = 0;
    std::array<float, 3> steps{};
    uint32_t steady = 0;
    bool carry = false;
    float carry_x = 0, carry_z = 0;
    float spread_x = 0, spread_z = 0;
    uint32_t spread_left = 0;
  };
  static std::array<Player, 4> players;
  static uint64_t fixes = 0, unwinds = 0, spreads = 0;
  const uint32_t player = ctx.r3.u32;
  const uint64_t flags = native60_flags_frame.load(std::memory_order_relaxed);
  const bool active = player < 4 && flags != ~0ull &&
                      Doax2EngineFrameCount() - flags <= 2;
  if (!active) {
    if (player < 4) players[player] = {};
    __imp__sub_82510218(ctx, base);
    return;
  }
  Player& p = players[player];
  const uint32_t delta = 0x83426FD0 + player * 116;
  float x = Native60LoadFloat(base, delta), z = Native60LoadFloat(base, delta + 8);
  const float raw_x = x, raw_z = z;
  const float last = std::sqrt(p.last_x * p.last_x + p.last_z * p.last_z);
  bool steady_steps = last > 0;
  for (float s : p.steps)
    steady_steps = steady_steps && s >= 0.02f && s <= 0.3f && std::fabs(s - last) <= 0.4f * last;
  const float along = last > 0 ? (x * p.last_x + z * p.last_z) / (last * last) : 0.0f;
  bool record = true;
  if (p.carry) {
    // Give back the carried step from the doubled delta, if it came.
    p.carry = false;
    const float carried = p.carry_x * p.carry_x + p.carry_z * p.carry_z;
    const float catch_up = x * p.carry_x + z * p.carry_z;
    if (carried > 0 && catch_up > 1.5f * carried && catch_up < 3.0f * carried) {
      x -= p.carry_x;
      z -= p.carry_z;
      ++unwinds;
    }
    p.steady = 1;
  } else if (x == 0.0f && z == 0.0f && p.steady >= 3 && steady_steps) {
    x = p.last_x;
    z = p.last_z;
    p.carry = true;
    p.carry_x = x;
    p.carry_z = z;
    p.steady = 0;
    record = false;
    if (++fixes <= 6 || fixes % 200 == 0)
      REXLOG_INFO("DOAX2 native60 root motion stall carried: player={} step=({:.4f},{:.4f}) "
                  "fixes={} unwinds={} spreads={}", player, x, z, fixes, unwinds, spreads);
  } else if (p.steady >= 2 && steady_steps && !p.spread_left && along > 1.6f &&
             along < 2.6f) {
    constexpr uint32_t kSpread = 8;
    p.spread_x = (x - p.last_x) / kSpread;
    p.spread_z = (z - p.last_z) / kSpread;
    p.spread_left = kSpread;
    x = p.last_x;
    z = p.last_z;
    p.steady = 0;
    record = false;
    if (++spreads <= 6 || spreads % 200 == 0)
      REXLOG_INFO("DOAX2 native60 root motion double step spread: player={} extra=({:.4f},{:.4f}) "
                  "spreads={}", player, p.spread_x * kSpread, p.spread_z * kSpread, spreads);
  } else if (p.spread_left) {
    x += p.spread_x;
    z += p.spread_z;
    --p.spread_left;
  }
  if (record) {
    // History uses the corrected single step (after undoing a carry).
    p.steps = {p.steps[1], p.steps[2], std::sqrt(x * x + z * z)};
    p.last_x = x;
    p.last_z = z;
    p.steady = std::min<uint32_t>(p.steady + 1, 1000);
  }
  if (x != raw_x) REX_STORE_U32(delta, std::bit_cast<uint32_t>(x));
  if (z != raw_z) REX_STORE_U32(delta + 8, std::bit_cast<uint32_t>(z));
  __imp__sub_82510218(ctx, base);
}
DOAX2_NATIVE60_POS_PROBE(sub_825103C0)
DOAX2_NATIVE60_POS_PROBE(sub_82511850)
DOAX2_NATIVE60_POS_PROBE(sub_82511AB8)
DOAX2_NATIVE60_POS_PROBE(sub_82511E00)
DOAX2_NATIVE60_POS_PROBE(sub_82511F00)
DOAX2_NATIVE60_POS_PROBE(sub_82511FA8)
DOAX2_NATIVE60_POS_PROBE(sub_82513058)
DOAX2_NATIVE60_POS_PROBE(sub_82513430)
DOAX2_NATIVE60_POS_PROBE(sub_82515748)
DOAX2_NATIVE60_POS_PROBE(sub_82515F30)
DOAX2_NATIVE60_POS_PROBE(sub_825160D0)
DOAX2_NATIVE60_POS_PROBE(sub_82517928)
DOAX2_NATIVE60_POS_PROBE(sub_82517DA8)
DOAX2_NATIVE60_POS_PROBE(sub_82517F48)
DOAX2_NATIVE60_POS_PROBE(sub_82518C50)
DOAX2_NATIVE60_POS_PROBE(sub_82519120)
DOAX2_NATIVE60_POS_PROBE(sub_82519728)
DOAX2_NATIVE60_POS_PROBE(sub_825359F0)
DOAX2_NATIVE60_POS_PROBE(sub_8255A248)
DOAX2_NATIVE60_POS_PROBE(sub_82561390)
DOAX2_NATIVE60_POS_PROBE(sub_82597038)
DOAX2_NATIVE60_POS_PROBE(sub_8259ABB0)
DOAX2_NATIVE60_POS_PROBE(sub_8259AE90)
DOAX2_NATIVE60_POS_PROBE(sub_8259B0D8)
DOAX2_NATIVE60_POS_PROBE(sub_8259B180)
DOAX2_NATIVE60_POS_PROBE(sub_8259B550)
DOAX2_NATIVE60_POS_PROBE(sub_825A4A68)
DOAX2_NATIVE60_POS_PROBE(sub_825A57F8)
DOAX2_NATIVE60_POS_PROBE(sub_825B3110)
DOAX2_NATIVE60_POS_PROBE(sub_825B3208)
DOAX2_NATIVE60_POS_PROBE(sub_825B33C0)
DOAX2_NATIVE60_POS_PROBE(sub_825B3498)
DOAX2_NATIVE60_POS_PROBE(sub_825B3680)
DOAX2_NATIVE60_POS_PROBE(sub_825B38B8)
DOAX2_NATIVE60_POS_PROBE(sub_825D3140)
DOAX2_NATIVE60_POS_PROBE(sub_825D3330)
DOAX2_NATIVE60_POS_PROBE(sub_825D4010)
DOAX2_NATIVE60_POS_PROBE(sub_825E7468)
DOAX2_NATIVE60_POS_PROBE(sub_825E9478)
DOAX2_NATIVE60_POS_PROBE(sub_825E9C58)
DOAX2_NATIVE60_POS_PROBE(sub_825E9E48)
DOAX2_NATIVE60_POS_PROBE(sub_825ECBE8)
DOAX2_NATIVE60_POS_PROBE(sub_825EEF00)
DOAX2_NATIVE60_POS_PROBE(sub_825F06A8)
DOAX2_NATIVE60_POS_PROBE(sub_82611A50)
DOAX2_NATIVE60_POS_PROBE(sub_82612330)
DOAX2_NATIVE60_POS_PROBE(sub_82612850)
DOAX2_NATIVE60_POS_PROBE(sub_82627FB0)
DOAX2_NATIVE60_POS_PROBE(sub_8262DD90)
DOAX2_NATIVE60_POS_PROBE(sub_8263DB38)
DOAX2_NATIVE60_POS_PROBE(sub_8263F740)
DOAX2_NATIVE60_POS_PROBE(sub_82640500)
DOAX2_NATIVE60_POS_PROBE(sub_82641EF0)
DOAX2_NATIVE60_POS_PROBE(sub_826423C0)
DOAX2_NATIVE60_POS_PROBE(sub_82642510)
DOAX2_NATIVE60_POS_PROBE(sub_82642BD8)
DOAX2_NATIVE60_POS_PROBE(sub_82643918)
DOAX2_NATIVE60_POS_PROBE(sub_826439A0)
DOAX2_NATIVE60_POS_PROBE(sub_82643A38)
DOAX2_NATIVE60_POS_PROBE(sub_82643DD8)
DOAX2_NATIVE60_POS_PROBE(sub_82643EC8)
DOAX2_NATIVE60_POS_PROBE(sub_82643F50)
DOAX2_NATIVE60_POS_PROBE(sub_82643FC8)
DOAX2_NATIVE60_POS_PROBE(sub_82644288)
DOAX2_NATIVE60_POS_PROBE(sub_826445D0)
DOAX2_NATIVE60_POS_PROBE(sub_826446C8)
DOAX2_NATIVE60_POS_PROBE(sub_82644820)
DOAX2_NATIVE60_POS_PROBE(sub_82649218)
DOAX2_NATIVE60_POS_PROBE(sub_82660738)
DOAX2_NATIVE60_POS_PROBE(sub_82663E10)
DOAX2_NATIVE60_POS_PROBE(sub_8266CD40)
DOAX2_NATIVE60_POS_PROBE(sub_8266D4A8)
DOAX2_NATIVE60_POS_PROBE(sub_826752D0)
DOAX2_NATIVE60_POS_PROBE(sub_82675FE0)
DOAX2_NATIVE60_POS_PROBE(sub_826763E0)
DOAX2_NATIVE60_POS_PROBE(sub_82676EC8)
DOAX2_NATIVE60_POS_PROBE(sub_826770D0)
DOAX2_NATIVE60_POS_PROBE(sub_82677598)
DOAX2_NATIVE60_POS_PROBE(sub_826777C0)
DOAX2_NATIVE60_POS_PROBE(sub_82677B38)
DOAX2_NATIVE60_POS_PROBE(sub_82677FE0)
DOAX2_NATIVE60_POS_PROBE(sub_82678310)
DOAX2_NATIVE60_POS_PROBE(sub_826784D8)
DOAX2_NATIVE60_POS_PROBE(sub_826785B0)
DOAX2_NATIVE60_POS_PROBE(sub_82678688)
DOAX2_NATIVE60_POS_PROBE(sub_82678BF0)
DOAX2_NATIVE60_POS_PROBE(sub_82678E50)
DOAX2_NATIVE60_POS_PROBE(sub_82678EB0)
DOAX2_NATIVE60_POS_PROBE(sub_826792A0)
DOAX2_NATIVE60_POS_PROBE(sub_82679438)
DOAX2_NATIVE60_POS_PROBE(sub_82679518)
DOAX2_NATIVE60_POS_PROBE(sub_826796F8)
DOAX2_NATIVE60_POS_PROBE(sub_82679E30)
DOAX2_NATIVE60_POS_PROBE(sub_82679F60)
DOAX2_NATIVE60_POS_PROBE(sub_8267A080)
DOAX2_NATIVE60_POS_PROBE(sub_8267A638)
DOAX2_NATIVE60_POS_PROBE(sub_8267A838)
DOAX2_NATIVE60_POS_PROBE(sub_8267A8B8)
DOAX2_NATIVE60_POS_PROBE(sub_8267A980)
DOAX2_NATIVE60_POS_PROBE(sub_8267AF70)
DOAX2_NATIVE60_POS_PROBE(sub_8267B0F0)
DOAX2_NATIVE60_POS_PROBE(sub_8267B638)
DOAX2_NATIVE60_POS_PROBE(sub_8267B6D0)
DOAX2_NATIVE60_POS_PROBE(sub_8267B730)
DOAX2_NATIVE60_POS_PROBE(sub_8267BAE0)
DOAX2_NATIVE60_POS_PROBE(sub_8267BBC0)
DOAX2_NATIVE60_POS_PROBE(sub_8267BCF0)
DOAX2_NATIVE60_POS_PROBE(sub_8267BF18)
DOAX2_NATIVE60_POS_PROBE(sub_8267C200)
DOAX2_NATIVE60_POS_PROBE(sub_8267C418)
DOAX2_NATIVE60_POS_PROBE(sub_8267C928)
DOAX2_NATIVE60_POS_PROBE(sub_8267CA70)
DOAX2_NATIVE60_POS_PROBE(sub_8267CDE8)
DOAX2_NATIVE60_POS_PROBE(sub_8267CF60)
DOAX2_NATIVE60_POS_PROBE(sub_8267D148)
DOAX2_NATIVE60_POS_PROBE(sub_8267D1C8)
DOAX2_NATIVE60_POS_PROBE(sub_8267D318)
DOAX2_NATIVE60_POS_PROBE(sub_8267D968)
DOAX2_NATIVE60_POS_PROBE(sub_8267DAD8)
DOAX2_NATIVE60_POS_PROBE(sub_8267E318)
DOAX2_NATIVE60_POS_PROBE(sub_8267E5C8)
DOAX2_NATIVE60_POS_PROBE(sub_8267E768)
DOAX2_NATIVE60_POS_PROBE(sub_8267E9A8)
DOAX2_NATIVE60_POS_PROBE(sub_8267EA88)
DOAX2_NATIVE60_POS_PROBE(sub_8267EC18)
DOAX2_NATIVE60_POS_PROBE(sub_8267EE00)
DOAX2_NATIVE60_POS_PROBE(sub_8267F3E8)
DOAX2_NATIVE60_POS_PROBE(sub_8267F968)
DOAX2_NATIVE60_POS_PROBE(sub_8267FE18)
DOAX2_NATIVE60_POS_PROBE(sub_82680768)
DOAX2_NATIVE60_POS_PROBE(sub_82680918)
DOAX2_NATIVE60_POS_PROBE(sub_82680D88)
DOAX2_NATIVE60_POS_PROBE(sub_82680FB0)
DOAX2_NATIVE60_POS_PROBE(sub_82681760)
DOAX2_NATIVE60_POS_PROBE(sub_82682948)
DOAX2_NATIVE60_POS_PROBE(sub_82682B00)
DOAX2_NATIVE60_POS_PROBE(sub_82682F60)
DOAX2_NATIVE60_POS_PROBE(sub_82683DB0)
DOAX2_NATIVE60_POS_PROBE(sub_82683EC8)
DOAX2_NATIVE60_POS_PROBE(sub_826843B0)
DOAX2_NATIVE60_POS_PROBE(sub_82684588)
DOAX2_NATIVE60_POS_PROBE(sub_82684760)
DOAX2_NATIVE60_POS_PROBE(sub_82685C68)
DOAX2_NATIVE60_POS_PROBE(sub_82685E60)
DOAX2_NATIVE60_POS_PROBE(sub_82686078)
DOAX2_NATIVE60_POS_PROBE(sub_826861B0)
DOAX2_NATIVE60_POS_PROBE(sub_826862B8)
DOAX2_NATIVE60_POS_PROBE(sub_82686338)
DOAX2_NATIVE60_POS_PROBE(sub_82686648)
DOAX2_NATIVE60_POS_PROBE(sub_82686780)
DOAX2_NATIVE60_POS_PROBE(sub_82686B10)
DOAX2_NATIVE60_POS_PROBE(sub_82686FF8)
DOAX2_NATIVE60_POS_PROBE(sub_826876D0)
DOAX2_NATIVE60_POS_PROBE(sub_826878E0)
DOAX2_NATIVE60_POS_PROBE(sub_82687DC0)
DOAX2_NATIVE60_POS_PROBE(sub_82688170)
DOAX2_NATIVE60_POS_PROBE(sub_82688268)
DOAX2_NATIVE60_POS_PROBE(sub_82688448)
DOAX2_NATIVE60_POS_PROBE(sub_82688DD8)
DOAX2_NATIVE60_POS_PROBE(sub_826896A0)
DOAX2_NATIVE60_POS_PROBE(sub_826898E8)
DOAX2_NATIVE60_POS_PROBE(sub_82689D88)
DOAX2_NATIVE60_POS_PROBE(sub_82689F38)
DOAX2_NATIVE60_POS_PROBE(sub_8268A028)
DOAX2_NATIVE60_POS_PROBE(sub_8268A5B8)
DOAX2_NATIVE60_POS_PROBE(sub_8268A7C8)
DOAX2_NATIVE60_POS_PROBE(sub_8268ABE8)
DOAX2_NATIVE60_POS_PROBE(sub_8268AF70)
DOAX2_NATIVE60_POS_PROBE(sub_8268B6D8)
DOAX2_NATIVE60_POS_PROBE(sub_8268C320)
DOAX2_NATIVE60_POS_PROBE(sub_8268C498)
DOAX2_NATIVE60_POS_PROBE(sub_8268C618)
DOAX2_NATIVE60_POS_PROBE(sub_8268C7A0)
DOAX2_NATIVE60_POS_PROBE(sub_8268C8A8)
DOAX2_NATIVE60_POS_PROBE(sub_8268DA38)
DOAX2_NATIVE60_POS_PROBE(sub_8268DDC8)
DOAX2_NATIVE60_POS_PROBE(sub_8268DF20)
DOAX2_NATIVE60_POS_PROBE(sub_8268E0F8)
DOAX2_NATIVE60_POS_PROBE(sub_8268E1D0)
DOAX2_NATIVE60_POS_PROBE(sub_8268E290)
DOAX2_NATIVE60_POS_PROBE(sub_8268E4C0)
DOAX2_NATIVE60_POS_PROBE(sub_8268E6F8)
DOAX2_NATIVE60_POS_PROBE(sub_8268E858)
DOAX2_NATIVE60_POS_PROBE(sub_8268EB28)
DOAX2_NATIVE60_POS_PROBE(sub_8268EBC8)
DOAX2_NATIVE60_POS_PROBE(sub_8268ED60)
DOAX2_NATIVE60_POS_PROBE(sub_8268EE68)
DOAX2_NATIVE60_POS_PROBE(sub_8268F768)
DOAX2_NATIVE60_POS_PROBE(sub_8268F930)
DOAX2_NATIVE60_POS_PROBE(sub_8268FA38)
DOAX2_NATIVE60_POS_PROBE(sub_8268FE80)
DOAX2_NATIVE60_POS_PROBE(sub_8268FF28)
DOAX2_NATIVE60_POS_PROBE(sub_826902C0)
DOAX2_NATIVE60_POS_PROBE(sub_82690438)
DOAX2_NATIVE60_POS_PROBE(sub_826906B0)
DOAX2_NATIVE60_POS_PROBE(sub_82690970)
DOAX2_NATIVE60_POS_PROBE(sub_82691088)
DOAX2_NATIVE60_POS_PROBE(sub_826911A0)
DOAX2_NATIVE60_POS_PROBE(sub_82691428)
DOAX2_NATIVE60_POS_PROBE(sub_826914B8)
DOAX2_NATIVE60_POS_PROBE(sub_82691CC0)
DOAX2_NATIVE60_POS_PROBE(sub_82691F38)
DOAX2_NATIVE60_POS_PROBE(sub_82691FE0)
DOAX2_NATIVE60_POS_PROBE(sub_826923A0)
DOAX2_NATIVE60_POS_PROBE(sub_82692BD8)
DOAX2_NATIVE60_POS_PROBE(sub_826931C8)
DOAX2_NATIVE60_POS_PROBE(sub_826965F8)
DOAX2_NATIVE60_POS_PROBE(sub_826966F0)
DOAX2_NATIVE60_POS_PROBE(sub_82696FE0)
DOAX2_NATIVE60_POS_PROBE(sub_82697668)
DOAX2_NATIVE60_POS_PROBE(sub_82697850)
DOAX2_NATIVE60_POS_PROBE(sub_826982B8)
DOAX2_NATIVE60_POS_PROBE(sub_82699DA8)
DOAX2_NATIVE60_POS_PROBE(sub_8269A138)
DOAX2_NATIVE60_POS_PROBE(sub_8269A208)
DOAX2_NATIVE60_POS_PROBE(sub_8269A398)
DOAX2_NATIVE60_POS_PROBE(sub_8269A530)
DOAX2_NATIVE60_POS_PROBE(sub_8269AA10)
DOAX2_NATIVE60_POS_PROBE(sub_8269ADF8)
DOAX2_NATIVE60_POS_PROBE(sub_8269AF08)
DOAX2_NATIVE60_POS_PROBE(sub_8269B110)
DOAX2_NATIVE60_POS_PROBE(sub_8269B2A8)
DOAX2_NATIVE60_POS_PROBE(sub_8269B588)
DOAX2_NATIVE60_POS_PROBE(sub_8269B5B8)
DOAX2_NATIVE60_POS_PROBE(sub_8269BF60)
DOAX2_NATIVE60_POS_PROBE(sub_8269C2E8)
DOAX2_NATIVE60_POS_PROBE(sub_8269C4C8)
DOAX2_NATIVE60_POS_PROBE(sub_8269CBD0)
DOAX2_NATIVE60_POS_PROBE(sub_8269D6D0)
DOAX2_NATIVE60_POS_PROBE(sub_8269DAD0)
DOAX2_NATIVE60_POS_PROBE(sub_8269DC08)
DOAX2_NATIVE60_POS_PROBE(sub_8269E078)
DOAX2_NATIVE60_POS_PROBE(sub_8269E118)
DOAX2_NATIVE60_POS_PROBE(sub_8269E560)
DOAX2_NATIVE60_POS_PROBE(sub_8269E750)
DOAX2_NATIVE60_POS_PROBE(sub_8269E878)
DOAX2_NATIVE60_POS_PROBE(sub_8269E930)
DOAX2_NATIVE60_POS_PROBE(sub_8269ED40)
DOAX2_NATIVE60_POS_PROBE(sub_8269F298)
DOAX2_NATIVE60_POS_PROBE(sub_8269F5C8)
DOAX2_NATIVE60_POS_PROBE(sub_8269F9F0)
DOAX2_NATIVE60_POS_PROBE(sub_8269FBF8)
DOAX2_NATIVE60_POS_PROBE(sub_8269FCE0)
DOAX2_NATIVE60_POS_PROBE(sub_8269FF40)
DOAX2_NATIVE60_POS_PROBE(sub_826A02B0)
DOAX2_NATIVE60_POS_PROBE(sub_826A0608)
DOAX2_NATIVE60_POS_PROBE(sub_826A0F80)
DOAX2_NATIVE60_POS_PROBE(sub_826A1560)
DOAX2_NATIVE60_POS_PROBE(sub_826A16A8)
DOAX2_NATIVE60_POS_PROBE(sub_826A1830)
DOAX2_NATIVE60_POS_PROBE(sub_826A1FD8)
DOAX2_NATIVE60_POS_PROBE(sub_826A3550)
DOAX2_NATIVE60_POS_PROBE(sub_826A39D8)
DOAX2_NATIVE60_POS_PROBE(sub_826A4040)
DOAX2_NATIVE60_POS_PROBE(sub_826A43D8)
DOAX2_NATIVE60_POS_PROBE(sub_826A4630)
DOAX2_NATIVE60_POS_PROBE(sub_826A7180)
DOAX2_NATIVE60_POS_PROBE(sub_826A9B60)
DOAX2_NATIVE60_POS_PROBE(sub_826AA728)
DOAX2_NATIVE60_POS_PROBE(sub_826AC4C0)
DOAX2_NATIVE60_POS_PROBE(sub_826AD028)
DOAX2_NATIVE60_POS_PROBE(sub_826AD190)
DOAX2_NATIVE60_POS_PROBE(sub_826ADC70)
DOAX2_NATIVE60_POS_PROBE(sub_826ADF30)
DOAX2_NATIVE60_POS_PROBE(sub_826ADFC8)
DOAX2_NATIVE60_POS_PROBE(sub_826AEC08)
DOAX2_NATIVE60_POS_PROBE(sub_826AF238)
DOAX2_NATIVE60_POS_PROBE(sub_826AF880)
DOAX2_NATIVE60_POS_PROBE(sub_826BC838)
DOAX2_NATIVE60_POS_PROBE(sub_826BCBC8)
DOAX2_NATIVE60_POS_PROBE(sub_826BCE00)
DOAX2_NATIVE60_POS_PROBE(sub_826BD040)
DOAX2_NATIVE60_POS_PROBE(sub_826CCDA8)
DOAX2_NATIVE60_POS_PROBE(sub_826CCEE8)
DOAX2_NATIVE60_POS_PROBE(sub_826CD090)
DOAX2_NATIVE60_POS_PROBE(sub_826CD3F0)
DOAX2_NATIVE60_POS_PROBE(sub_826CDEF0)
DOAX2_NATIVE60_POS_PROBE(sub_826D1D98)
DOAX2_NATIVE60_POS_PROBE(sub_826DAD48)
DOAX2_NATIVE60_POS_PROBE(sub_826DBFA0)
DOAX2_NATIVE60_POS_PROBE(sub_826DC458)
DOAX2_NATIVE60_POS_PROBE(sub_826E2C18)
DOAX2_NATIVE60_POS_PROBE(sub_826E5EC0)
DOAX2_NATIVE60_POS_PROBE(sub_826EF868)
DOAX2_NATIVE60_POS_PROBE(sub_826F0540)

DOAX2_NATIVE60_POS_PROBE(sub_82410000)
DOAX2_NATIVE60_POS_PROBE(sub_82410048)
DOAX2_NATIVE60_POS_PROBE(sub_82410098)
DOAX2_NATIVE60_POS_PROBE(sub_824101B0)
DOAX2_NATIVE60_POS_PROBE(sub_824102F8)
DOAX2_NATIVE60_POS_PROBE(sub_82410408)
DOAX2_NATIVE60_POS_PROBE(sub_82410490)
DOAX2_NATIVE60_POS_PROBE(sub_82410520)
DOAX2_NATIVE60_POS_PROBE(sub_824105D0)
DOAX2_NATIVE60_POS_PROBE(sub_82410698)
DOAX2_NATIVE60_POS_PROBE(sub_82410778)
DOAX2_NATIVE60_POS_PROBE(sub_82410800)
DOAX2_NATIVE60_POS_PROBE(sub_82410928)
DOAX2_NATIVE60_POS_PROBE(sub_82410990)
DOAX2_NATIVE60_POS_PROBE(sub_82410A18)
DOAX2_NATIVE60_POS_PROBE(sub_82410A90)
DOAX2_NATIVE60_POS_PROBE(sub_82410AF8)
DOAX2_NATIVE60_POS_PROBE(sub_82410B68)
DOAX2_NATIVE60_POS_PROBE(sub_82410BD0)
DOAX2_NATIVE60_POS_PROBE(sub_82410BE0)
DOAX2_NATIVE60_POS_PROBE(sub_82410C50)
DOAX2_NATIVE60_POS_PROBE(sub_82410D20)
DOAX2_NATIVE60_POS_PROBE(sub_82410D68)
DOAX2_NATIVE60_POS_PROBE(sub_82410DC8)
DOAX2_NATIVE60_POS_PROBE(sub_82410E48)
DOAX2_NATIVE60_POS_PROBE(sub_824110C0)
DOAX2_NATIVE60_POS_PROBE(sub_82411360)
DOAX2_NATIVE60_POS_PROBE(sub_82411438)
DOAX2_NATIVE60_POS_PROBE(sub_82411690)
DOAX2_NATIVE60_POS_PROBE(sub_82411760)
DOAX2_NATIVE60_POS_PROBE(sub_824118A0)
DOAX2_NATIVE60_POS_PROBE(sub_824119C0)
DOAX2_NATIVE60_POS_PROBE(sub_82411AD8)
DOAX2_NATIVE60_POS_PROBE(sub_82411C10)
DOAX2_NATIVE60_POS_PROBE(sub_824122C8)
DOAX2_NATIVE60_POS_PROBE(sub_824123A0)
DOAX2_NATIVE60_POS_PROBE(sub_82412600)
DOAX2_NATIVE60_POS_PROBE(sub_82412AC8)
DOAX2_NATIVE60_POS_PROBE(sub_82412BA8)
DOAX2_NATIVE60_POS_PROBE(sub_82412C60)
DOAX2_NATIVE60_POS_PROBE(sub_82412DB0)
DOAX2_NATIVE60_POS_PROBE(sub_82412EA0)
DOAX2_NATIVE60_POS_PROBE(sub_82413080)
DOAX2_NATIVE60_POS_PROBE(sub_824130D8)
DOAX2_NATIVE60_POS_PROBE(sub_824131C0)
DOAX2_NATIVE60_POS_PROBE(sub_82413318)
DOAX2_NATIVE60_POS_PROBE(sub_82413378)
DOAX2_NATIVE60_POS_PROBE(sub_824133C0)
DOAX2_NATIVE60_POS_PROBE(sub_82413410)
DOAX2_NATIVE60_POS_PROBE(sub_82413460)
DOAX2_NATIVE60_POS_PROBE(sub_82413500)
DOAX2_NATIVE60_POS_PROBE(sub_82413548)
DOAX2_NATIVE60_POS_PROBE(sub_82413628)
DOAX2_NATIVE60_POS_PROBE(sub_82413678)
DOAX2_NATIVE60_POS_PROBE(sub_824136D8)
DOAX2_NATIVE60_POS_PROBE(sub_82413738)
DOAX2_NATIVE60_POS_PROBE(sub_824137C0)
DOAX2_NATIVE60_POS_PROBE(sub_82413958)
DOAX2_NATIVE60_POS_PROBE(sub_82413A10)
DOAX2_NATIVE60_POS_PROBE(sub_82413AD0)
DOAX2_NATIVE60_POS_PROBE(sub_82413C18)
DOAX2_NATIVE60_POS_PROBE(sub_82413D88)
DOAX2_NATIVE60_POS_PROBE(sub_82413E28)
DOAX2_NATIVE60_POS_PROBE(sub_82413ED0)
DOAX2_NATIVE60_POS_PROBE(sub_82414018)
DOAX2_NATIVE60_POS_PROBE(sub_82414090)
DOAX2_NATIVE60_POS_PROBE(sub_82414100)
DOAX2_NATIVE60_POS_PROBE(sub_82414168)
DOAX2_NATIVE60_POS_PROBE(sub_824141C0)
DOAX2_NATIVE60_POS_PROBE(sub_824143F8)
DOAX2_NATIVE60_POS_PROBE(sub_82414538)
DOAX2_NATIVE60_POS_PROBE(sub_82414618)
DOAX2_NATIVE60_POS_PROBE(sub_82414850)
DOAX2_NATIVE60_POS_PROBE(sub_82414A78)
DOAX2_NATIVE60_POS_PROBE(sub_82414EF0)
DOAX2_NATIVE60_POS_PROBE(sub_82414FC8)
DOAX2_NATIVE60_POS_PROBE(sub_824150B0)
DOAX2_NATIVE60_POS_PROBE(sub_82415120)
DOAX2_NATIVE60_POS_PROBE(sub_824151F8)
DOAX2_NATIVE60_POS_PROBE(sub_82415250)
DOAX2_NATIVE60_POS_PROBE(sub_82415380)
DOAX2_NATIVE60_POS_PROBE(sub_824153D0)
DOAX2_NATIVE60_POS_PROBE(sub_82415478)
DOAX2_NATIVE60_POS_PROBE(sub_82415558)
DOAX2_NATIVE60_POS_PROBE(sub_82415640)
DOAX2_NATIVE60_POS_PROBE(sub_82415738)
DOAX2_NATIVE60_POS_PROBE(sub_824157C8)
DOAX2_NATIVE60_POS_PROBE(sub_82415888)
DOAX2_NATIVE60_POS_PROBE(sub_82415930)
DOAX2_NATIVE60_POS_PROBE(sub_82415B20)
DOAX2_NATIVE60_POS_PROBE(sub_82415B78)
DOAX2_NATIVE60_POS_PROBE(sub_82415C48)
DOAX2_NATIVE60_POS_PROBE(sub_82415CE0)
DOAX2_NATIVE60_POS_PROBE(sub_82415DA8)
DOAX2_NATIVE60_POS_PROBE(sub_82415E18)
DOAX2_NATIVE60_POS_PROBE(sub_82415E88)
DOAX2_NATIVE60_POS_PROBE(sub_82416DD8)
DOAX2_NATIVE60_POS_PROBE(sub_82416FD8)
DOAX2_NATIVE60_POS_PROBE(sub_82417080)
DOAX2_NATIVE60_POS_PROBE(sub_82417150)
DOAX2_NATIVE60_POS_PROBE(sub_82417220)
DOAX2_NATIVE60_POS_PROBE(sub_82417270)
DOAX2_NATIVE60_POS_PROBE(sub_82417358)
DOAX2_NATIVE60_POS_PROBE(sub_824173E0)
DOAX2_NATIVE60_POS_PROBE(sub_82417528)
DOAX2_NATIVE60_POS_PROBE(sub_82417638)
DOAX2_NATIVE60_POS_PROBE(sub_824176B8)
DOAX2_NATIVE60_POS_PROBE(sub_82417748)
DOAX2_NATIVE60_POS_PROBE(sub_82417988)
DOAX2_NATIVE60_POS_PROBE(sub_82417A58)
DOAX2_NATIVE60_POS_PROBE(sub_82417B28)
DOAX2_NATIVE60_POS_PROBE(sub_82417BF8)
DOAX2_NATIVE60_POS_PROBE(sub_82417D68)
DOAX2_NATIVE60_POS_PROBE(sub_82417E28)
DOAX2_NATIVE60_POS_PROBE(sub_824180E8)
DOAX2_NATIVE60_POS_PROBE(sub_82418C88)
DOAX2_NATIVE60_POS_PROBE(sub_82418D10)
DOAX2_NATIVE60_POS_PROBE(sub_82418DC0)
DOAX2_NATIVE60_POS_PROBE(sub_82418F70)
DOAX2_NATIVE60_POS_PROBE(sub_82419088)
DOAX2_NATIVE60_POS_PROBE(sub_824190F8)
DOAX2_NATIVE60_POS_PROBE(sub_82419218)
DOAX2_NATIVE60_POS_PROBE(sub_824195C8)
DOAX2_NATIVE60_POS_PROBE(sub_824199B0)
DOAX2_NATIVE60_POS_PROBE(sub_82419DF0)
DOAX2_NATIVE60_POS_PROBE(sub_82419F68)
DOAX2_NATIVE60_POS_PROBE(sub_8241A338)
DOAX2_NATIVE60_POS_PROBE(sub_8241A3A0)
DOAX2_NATIVE60_POS_PROBE(sub_8241A770)
DOAX2_NATIVE60_POS_PROBE(sub_8241ABD8)
DOAX2_NATIVE60_POS_PROBE(sub_8241B080)
DOAX2_NATIVE60_POS_PROBE(sub_8241D4A8)
DOAX2_NATIVE60_POS_PROBE(sub_8241D510)
DOAX2_NATIVE60_POS_PROBE(sub_8241E788)
DOAX2_NATIVE60_POS_PROBE(sub_8241E808)
DOAX2_NATIVE60_POS_PROBE(sub_8241EA40)
DOAX2_NATIVE60_POS_PROBE(sub_8241EDA0)
DOAX2_NATIVE60_POS_PROBE(sub_8241F070)
DOAX2_NATIVE60_POS_PROBE(sub_8241F098)
DOAX2_NATIVE60_POS_PROBE(sub_8241F0C0)
DOAX2_NATIVE60_POS_PROBE(sub_8241F0E8)
DOAX2_NATIVE60_POS_PROBE(sub_8241F308)
DOAX2_NATIVE60_POS_PROBE(sub_8241F3E0)
DOAX2_NATIVE60_POS_PROBE(sub_8241F4B8)
DOAX2_NATIVE60_POS_PROBE(sub_8241F5A8)
DOAX2_NATIVE60_POS_PROBE(sub_8241F668)
DOAX2_NATIVE60_POS_PROBE(sub_8241F888)
DOAX2_NATIVE60_POS_PROBE(sub_8241FBB0)
DOAX2_NATIVE60_POS_PROBE(sub_8241FFB0)
DOAX2_NATIVE60_POS_PROBE(sub_82420160)
DOAX2_NATIVE60_POS_PROBE(sub_824201B0)
DOAX2_NATIVE60_POS_PROBE(sub_82420200)
DOAX2_NATIVE60_POS_PROBE(sub_82420238)
DOAX2_NATIVE60_POS_PROBE(sub_82420308)
DOAX2_NATIVE60_POS_PROBE(sub_82420408)
DOAX2_NATIVE60_POS_PROBE(sub_82420610)
DOAX2_NATIVE60_POS_PROBE(sub_82420660)
DOAX2_NATIVE60_POS_PROBE(sub_824207B8)
DOAX2_NATIVE60_POS_PROBE(sub_82420A48)
DOAX2_NATIVE60_POS_PROBE(sub_82420AB0)
DOAX2_NATIVE60_POS_PROBE(sub_82420B10)
DOAX2_NATIVE60_POS_PROBE(sub_82420B38)
DOAX2_NATIVE60_POS_PROBE(sub_82420B68)
DOAX2_NATIVE60_POS_PROBE(sub_82420BF0)
DOAX2_NATIVE60_POS_PROBE(sub_82420C20)
DOAX2_NATIVE60_POS_PROBE(sub_82422878)
DOAX2_NATIVE60_POS_PROBE(sub_824228E0)
DOAX2_NATIVE60_POS_PROBE(sub_824229A8)
DOAX2_NATIVE60_POS_PROBE(sub_82422AB8)
DOAX2_NATIVE60_POS_PROBE(sub_82422C10)
DOAX2_NATIVE60_POS_PROBE(sub_82422F98)
DOAX2_NATIVE60_POS_PROBE(sub_824233E8)
DOAX2_NATIVE60_POS_PROBE(sub_82423A00)
DOAX2_NATIVE60_POS_PROBE(sub_82423AA0)
DOAX2_NATIVE60_POS_PROBE(sub_82423AC0)
DOAX2_NATIVE60_POS_PROBE(sub_82424070)
DOAX2_NATIVE60_POS_PROBE(sub_824240B0)
DOAX2_NATIVE60_POS_PROBE(sub_824240F8)
DOAX2_NATIVE60_POS_PROBE(sub_82424188)
DOAX2_NATIVE60_POS_PROBE(sub_82424228)
DOAX2_NATIVE60_POS_PROBE(sub_82424320)
DOAX2_NATIVE60_POS_PROBE(sub_82424340)
DOAX2_NATIVE60_POS_PROBE(sub_82424400)
DOAX2_NATIVE60_POS_PROBE(sub_824244D0)
DOAX2_NATIVE60_POS_PROBE(sub_82426F70)
DOAX2_NATIVE60_POS_PROBE(sub_82427318)
DOAX2_NATIVE60_POS_PROBE(sub_82427430)
DOAX2_NATIVE60_POS_PROBE(sub_82427D88)
DOAX2_NATIVE60_POS_PROBE(sub_82427E28)
DOAX2_NATIVE60_POS_PROBE(sub_824297D8)
DOAX2_NATIVE60_POS_PROBE(sub_82429AD8)
DOAX2_NATIVE60_POS_PROBE(sub_82429B48)
DOAX2_NATIVE60_POS_PROBE(sub_82429BA0)
DOAX2_NATIVE60_POS_PROBE(sub_82429BF8)
DOAX2_NATIVE60_POS_PROBE(sub_82429C50)
DOAX2_NATIVE60_POS_PROBE(sub_8242A518)
DOAX2_NATIVE60_POS_PROBE(sub_8242A608)
DOAX2_NATIVE60_POS_PROBE(sub_8242A680)
DOAX2_NATIVE60_POS_PROBE(sub_8242AA08)
DOAX2_NATIVE60_POS_PROBE(sub_8242ABC0)
DOAX2_NATIVE60_POS_PROBE(sub_8242B920)
DOAX2_NATIVE60_POS_PROBE(sub_8242C0A0)
DOAX2_NATIVE60_POS_PROBE(sub_8242C458)
DOAX2_NATIVE60_POS_PROBE(sub_8242C520)
DOAX2_NATIVE60_POS_PROBE(sub_8242C718)
DOAX2_NATIVE60_POS_PROBE(sub_8242C8F0)
DOAX2_NATIVE60_POS_PROBE(sub_8242C9F0)
DOAX2_NATIVE60_POS_PROBE(sub_8242CAB0)
DOAX2_NATIVE60_POS_PROBE(sub_8242CBD0)
DOAX2_NATIVE60_POS_PROBE(sub_8242CEF0)
DOAX2_NATIVE60_POS_PROBE(sub_8242E050)
DOAX2_NATIVE60_POS_PROBE(sub_8242E1C8)
DOAX2_NATIVE60_POS_PROBE(sub_8242E268)
DOAX2_NATIVE60_POS_PROBE(sub_8242E3F8)
DOAX2_NATIVE60_POS_PROBE(sub_8242F1E0)
DOAX2_NATIVE60_POS_PROBE(sub_8242F250)
DOAX2_NATIVE60_POS_PROBE(sub_8242F530)
DOAX2_NATIVE60_POS_PROBE(sub_824300C0)
DOAX2_NATIVE60_POS_PROBE(sub_82430208)
DOAX2_NATIVE60_POS_PROBE(sub_82432798)
DOAX2_NATIVE60_POS_PROBE(sub_82432818)
DOAX2_NATIVE60_POS_PROBE(sub_824328B8)
DOAX2_NATIVE60_POS_PROBE(sub_82432A58)
DOAX2_NATIVE60_POS_PROBE(sub_82432C58)
DOAX2_NATIVE60_POS_PROBE(sub_82432D80)
DOAX2_NATIVE60_POS_PROBE(sub_82432E90)
DOAX2_NATIVE60_POS_PROBE(sub_82433068)
DOAX2_NATIVE60_POS_PROBE(sub_82433138)
DOAX2_NATIVE60_POS_PROBE(sub_824332E8)
DOAX2_NATIVE60_POS_PROBE(sub_82433598)
DOAX2_NATIVE60_POS_PROBE(sub_82433740)
DOAX2_NATIVE60_POS_PROBE(sub_82433C08)
DOAX2_NATIVE60_POS_PROBE(sub_82433CD0)
DOAX2_NATIVE60_POS_PROBE(sub_82434838)
DOAX2_NATIVE60_POS_PROBE(sub_82434900)
DOAX2_NATIVE60_POS_PROBE(sub_82434C38)
DOAX2_NATIVE60_POS_PROBE(sub_82434EA0)
DOAX2_NATIVE60_POS_PROBE(sub_82434FF8)
DOAX2_NATIVE60_POS_PROBE(sub_82435280)
DOAX2_NATIVE60_POS_PROBE(sub_82435B40)
DOAX2_NATIVE60_POS_PROBE(sub_82435DB8)
DOAX2_NATIVE60_POS_PROBE(sub_82435F08)
DOAX2_NATIVE60_POS_PROBE(sub_824360B8)
DOAX2_NATIVE60_POS_PROBE(sub_82436408)
DOAX2_NATIVE60_POS_PROBE(sub_82436498)
DOAX2_NATIVE60_POS_PROBE(sub_82436598)
DOAX2_NATIVE60_POS_PROBE(sub_82436630)
DOAX2_NATIVE60_POS_PROBE(sub_824368A0)
DOAX2_NATIVE60_POS_PROBE(sub_82436968)
DOAX2_NATIVE60_POS_PROBE(sub_82436AB8)
DOAX2_NATIVE60_POS_PROBE(sub_82436BA0)
DOAX2_NATIVE60_POS_PROBE(sub_82436C80)
DOAX2_NATIVE60_POS_PROBE(sub_82436D50)
DOAX2_NATIVE60_POS_PROBE(sub_82437028)
DOAX2_NATIVE60_POS_PROBE(sub_82437118)
DOAX2_NATIVE60_POS_PROBE(sub_82437250)
DOAX2_NATIVE60_POS_PROBE(sub_82437308)
DOAX2_NATIVE60_POS_PROBE(sub_82437390)
DOAX2_NATIVE60_POS_PROBE(sub_82437498)
DOAX2_NATIVE60_POS_PROBE(sub_82437C60)
DOAX2_NATIVE60_POS_PROBE(sub_82437DE8)
DOAX2_NATIVE60_POS_PROBE(sub_82437E98)
DOAX2_NATIVE60_POS_PROBE(sub_82437EF0)
DOAX2_NATIVE60_POS_PROBE(sub_82437FC0)
DOAX2_NATIVE60_POS_PROBE(sub_82438140)
DOAX2_NATIVE60_POS_PROBE(sub_82438460)
DOAX2_NATIVE60_POS_PROBE(sub_82438580)
DOAX2_NATIVE60_POS_PROBE(sub_82438648)
DOAX2_NATIVE60_POS_PROBE(sub_824386F0)
DOAX2_NATIVE60_POS_PROBE(sub_82438758)
DOAX2_NATIVE60_POS_PROBE(sub_82439000)
DOAX2_NATIVE60_POS_PROBE(sub_8243B538)
DOAX2_NATIVE60_POS_PROBE(sub_8243C408)
DOAX2_NATIVE60_POS_PROBE(sub_8243C878)
DOAX2_NATIVE60_POS_PROBE(sub_8243CB18)
DOAX2_NATIVE60_POS_PROBE(sub_8243D378)
DOAX2_NATIVE60_POS_PROBE(sub_8243DCF8)
DOAX2_NATIVE60_POS_PROBE(sub_8243DF08)
DOAX2_NATIVE60_POS_PROBE(sub_8243E0B8)
DOAX2_NATIVE60_POS_PROBE(sub_8243E9D8)
DOAX2_NATIVE60_POS_PROBE(sub_8243F1D0)
DOAX2_NATIVE60_POS_PROBE(sub_8243F3C0)
DOAX2_NATIVE60_POS_PROBE(sub_8243F550)
DOAX2_NATIVE60_POS_PROBE(sub_8243F680)
DOAX2_NATIVE60_POS_PROBE(sub_8243F988)
DOAX2_NATIVE60_POS_PROBE(sub_8243FA18)
DOAX2_NATIVE60_POS_PROBE(sub_8243FC98)
DOAX2_NATIVE60_POS_PROBE(sub_8243FEC0)

DOAX2_NATIVE60_POS_PROBE(sub_826984E0)
DOAX2_NATIVE60_POS_PROBE(sub_82698670)
DOAX2_NATIVE60_POS_PROBE(sub_826987A0)
DOAX2_NATIVE60_POS_PROBE(sub_82698838)
DOAX2_NATIVE60_POS_PROBE(sub_82698900)
DOAX2_NATIVE60_POS_PROBE(sub_82699008)
DOAX2_NATIVE60_POS_PROBE(sub_826992D0)
DOAX2_NATIVE60_POS_PROBE(sub_826994C8)
DOAX2_NATIVE60_POS_PROBE(sub_82699514)
DOAX2_NATIVE60_POS_PROBE(sub_82699518)
DOAX2_NATIVE60_POS_PROBE(sub_82699578)
DOAX2_NATIVE60_POS_PROBE(sub_82699698)
DOAX2_NATIVE60_POS_PROBE(sub_82699980)
DOAX2_NATIVE60_POS_PROBE(sub_826999F0)
DOAX2_NATIVE60_POS_PROBE(sub_82699A58)
DOAX2_NATIVE60_POS_PROBE(sub_82699C28)
DOAX2_NATIVE60_POS_PROBE(sub_82699CC0)
DOAX2_NATIVE60_POS_PROBE(sub_82699EF0)
DOAX2_NATIVE60_POS_PROBE(sub_82699F60)
DOAX2_NATIVE60_POS_PROBE(sub_8269A338)
DOAX2_NATIVE60_POS_PROBE(sub_8269A520)
DOAX2_NATIVE60_POS_PROBE(sub_8269A738)
DOAX2_NATIVE60_POS_PROBE(sub_8269A748)
DOAX2_NATIVE60_POS_PROBE(sub_8269AA00)
DOAX2_NATIVE60_POS_PROBE(sub_8269AC80)
DOAX2_NATIVE60_POS_PROBE(sub_8269ACC0)
DOAX2_NATIVE60_POS_PROBE(sub_8269AE4C)
DOAX2_NATIVE60_POS_PROBE(sub_8269AE50)
DOAX2_NATIVE60_POS_PROBE(sub_8269AEC8)
DOAX2_NATIVE60_POS_PROBE(sub_8269B268)
DOAX2_NATIVE60_POS_PROBE(sub_8269B7A0)
DOAX2_NATIVE60_POS_PROBE(sub_8269B7B0)
DOAX2_NATIVE60_POS_PROBE(sub_8269B988)
DOAX2_NATIVE60_POS_PROBE(sub_8269B9A8)
DOAX2_NATIVE60_POS_PROBE(sub_8269B9C8)
DOAX2_NATIVE60_POS_PROBE(sub_8269BDD0)
DOAX2_NATIVE60_POS_PROBE(sub_8269C220)
DOAX2_NATIVE60_POS_PROBE(sub_8269CFC0)
DOAX2_NATIVE60_POS_PROBE(sub_8269D028)
DOAX2_NATIVE60_POS_PROBE(sub_8269D118)
DOAX2_NATIVE60_POS_PROBE(sub_8269D430)
DOAX2_NATIVE60_POS_PROBE(sub_8269D550)
DOAX2_NATIVE60_POS_PROBE(sub_8269DA70)
DOAX2_NATIVE60_POS_PROBE(sub_8269DE38)
DOAX2_NATIVE60_POS_PROBE(sub_8269E7F0)
DOAX2_NATIVE60_POS_PROBE(sub_8269F0A8)
DOAX2_NATIVE60_POS_PROBE(sub_8269F3A8)
DOAX2_NATIVE60_POS_PROBE(sub_8269F488)
DOAX2_NATIVE60_POS_PROBE(sub_8269FB38)
DOAX2_NATIVE60_POS_PROBE(sub_826A0450)
DOAX2_NATIVE60_POS_PROBE(sub_826A0AD0)
DOAX2_NATIVE60_POS_PROBE(sub_826A0C48)
DOAX2_NATIVE60_POS_PROBE(sub_826A13E8)
DOAX2_NATIVE60_POS_PROBE(sub_826A14A8)
DOAX2_NATIVE60_POS_PROBE(sub_826A1BF8)
DOAX2_NATIVE60_POS_PROBE(sub_826A1CC8)
DOAX2_NATIVE60_POS_PROBE(sub_826A1E98)
DOAX2_NATIVE60_POS_PROBE(sub_826A2220)
DOAX2_NATIVE60_POS_PROBE(sub_826A2340)
DOAX2_NATIVE60_POS_PROBE(sub_826A2478)
DOAX2_NATIVE60_POS_PROBE(sub_826A2580)
DOAX2_NATIVE60_POS_PROBE(sub_826A2610)
DOAX2_NATIVE60_POS_PROBE(sub_826A2818)
DOAX2_NATIVE60_POS_PROBE(sub_826A2B68)
DOAX2_NATIVE60_POS_PROBE(sub_826A2EF0)
DOAX2_NATIVE60_POS_PROBE(sub_826A3238)
DOAX2_NATIVE60_POS_PROBE(sub_826A32C8)
DOAX2_NATIVE60_POS_PROBE(sub_826A33B8)
// Pool Slide transform helpers (census: 826A34C8 runs ~10x per update after
// the first landing at 60 vs ~0.1 at 30). Diagnostics: per-caller counts
// (lr and the caller's own return address) during the slide session.
inline void Native60SlideXformLog(const char* name, const PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan) || !Native60SlideActive()) return;
  const uint32_t lr = uint32_t(ctx.lr);
  const uint32_t back = REX_LOAD_U32(ctx.r1.u32);
  const uint32_t lr2 = back >= 0x40000000 ? REX_LOAD_U32(back - 8) : 0;
  static std::mutex mutex;
  static std::map<std::pair<uint32_t, uint32_t>, uint64_t> counts;
  static uint64_t total = 0, lines = 0;
  std::lock_guard lock(mutex);
  const uint64_t n = ++counts[{lr, lr2}];
  ++total;
  if ((n <= 3 || (n & (n - 1)) == 0) && lines++ < 400)
    REXLOG_INFO("DOAX2 native60 slide xform {}: frame={} sim={} lr={:08X} lr2={:08X} n={} total={} f1={:.4g} r3={:X} r4={:X}",
                name, Doax2EngineFrameCount(), native60_slide_sim_frame == Doax2EngineFrameCount() ? 1 : 0,
                lr, lr2, n, total, ctx.f1.f64, ctx.r3.u32, ctx.r4.u32);
}
// Audio runtime parameter setters (diagnostics, Pool Slide robotic water
// sound): 82715158(obj, list{u8 count, (u8 index, f32 value)*}, immediate)
// sets variable targets (immediate also snaps the current value); 82724E68
// (obj, list{u8 count, (u8 index, u32 value)*}) forwards to sub-objects.
// Logged in windows of 90 updates every 600 updates of the slide session.
inline uint64_t native60_slide_audio_origin = 0;
inline void Native60SlideAudioParamLog(const char* name, const PPCContext& ctx, uint8_t* base, bool floats) {
  if (!REXCVAR_GET(doax2_native60_counter_scan) || !Native60SlideActive()) return;
  const uint64_t frame = Doax2EngineFrameCount();
  if (native60_slide_audio_origin == 0 || frame - native60_slide_audio_origin > 100000)
    native60_slide_audio_origin = frame;
  if ((frame - native60_slide_audio_origin) % 600 >= 90) return;
  static std::atomic<uint32_t> lines{0};
  if (lines.fetch_add(1) >= 20000) return;
  const uint32_t list = ctx.r4.u32;
  const uint32_t count = list >= 0x40000000 ? REX_LOAD_U8(list) : 0;
  const uint32_t items = count ? REX_LOAD_U32(list + 4) : 0;
  std::string values;
  for (uint32_t i = 0; i < count && i < 8 && items >= 0x40000000; ++i) {
    const uint32_t index = REX_LOAD_U8(items + i * 8);
    const uint32_t raw = REX_LOAD_U32(items + i * 8 + 4);
    if (floats)
      values += fmt::format(" {}={:.5g}", index, std::bit_cast<float>(raw));
    else
      values += fmt::format(" {}={:08X}", index, raw);
  }
  REXLOG_INFO("DOAX2 native60 slide audio {}: frame={} sim={} lr={:08X} obj={:08X} imm={} n={}{}",
              name, frame, native60_slide_sim_frame == frame ? 1 : 0, uint32_t(ctx.lr), ctx.r3.u32,
              floats ? ctx.r5.u32 & 0xFF : 0, count, values);
}
REX_EXTERN(__imp__sub_82715158);
REX_EXTERN(__imp__sub_82724E68);
REX_HOOK_RAW(sub_82715158) {
  Native60SlideAudioParamLog("vars", ctx, base, true);
  __imp__sub_82715158(ctx, base);
}
REX_HOOK_RAW(sub_82724E68) {
  Native60SlideAudioParamLog("fwd", ctx, base, false);
  __imp__sub_82724E68(ctx, base);
}
// Audio engine work (8273ACC0, once per update: 3D parameter update
// 8274A980 plus bank/stream/voice housekeeping). At 60 updates per second it
// ran twice as often as the original; Pool Slide's water voice turned robotic
// only at the doubled update rate (normal at 30 updates/s even in slow
// motion). Test: run it on alternate updates (30 per second) at tick 0.5.
// F6 toggles the pacing live (A/B).
REX_EXTERN(__imp__sub_8273ACC0);
REX_HOOK_RAW(sub_8273ACC0) {
  static constexpr bool pacing_on = false;
  static uint64_t last_frame = ~0ull;
  static bool ran_last = false;
  const uint64_t frame = Doax2EngineFrameCount();
  if (pacing_on && float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f && frame != last_frame) {
    // Alternate by call (not frame parity) so a skipped engine frame never
    // stalls the audio work for two updates.
    last_frame = frame;
    if (ran_last) {
      ran_last = false;
      return;
    }
    ran_last = true;
  }
  static std::atomic<uint64_t> calls{0};
  const uint64_t n = ++calls;
  if (n <= 2 || n % 3600 == 0)
    REXLOG_INFO("DOAX2 native60 audio work: calls={} frame={} pacing={}", n, frame, pacing_on ? 1 : 0);
  __imp__sub_8273ACC0(ctx, base);
}
// Audio capture (diagnostics, Pool Slide robotic water sound): every mixed
// frame the title hands to XAudioSubmitRenderDriverFrame (827147F8; 6 channels
// x 256 samples, planar big-endian float, 48 kHz) while a slide session is
// active goes to logs/slide_audio_<n>.wav (interleaved little-endian float)
// plus logs/slide_audio_<n>.frames (u64 engine frame + u8 sim flag per audio
// frame) to line the audio up with the paced slide updates.
namespace native60_audio_capture {
inline std::mutex mutex;
inline FILE* wav = nullptr;
inline FILE* frames = nullptr;
inline uint32_t data_bytes = 0;
inline uint32_t inactive = 0;
inline uint32_t session = 0;
inline void WriteHeader(FILE* f, uint32_t bytes) {
  const uint16_t channels = 6, bits = 32, format = 3;
  const uint32_t rate = 48000, block = channels * bits / 8, byte_rate = rate * block;
  const uint32_t riff = 36 + bytes, fmt_size = 16;
  std::fseek(f, 0, SEEK_SET);
  std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
  std::fwrite(&fmt_size, 4, 1, f); std::fwrite(&format, 2, 1, f); std::fwrite(&channels, 2, 1, f);
  std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f);
  const uint16_t block16 = uint16_t(block);
  std::fwrite(&block16, 2, 1, f); std::fwrite(&bits, 2, 1, f);
  std::fwrite("data", 1, 4, f); std::fwrite(&bytes, 4, 1, f);
  std::fseek(f, 0, SEEK_END);
}
inline void Close() {
  if (!wav) return;
  WriteHeader(wav, data_bytes);
  std::fclose(wav);
  std::fclose(frames);
  REXLOG_INFO("DOAX2 native60 slide audio capture {} closed: {:.1f} s", session,
              double(data_bytes) / (48000.0 * 24.0));
  wav = frames = nullptr;
}
inline void Capture(uint8_t* base, uint32_t samples) {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  const bool active = Native60SlideActive();
  std::lock_guard lock(mutex);
  if (!active) {
    if (wav && ++inactive > 400) Close();
    return;
  }
  inactive = 0;
  if (!wav) {
    if (session >= 8) return;
    ++session;
    std::filesystem::create_directories("logs");
    const std::string stem = fmt::format("logs/slide_audio_{}", session);
    wav = std::fopen((stem + ".wav").c_str(), "wb");
    frames = std::fopen((stem + ".frames").c_str(), "wb");
    if (!wav || !frames) {
      if (wav) std::fclose(wav);
      if (frames) std::fclose(frames);
      wav = frames = nullptr;
      return;
    }
    data_bytes = 0;
    WriteHeader(wav, 0);
    REXLOG_INFO("DOAX2 native60 slide audio capture {} opened at engine_frame={}", session,
                Doax2EngineFrameCount());
  }
  if (samples < 0x40000000) return;
  float out[256 * 6];
  for (uint32_t i = 0; i < 256; ++i)
    for (uint32_t c = 0; c < 6; ++c)
      out[i * 6 + c] = std::bit_cast<float>(REX_LOAD_U32(samples + (c * 256 + i) * 4));
  std::fwrite(out, sizeof(out), 1, wav);
  data_bytes += sizeof(out);
  const uint64_t frame = Doax2EngineFrameCount();
  const uint8_t sim = native60_slide_sim_frame == frame ? 1 : 0;
  std::fwrite(&frame, 8, 1, frames);
  std::fwrite(&sim, 1, 1, frames);
}
}  // namespace native60_audio_capture
REX_EXTERN(__imp__sub_827147F8);
REX_HOOK_RAW(sub_827147F8) {
  // r4 = buffer context; the samples pointer is at [r4 - 8 + 28] (see 827147F8).
  if (ctx.r4.u32)
    native60_audio_capture::Capture(base, REX_LOAD_U32(ctx.r4.u32 - 8 + 28));
  __imp__sub_827147F8(ctx, base);
}
// Rare audio engine / XAudio2 calls (diagnostics, Pool Slide): the doubled
// water loop (16.00 ms echo in the captured mix, onset at the first landing)
// means the loop is started twice on consecutive updates. Every call of the
// audio functions the census sees < 0.7x per update is logged during the
// slide session with update number, sim flag, thread, caller chain and args.
inline void Native60RareAudioLog(const char* name, const PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(doax2_native60_counter_scan) || !Native60SlideActive()) return;
  static std::atomic<uint32_t> lines{0};
  if (lines.fetch_add(1) >= 30000) return;
  const uint32_t back = REX_LOAD_U32(ctx.r1.u32);
  const uint32_t lr2 = back >= 0x40000000 ? REX_LOAD_U32(back - 8) : 0;
  const uint32_t back2 = back >= 0x40000000 ? REX_LOAD_U32(back) : 0;
  const uint32_t lr3 = back2 >= 0x40000000 ? REX_LOAD_U32(back2 - 8) : 0;
  const uint64_t frame = Doax2EngineFrameCount();
  REXLOG_INFO("DOAX2 native60 rare audio {}: frame={} sim={} tid={} lr={:08X} lr2={:08X} lr3={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} f1={:.4g}",
              name, frame, native60_slide_sim_frame == frame ? 1 : 0, GetCurrentThreadId(), uint32_t(ctx.lr),
              lr2, lr3, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.f1.f64);
}
#define DOAX2_NATIVE60_RARE_AUDIO(name) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    Native60RareAudioLog(#name, ctx, base); \
    __imp__##name(ctx, base); \
  }
DOAX2_NATIVE60_RARE_AUDIO(sub_82713BF8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82713F30)
DOAX2_NATIVE60_RARE_AUDIO(sub_82714068)
DOAX2_NATIVE60_RARE_AUDIO(sub_827142F0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82714350)
DOAX2_NATIVE60_RARE_AUDIO(sub_827143B0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82714478)
DOAX2_NATIVE60_RARE_AUDIO(sub_82714928)
DOAX2_NATIVE60_RARE_AUDIO(sub_82714A48)
DOAX2_NATIVE60_RARE_AUDIO(sub_827157B8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82715F00)
DOAX2_NATIVE60_RARE_AUDIO(sub_82715F60)
DOAX2_NATIVE60_RARE_AUDIO(sub_827160E8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82716160)
DOAX2_NATIVE60_RARE_AUDIO(sub_827163A8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82716438)
DOAX2_NATIVE60_RARE_AUDIO(sub_827164B8)
DOAX2_NATIVE60_RARE_AUDIO(sub_827165C8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82716790)
DOAX2_NATIVE60_RARE_AUDIO(sub_827181F8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718668)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718688)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718790)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718888)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718980)
DOAX2_NATIVE60_RARE_AUDIO(sub_827189B0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718AD0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718C50)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718E70)
DOAX2_NATIVE60_RARE_AUDIO(sub_82718F20)
DOAX2_NATIVE60_RARE_AUDIO(sub_827191A0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82719460)
DOAX2_NATIVE60_RARE_AUDIO(sub_82719530)
DOAX2_NATIVE60_RARE_AUDIO(sub_82719598)
DOAX2_NATIVE60_RARE_AUDIO(sub_827197D0)
DOAX2_NATIVE60_RARE_AUDIO(sub_827198A0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82719918)
DOAX2_NATIVE60_RARE_AUDIO(sub_82719FB0)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271A088)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271A090)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271A1D8)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271A2E0)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271A310)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271A6C0)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271E660)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271EA18)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271EC88)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271EFE8)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F090)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F378)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F588)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F628)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F7E8)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F818)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F8E0)
DOAX2_NATIVE60_RARE_AUDIO(sub_8271F9D8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82720830)
DOAX2_NATIVE60_RARE_AUDIO(sub_82720AB0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82720B50)
DOAX2_NATIVE60_RARE_AUDIO(sub_82721BF8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82721DB0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82721FD8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82722018)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723728)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723818)
DOAX2_NATIVE60_RARE_AUDIO(sub_827238E8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723AB0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723B00)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723B68)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723B80)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723C58)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723D50)
DOAX2_NATIVE60_RARE_AUDIO(sub_82723E28)
DOAX2_NATIVE60_RARE_AUDIO(sub_827249A0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82724AC8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82724B68)
DOAX2_NATIVE60_RARE_AUDIO(sub_82725148)
DOAX2_NATIVE60_RARE_AUDIO(sub_827274E0)
DOAX2_NATIVE60_RARE_AUDIO(sub_8273A9F8)
DOAX2_NATIVE60_RARE_AUDIO(sub_8273B178)
DOAX2_NATIVE60_RARE_AUDIO(sub_8273B1E0)
DOAX2_NATIVE60_RARE_AUDIO(sub_8273B280)
DOAX2_NATIVE60_RARE_AUDIO(sub_827473A0)
DOAX2_NATIVE60_RARE_AUDIO(sub_8274A310)
DOAX2_NATIVE60_RARE_AUDIO(sub_8274A3B8)
DOAX2_NATIVE60_RARE_AUDIO(sub_8274EB58)
DOAX2_NATIVE60_RARE_AUDIO(sub_82751120)
DOAX2_NATIVE60_RARE_AUDIO(sub_827511F0)
DOAX2_NATIVE60_RARE_AUDIO(sub_827512E8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82751828)
DOAX2_NATIVE60_RARE_AUDIO(sub_82752138)
DOAX2_NATIVE60_RARE_AUDIO(sub_827524D8)
DOAX2_NATIVE60_RARE_AUDIO(sub_827527A8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82752D10)
DOAX2_NATIVE60_RARE_AUDIO(sub_82755190)
DOAX2_NATIVE60_RARE_AUDIO(sub_827551D8)
DOAX2_NATIVE60_RARE_AUDIO(sub_82756958)
DOAX2_NATIVE60_RARE_AUDIO(sub_827569E0)
DOAX2_NATIVE60_RARE_AUDIO(sub_82756A48)
DOAX2_NATIVE60_RARE_AUDIO(sub_82756D88)
// Sound emitter play (82668CC8(emitter); sound = [emitter+176] +4/+8, the
// voice handle goes to emitter+52). Pool Slide (log 427, audio capture): at
// the first landing the same water loop was started on two consecutive
// updates (one slide-sim update and the in-between one), and both copies
// kept looping 16 ms apart (comb filter: the robotic water). A second start
// of the same sound on the same emitter one update after the first can only
// come from the extra 60 Hz update: skip it and keep the first voice.
// F6 toggles the dedupe live (A/B).
REX_EXTERN(__imp__sub_82668CC8);
REX_HOOK_RAW(sub_82668CC8) {
  static constexpr bool dedupe_on = false;
  const uint32_t emitter = ctx.r3.u32;
  const uint32_t info = emitter >= 0x40000000 ? REX_LOAD_U32(emitter + 176) : 0;
  const uint32_t bank = info >= 0x40000000 ? REX_LOAD_U32(info + 4) : 0;
  const uint32_t cue = info >= 0x40000000 ? REX_LOAD_U32(info + 8) : 0;
  const uint64_t frame = Doax2EngineFrameCount();
  struct Last { uint32_t emitter, bank, cue; uint64_t frame; };
  static std::mutex mutex;
  static std::array<Last, 32> recent{};
  static uint32_t next = 0;
  bool repeat = false;
  {
    std::lock_guard lock(mutex);
    for (const Last& l : recent)
      if (l.emitter == emitter && l.bank == bank && l.cue == cue && l.frame + 1 == frame) repeat = true;
    if (!repeat) {
      bool updated = false;
      for (Last& l : recent)
        if (l.emitter == emitter) { l = {emitter, bank, cue, frame}; updated = true; break; }
      if (!updated) recent[next++ % recent.size()] = {emitter, bank, cue, frame};
    }
  }
  const bool half = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f;
  const bool skip = repeat && half && dedupe_on;
  if (REXCVAR_GET(doax2_native60_counter_scan) && (Native60SlideActive() || repeat)) {
    static std::atomic<uint32_t> lines{0};
    if (lines.fetch_add(1) < 4000) {
      const uint32_t back = REX_LOAD_U32(ctx.r1.u32);
      const uint32_t lr2 = back >= 0x40000000 ? REX_LOAD_U32(back - 8) : 0;
      const uint32_t back2 = back >= 0x40000000 ? REX_LOAD_U32(back) : 0;
      const uint32_t lr3 = back2 >= 0x40000000 ? REX_LOAD_U32(back2 - 8) : 0;
      const uint32_t back3 = back2 >= 0x40000000 ? REX_LOAD_U32(back2) : 0;
      const uint32_t lr4 = back3 >= 0x40000000 ? REX_LOAD_U32(back3 - 8) : 0;
      REXLOG_INFO("DOAX2 native60 emitter play: frame={} sim={} emitter={:08X} sound={:X}/{:X} handle={:08X} repeat={} skipped={} lr={:08X} lr2={:08X} lr3={:08X} lr4={:08X}",
                  frame, native60_slide_sim_frame == frame ? 1 : 0, emitter, bank, cue,
                  emitter >= 0x40000000 ? REX_LOAD_U32(emitter + 52) : 0, repeat ? 1 : 0, skip ? 1 : 0,
                  uint32_t(ctx.lr), lr2, lr3, lr4);
    }
  }
  if (skip) {
    ctx.r3.u64 = REX_LOAD_U32(emitter + 52);
    return;
  }
  __imp__sub_82668CC8(ctx, base);
}
// Sound event requests: 826676A0 / 82666DA0 (r3 = sound manager, r4 = event
// id, r5 = variant, f1 = volume) create a new sound object per request.
// Pool Slide (log 428): at each landing the same events (water loop 58/19,
// splashes) were requested on the slide-sim update AND the in-between 60 Hz
// update, spawning every sound twice 16 ms apart (robotic comb). A repeat of
// the same request one update later is skipped (the first handle is
// returned); a request repeated every update thus plays at 30/s as in the
// original. F6 toggles the dedupe live (A/B).
// Boot title voice ("Dead or Alive Xtreme 2") played twice after skipping
// the boot movies: log every sound request for 25 s after a movie starts or
// is skipped (experimental logging).
inline std::atomic<int64_t> native60_movie_event_ms{-1000000};
inline int64_t Native60NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
namespace native60_sound_events {
struct Last { uint32_t manager, event, variant, handle; uint64_t frame; };
inline std::mutex mutex;
inline std::array<Last, 64> recent{};
inline uint32_t next = 0;
inline bool enabled = true;
inline void PollKey() {
  if (!REXCVAR_GET(doax2_native60_counter_scan)) return;
  static bool f6_was_down = false;
  const bool f6_down = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
  if (f6_down && !f6_was_down) {
    enabled = !enabled;
    REXLOG_INFO("DOAX2 native60 sound event dedupe (F6): {}", enabled ? "ON" : "OFF");
  }
  f6_was_down = f6_down;
}
template <typename F>
inline void Request(const char* name, PPCContext& ctx, uint8_t* base, F&& call) {
  PollKey();
  const uint32_t manager = ctx.r3.u32, event = ctx.r4.u32, variant = ctx.r5.u32;
  const uint64_t frame = Doax2EngineFrameCount();
  const bool half = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f;
  bool repeat = false;
  uint32_t previous = 0;
  {
    std::lock_guard lock(mutex);
    for (const Last& l : recent)
      if (l.manager == manager && l.event == event && l.variant == variant && l.frame + 1 == frame) {
        repeat = true;
        previous = l.handle;
      }
  }
  const uint32_t lr = uint32_t(ctx.lr);
  // Title voice ("Dead or Alive Xtreme 2", a random girl): 824C0B78 picks
  // and requests it (call at 824C0E10) when the title is entered. Skipping
  // the boot movies enters the title twice 14 updates apart (log game-exp
  // 004: events 1118 then 3732), and the second voice cut into the first.
  // Drop a second title voice within 3 s.
  bool title_voice_repeat = false;
  if (lr == 0x824C0E10) {
    static std::atomic<int64_t> last_title_voice_ms{-1000000};
    const int64_t now = Native60NowMs();
    title_voice_repeat = now - last_title_voice_ms.load() < 3000;
    if (!title_voice_repeat) last_title_voice_ms = now;
    else REXLOG_INFO("DOAX2 title voice requested again {} ms after the first: skipped (event {})",
                     now - last_title_voice_ms.load(), int32_t(event));
  }
  const bool skip = (repeat && half && enabled && event != 0xFFFFFFFFu) || title_voice_repeat;
  const double volume = ctx.f1.f64;
  if (skip) {
    ctx.r3.u64 = previous;
  } else {
    call();
    std::lock_guard lock(mutex);
    bool updated = false;
    for (Last& l : recent)
      if (l.manager == manager && l.event == event && l.variant == variant) {
        l = {manager, event, variant, ctx.r3.u32, frame};
        updated = true;
        break;
      }
    if (!updated) recent[next++ % recent.size()] = {manager, event, variant, ctx.r3.u32, frame};
  }
  if (Native60TransitionLogOn() && Native60NowMs() - native60_movie_event_ms.load() < 25000) {
    static std::atomic<uint32_t> boot_lines{0};
    if (boot_lines.fetch_add(1) < 400)
      REXLOG_INFO("DOAX2 boot sound {}: frame={} event={} variant={:X} vol={:.3g} repeat={} skipped={} "
                  "handle={:08X} lr={:08X}",
                  name, frame, int32_t(event), variant, volume, repeat ? 1 : 0, skip ? 1 : 0, ctx.r3.u32, lr);
  }
  if (REXCVAR_GET(doax2_native60_counter_scan) && (Native60SlideActive() || skip)) {
    static std::atomic<uint32_t> lines{0};
    if (lines.fetch_add(1) < 6000)
      REXLOG_INFO("DOAX2 native60 sound event {}: frame={} sim={} event={} variant={:X} vol={:.3g} repeat={} skipped={} handle={:08X} lr={:08X}",
                  name, frame, native60_slide_sim_frame == frame ? 1 : 0, int32_t(event), variant, volume,
                  repeat ? 1 : 0, skip ? 1 : 0, ctx.r3.u32, lr);
  }
}
}  // namespace native60_sound_events
REX_EXTERN(__imp__sub_826676A0);
REX_EXTERN(__imp__sub_82666DA0);
REX_HOOK_RAW(sub_826676A0) {
  native60_sound_events::Request("826676A0", ctx, base, [&] { __imp__sub_826676A0(ctx, base); });
}
REX_HOOK_RAW(sub_82666DA0) {
  native60_sound_events::Request("82666DA0", ctx, base, [&] { __imp__sub_82666DA0(ctx, base); });
}
REX_EXTERN(__imp__sub_826A3440);
REX_EXTERN(__imp__sub_826A34C8);
REX_HOOK_RAW(sub_826A3440) {
  Native60SlideXformLog("826A3440", ctx, base);
  __imp__sub_826A3440(ctx, base);
}
REX_HOOK_RAW(sub_826A34C8) {
  Native60SlideXformLog("826A34C8", ctx, base);
  __imp__sub_826A34C8(ctx, base);
}
DOAX2_NATIVE60_POS_PROBE(sub_826A7F90)
DOAX2_NATIVE60_POS_PROBE(sub_826A8238)
DOAX2_NATIVE60_POS_PROBE(sub_826A8408)
DOAX2_NATIVE60_POS_PROBE(sub_826A8428)
DOAX2_NATIVE60_POS_PROBE(sub_826A8470)
DOAX2_NATIVE60_POS_PROBE(sub_826A8508)
DOAX2_NATIVE60_POS_PROBE(sub_826A8518)
DOAX2_NATIVE60_POS_PROBE(sub_826A8580)
DOAX2_NATIVE60_POS_PROBE(sub_826A8908)
DOAX2_NATIVE60_POS_PROBE(sub_826A89E8)
DOAX2_NATIVE60_POS_PROBE(sub_826A8DE0)
DOAX2_NATIVE60_POS_PROBE(sub_826A8EA8)
DOAX2_NATIVE60_POS_PROBE(sub_826A8F70)
DOAX2_NATIVE60_POS_PROBE(sub_826A8FF8)
DOAX2_NATIVE60_POS_PROBE(sub_826A90C0)
DOAX2_NATIVE60_POS_PROBE(sub_826A9338)
DOAX2_NATIVE60_POS_PROBE(sub_826A9508)
DOAX2_NATIVE60_POS_PROBE(sub_826A95D0)
DOAX2_NATIVE60_POS_PROBE(sub_826A9848)
DOAX2_NATIVE60_POS_PROBE(sub_826A99C8)
DOAX2_NATIVE60_POS_PROBE(sub_826A9A98)
DOAX2_NATIVE60_POS_PROBE(sub_826A9AF8)
DOAX2_NATIVE60_POS_PROBE(sub_826A9E28)
DOAX2_NATIVE60_POS_PROBE(sub_826A9F28)
DOAX2_NATIVE60_POS_PROBE(sub_826A9FE8)
DOAX2_NATIVE60_POS_PROBE(sub_826AA2C0)
DOAX2_NATIVE60_POS_PROBE(sub_826AA650)

// Beach Flags run stutter (log 217 dump, F10 bursts): runners move by their
// run motion's root travel, but on the update where that motion loops or
// switches (pose record +76 frame reset, runner +52 motion id change) the
// runner's movement is exactly zero and the next update moves twice as far.
// Pose root (+12/+20) stalls with it, and the race camera aims at the runner
// one update later, so the whole view jerks every ~20 updates (also at 30
// FPS). Carry the previous update's movement into the stalled update and take
// it back out of the catch-up, so the runner ends at the same place without
// the stall. Runner k: *(8398A4A0+32) + k*192 (+0/+8 position x/z, +32/+40
// displacement x/z); pose record k: 834271A0 + k*288 (+12/+20 root x/z).
struct Native60FlagsRootMotion {
  uint32_t runners = 0;
  std::array<std::array<float, 6>, 2> before{};
};
constexpr std::array<uint32_t, 6> kNative60FlagsRunnerFields = {0, 8, 32, 40, 12, 20};
inline bool Native60FlagsRunnerField(uint32_t i) { return i < 4; }
uint32_t Native60FlagsFieldAddress(uint32_t runners, uint32_t k, uint32_t i) {
  return Native60FlagsRunnerField(i) ? runners + k * 192 + kNative60FlagsRunnerFields[i]
                                     : 0x834271A0 + k * 288 + kNative60FlagsRunnerFields[i];
}
Native60FlagsRootMotion Native60FlagsRootMotionBegin(uint8_t* base) {
  Native60FlagsRootMotion state;
  const uint32_t runners = REX_LOAD_U32(0x8398A4A0 + 32);
  if (runners < 0x40000000 || runners >= 0xA0000000) return state;
  state.runners = runners;
  for (uint32_t k = 0; k < 2; ++k)
    for (uint32_t i = 0; i < 6; ++i)
      state.before[k][i] = Native60LoadFloat(base, Native60FlagsFieldAddress(runners, k, i));
  return state;
}
void Native60FlagsRootMotionEnd(uint8_t* base, const Native60FlagsRootMotion& state) {
  struct Runner {
    uint32_t runners = 0;
    float last_dx = 0, last_dz = 0;  // previous update's movement
    float carry_dx = 0, carry_dz = 0;
    bool carry = false;
    // Lengths of the last three normal (uncorrected) running steps; a stall
    // is only corrected during steady running (log 219: carrying the
    // pre-race placement jumps diverged and lost the camera).
    std::array<float, 3> steps{};
    uint32_t steady = 0;
    // Some motion switches instead move a double step with no stall; the
    // extra step is removed there and fed back over the next updates.
    float spread_dx = 0, spread_dz = 0;
    uint32_t spread_left = 0;
  };
  static std::array<Runner, 2> runners_state;
  static uint64_t fixes = 0, unwinds = 0, spreads = 0;
  if (!state.runners) return;
  for (uint32_t k = 0; k < 2; ++k) {
    Runner& r = runners_state[k];
    if (r.runners != state.runners) r = Runner{state.runners};
    const auto load = [&](uint32_t i) {
      return Native60LoadFloat(base, Native60FlagsFieldAddress(state.runners, k, i));
    };
    const auto add = [&](uint32_t i, float delta) {
      const uint32_t address = Native60FlagsFieldAddress(state.runners, k, i);
      REX_STORE_U32(address, std::bit_cast<uint32_t>(Native60LoadFloat(base, address) + delta));
    };
    const float dx = load(0) - state.before[k][0];
    const float dz = load(1) - state.before[k][1];
    if (r.carry) {
      // Undo the carried step from the catch-up (only if it actually came).
      r.carry = false;
      const float catch_up = dx * r.carry_dx + dz * r.carry_dz;
      const float carried = r.carry_dx * r.carry_dx + r.carry_dz * r.carry_dz;
      if (carried > 0 && catch_up > 1.5f * carried && catch_up < 3.0f * carried) {
        for (uint32_t i : {0u, 2u, 4u}) add(i, -r.carry_dx);
        for (uint32_t i : {1u, 3u, 5u}) add(i, -r.carry_dz);
        r.last_dx = dx - r.carry_dx;
        r.last_dz = dz - r.carry_dz;
        ++unwinds;
      } else {
        r.last_dx = dx;
        r.last_dz = dz;
      }
      r.steps = {r.steps[1], r.steps[2],
                 std::sqrt(r.last_dx * r.last_dx + r.last_dz * r.last_dz)};
      r.steady = 1;
      continue;
    }
    if (r.spread_left) {
      // Feed back a removed extra step in small pieces (keeps total travel).
      for (uint32_t i : {0u, 2u, 4u}) add(i, r.spread_dx);
      for (uint32_t i : {1u, 3u, 5u}) add(i, r.spread_dz);
      --r.spread_left;
    }
    const float step = std::sqrt(dx * dx + dz * dz);
    const float last = std::sqrt(r.last_dx * r.last_dx + r.last_dz * r.last_dz);
    bool steady_steps = true;
    for (float s : r.steps)
      steady_steps = steady_steps && s >= 0.02f && s <= 0.3f && std::fabs(s - last) <= 0.4f * last;
    const bool steady = r.steady >= 3 && steady_steps;
    const float along = last > 0 ? (dx * r.last_dx + dz * r.last_dz) / (last * last) : 0.0f;
    if (r.steady >= 2 && steady_steps && !r.spread_left && along > 1.6f && along < 2.6f &&
        step < 2.6f * last) {
      // Double step without a stall: remove the extra step now, spread it
      // over the next 8 updates.
      constexpr uint32_t kSpread = 8;
      const float extra_dx = dx - r.last_dx, extra_dz = dz - r.last_dz;
      for (uint32_t i : {0u, 2u, 4u}) add(i, -extra_dx);
      for (uint32_t i : {1u, 3u, 5u}) add(i, -extra_dz);
      r.spread_dx = extra_dx / kSpread;
      r.spread_dz = extra_dz / kSpread;
      r.spread_left = kSpread;
      ++spreads;
      if (spreads <= 6 || spreads % 100 == 0)
        REXLOG_INFO("DOAX2 native60 flags run double step spread: runner={} extra=({:.4f},{:.4f}) "
                    "spreads={}", k, extra_dx, extra_dz, spreads);
      r.steady = 0;
      continue;  // last step stays the steady one
    }
    if (dx == 0.0f && dz == 0.0f && steady &&
        load(4) == state.before[k][4] && load(5) == state.before[k][5]) {
      // Stalled update during steady running: apply the previous movement now.
      for (uint32_t i : {0u, 2u, 4u}) add(i, r.last_dx);
      for (uint32_t i : {1u, 3u, 5u}) add(i, r.last_dz);
      r.carry = true;
      r.carry_dx = r.last_dx;
      r.carry_dz = r.last_dz;
      r.steady = 0;
      if (++fixes <= 6 || fixes % 100 == 0)
        REXLOG_INFO("DOAX2 native60 flags run stall carried: runner={} step=({:.4f},{:.4f}) "
                    "fixes={} unwinds={}", k, r.last_dx, r.last_dz, fixes, unwinds);
      continue;
    }
    r.steps = {r.steps[1], r.steps[2], step};
    r.steady = std::min<uint32_t>(r.steady + 1, 1000);
    r.last_dx = dx;
    r.last_dz = dz;
  }
}
REX_EXTERN(__imp__sub_826AC790);
REX_HOOK_RAW(sub_826AC790) {
  // Beach Flags scripted camera 8398A4A0: +16/+20 are float clocks that
  // advance exactly 1.0 per call regardless of the game step; 82427430
  // evaluates the race camera from +16. Advance them by the step instead.
  constexpr uint32_t kFlagsCamera = 0x8398A4A0;
  Native60NoteFiberContext(base, "826AC790 flags camera clock");
  const bool scaled = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f &&
                      !Native60InScriptFiber(base);
  const float clock_a = Native60LoadFloat(base, kFlagsCamera + 16);
  const float clock_b = Native60LoadFloat(base, kFlagsCamera + 20);
  Native60ObjectDeltaBegin(base, native60_flags_a, ctx.r3.u32);
  native60_flags_frame.store(Doax2EngineFrameCount(), std::memory_order_relaxed);
  __imp__sub_826AC790(ctx, base);
  Native60ObjectDeltaEnd(base, native60_flags_a);
  Native60FlagsCameraDump(base);
  if (!scaled) return;
  uint32_t corrected = 0;
  if (Native60LoadFloat(base, kFlagsCamera + 16) == clock_a + 1.0f) {
    REX_STORE_U32(kFlagsCamera + 16, std::bit_cast<uint32_t>(clock_a + 0.5f));
    ++corrected;
  }
  if (Native60LoadFloat(base, kFlagsCamera + 20) == clock_b + 1.0f) {
    REX_STORE_U32(kFlagsCamera + 20, std::bit_cast<uint32_t>(clock_b + 0.5f));
    ++corrected;
  }
  static uint64_t calls = 0;
  if (corrected && (++calls <= 4 || calls % 600 == 0))
    REXLOG_INFO("DOAX2 native60 flags camera clock halved: calls={} "
                "clock={} corrected={}", calls,
                Native60LoadFloat(base, kFlagsCamera + 16), corrected);
}
// Beach Flags runner power gauge (runner +156), 826AFAE0: each call
// subtracts a tiered decay; human runners (+116 == 0) add +160 per button
// press, CPU runners add their AI gain *(+176)+32 every call. At 60 Hz the
// decay and the CPU gain ran twice as fast (log 161 vs 159: the CPU runner
// sprinted ~37% faster than the player). With the base speed scaled, halve
// the CPU's net per-call change; for humans pre-credit half the decay so
// presses keep their full value.
float Native60FlagsGaugeDecay(uint8_t* base, float value) {
  constexpr uint32_t table = 0x821CB16C;  // lis -32227, addi -20116
  if (value < Native60LoadFloat(base, 0x820BD90C))           // -9972 const
    return Native60LoadFloat(base, table - 172);
  if (value < Native60LoadFloat(base, table + 13112))
    return Native60LoadFloat(base, 0x82064494);                // 17556 const
  if (value < Native60LoadFloat(base, table + 0))
    return Native60LoadFloat(base, table + 13132);
  return Native60LoadFloat(base, table + 9604);
}
// 826AE108 (race step) decrements each CPU runner's decision countdown at
// runner +164 once per call and re-rolls a random delay at zero, so CPU
// decisions ran twice as often at 60 Hz (CPU still ~17-50% faster than the
// player after the gauge fix, log 163). Hold the decrement on alternate calls.
REX_EXTERN(__imp__sub_826AE108);
REX_HOOK_RAW(sub_826AE108) {
  const uint32_t game = ctx.r3.u32;
  Native60NoteFiberContext(base, "826AE108 flags race step");
  const bool scaled = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f &&
                      game >= 0x40000000 && !Native60InScriptFiber(base);
  const uint32_t runners = scaled ? REX_LOAD_U32(game + 32) : 0;
  std::array<uint32_t, 2> before{};
  if (runners >= 0x40000000)
    for (uint32_t i = 0; i < 2; ++i) before[i] = REX_LOAD_U32(runners + i * 192 + 164);
  __imp__sub_826AE108(ctx, base);
  if (runners < 0x40000000) return;
  static bool hold = false;
  hold = !hold;
  if (!hold) return;
  for (uint32_t i = 0; i < 2; ++i) {
    const uint32_t runner = runners + i * 192;
    if (REX_LOAD_U32(runner + 116) == 0) continue;  // human runner
    if (REX_LOAD_U32(runner + 164) == before[i] - 1)
      REX_STORE_U32(runner + 164, before[i]);
  }
}

REX_EXTERN(__imp__sub_826AFAE0);
REX_HOOK_RAW(sub_826AFAE0) {
  const uint32_t runner = ctx.r3.u32;
  Native60NoteFiberContext(base, "826AFAE0 flags gauge");
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f ||
      runner < 0x40000000 || Native60InScriptFiber(base)) {
    __imp__sub_826AFAE0(ctx, base);
    return;
  }
  const bool cpu = REX_LOAD_U32(runner + 116) != 0;
  const float before = Native60LoadFloat(base, runner + 156);
  if (!cpu) {
    const float decay = Native60FlagsGaugeDecay(base, before);
    if (std::isfinite(decay))
      REX_STORE_U32(runner + 156, std::bit_cast<uint32_t>(before + decay * 0.5f));
  }
  __imp__sub_826AFAE0(ctx, base);
  if (cpu) {
    const float after = Native60LoadFloat(base, runner + 156);
    float net = after - before;
    // Optional difficulty: scale only the CPU's gain (net + decay), never its
    // decay, so a weaker CPU just builds less speed.
    const float strength = float(REXCVAR_GET(doax2_native60_flags_cpu_strength));
    const float decay = Native60FlagsGaugeDecay(base, before);
    if (strength > 0.0f && strength < 1.0f && std::isfinite(decay) && net + decay > 0.0f)
      net = (net + decay) * strength - decay;
    if (std::isfinite(after))
      REX_STORE_U32(runner + 156, std::bit_cast<uint32_t>(before + net * 0.5f));
  }
  static uint64_t calls = 0;
  if (++calls <= 4 || calls % 1200 == 0)
    REXLOG_INFO("DOAX2 native60 flags gauge: calls={} cpu={} before={} "
                "after={}", calls, cpu, before,
                Native60LoadFloat(base, runner + 156));
}

// Region statistics: per-word change count and mean float delta per call,
// reported every 300 calls. Compared between 30 Hz and scaled 60 Hz runs,
// a field whose per-call delta is unchanged advances twice as fast.
struct Native60RegionStats {
  std::string name;
  uint32_t address = 0, words = 0;
  uint64_t calls = 0;
  std::vector<uint32_t> before;
  std::vector<uint32_t> changes;
  std::vector<double> delta_sum;
  void Begin(uint8_t* base, uint32_t addr, uint32_t size_words) {
    if (addr != address || size_words != words) {
      address = addr; words = size_words; calls = 0;
      changes.assign(words, 0); delta_sum.assign(words, 0.0);
    }
    before.resize(words);
    for (uint32_t i = 0; i < words; ++i) before[i] = REX_LOAD_U32(address + i * 4);
  }
  void End(uint8_t* base) {
    ++calls;
    for (uint32_t i = 0; i < words; ++i) {
      const uint32_t now = REX_LOAD_U32(address + i * 4);
      if (now == before[i]) continue;
      ++changes[i];
      const float d = std::bit_cast<float>(now) - std::bit_cast<float>(before[i]);
      if (std::isfinite(d) && std::fabs(d) < 1e4f) delta_sum[i] += d;
    }
    if (calls % 300 != 0) return;
    static uint32_t reports = 0;
    if (++reports <= 3000)
      for (uint32_t i = 0; i < words; ++i) {
        if (changes[i] < calls / 3) continue;
        const uint32_t value = REX_LOAD_U32(address + i * 4);
        REXLOG_INFO("DOAX2 native60 region: name={} addr={:08X} offset={} "
                    "calls={} changes={} mean_delta={:.6f} float={} int={} "
                    "step={}", name, address, i * 4, calls, changes[i],
                    delta_sum[i] / double(calls), std::bit_cast<float>(value),
                    int32_t(value), Native60LoadFloat(base, kNative60GameStep));
      }
    changes.assign(words, 0);
    delta_sum.assign(words, 0.0);
    calls = 0;
  }
};

// Beach Flags keyframed transforms. 826ACB20 indexes a 32-byte keyframe
// table with int(game clock +16) - 1 (clock <= 120) and 826ABE00/826AB800
// build a 4x4 matrix from that keyframe. With the clock advancing 0.5 per
// 60 Hz update the index changed every other update, so the result held
// for two frames and then jumped (runner hitch/double image). Evaluate the
// authored table at the fractional clock: build keyframes n and n+1 and
// blend the matrices by the fraction.
struct Native60FlagsKeyframe {
  bool active = false;
  float fraction = 0;
};
static thread_local Native60FlagsKeyframe native60_flags_keyframe;
void Native60BlendFlagsKeyframe(PPCContext& ctx, uint8_t* base,
                                void (*original)(PPCContext&, uint8_t*)) {
  const auto sample = native60_flags_keyframe;
  if (!sample.active || sample.fraction <= 0.0f) {
    original(ctx, base);
    return;
  }
  const uint32_t output = ctx.r3.u32;
  PPCContext next = ctx;
  next.r4.u64 = ctx.r4.u32 + 32;
  original(next, base);
  std::array<float, 16> upper{};
  for (uint32_t i = 0; i < 16; ++i)
    upper[i] = Native60LoadFloat(base, output + i * 4);
  // Authoritative invocation last so its register/stack effects remain.
  original(ctx, base);
  for (uint32_t i = 0; i < 16; ++i) {
    const float lower = Native60LoadFloat(base, output + i * 4);
    const float blended = lower + sample.fraction * (upper[i] - lower);
    if (std::isfinite(blended))
      REX_STORE_U32(output + i * 4, std::bit_cast<uint32_t>(blended));
  }
  static uint64_t calls = 0;
  if (++calls <= 4 || calls % 600 == 0)
    REXLOG_INFO("DOAX2 native60 flags keyframe blended: calls={} "
                "fraction={}", calls, sample.fraction);
}
REX_EXTERN(__imp__sub_826AB800);
REX_HOOK_RAW(sub_826AB800) {
  Native60BlendFlagsKeyframe(ctx, base, __imp__sub_826AB800);
}
REX_EXTERN(__imp__sub_826ABE00);
REX_HOOK_RAW(sub_826ABE00) {
  Native60BlendFlagsKeyframe(ctx, base, __imp__sub_826ABE00);
}

REX_EXTERN(__imp__sub_826ACB20);
REX_HOOK_RAW(sub_826ACB20) {
  Native60MarkMinigameUpdate();
  {
    const auto prior = native60_flags_keyframe;
    native60_flags_keyframe = {};
    if (float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f &&
        ctx.r3.u32 >= 0x40000000) {
      const float clock = Native60LoadFloat(base, ctx.r3.u32 + 16);
      // Next keyframe exists while int(clock) <= 119 (index int(clock) - 1).
      if (std::isfinite(clock) && clock >= 1.0f && clock < 120.0f)
        native60_flags_keyframe = {true, clock - std::floor(clock)};
    }
    (void)prior;
  }
  // Beach Flags game object (r3) and its 192-byte runner records (+32).
  static std::array<Native60RegionStats, 5> regions = {{
      {"flags_game"}, {"runner0"}, {"runner1"}, {"runner2"}, {"runner3"}}};
  const bool probe = REXCVAR_GET(doax2_native60_counter_scan) &&
                     ctx.r3.u32 >= 0x40000000;
  const uint32_t game = ctx.r3.u32;
  const uint32_t runners = probe ? REX_LOAD_U32(game + 32) : 0;
  // Measure update-to-update (previous call to this call) so changes made
  // anywhere in the frame are included.
  static bool begun = false;
  if (probe && begun) {
    regions[0].End(base);
    if (runners >= 0x40000000)
      for (uint32_t i = 0; i < 4; ++i) regions[1 + i].End(base);
  }
  begun = probe;
  // Pointer-follow probe: the runners' world transforms live behind
  // pointers in the runner records. For each runner, follow up to six
  // pointer-looking fields and track the first 512 bytes they point to.
  {
    static std::array<Native60RegionStats, 12> pointees;
    static std::array<uint32_t, 12> targets{};
    static bool pointees_begun = false;
    if (probe && runners >= 0x40000000) {
      if (pointees_begun)
        for (auto& stats : pointees) if (stats.words) stats.End(base);
      uint32_t slot = 0;
      for (uint32_t r = 0; r < 2 && slot < pointees.size(); ++r) {
        uint32_t found = 0;
        for (uint32_t off = 0; off < 192 && found < 6 && slot < pointees.size(); off += 4) {
          const uint32_t value = REX_LOAD_U32(runners + r * 192 + off);
          if (value < 0x40000000 || value >= 0xA0000000 || (value & 3)) continue;
#if defined(_WIN32)
          // Floats can look like heap pointers; only follow committed memory.
          MEMORY_BASIC_INFORMATION info{};
          if (!VirtualQuery(base + value, &info, sizeof(info)) ||
              info.State != MEM_COMMIT ||
              (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
              uintptr_t(base + value) + 512 >
                  uintptr_t(info.BaseAddress) + info.RegionSize)
            continue;
#endif
          if (targets[slot] != value)
            pointees[slot].name = fmt::format("runner{}_ptr{}", r, off);
          targets[slot] = value;
          pointees[slot].Begin(base, value, 128);
          ++slot;
          ++found;
        }
      }
      pointees_begun = true;
    }
  }
  if (probe) {
    regions[0].Begin(base, game, 128);
    if (runners >= 0x40000000)
      for (uint32_t i = 0; i < 4; ++i)
        regions[1 + i].Begin(base, runners + i * 192, 48);
  }
  Native60ObjectDeltaBegin(base, native60_flags_b, ctx.r3.u32);
  __imp__sub_826ACB20(ctx, base);
  native60_flags_keyframe = {};
  Native60ObjectDeltaEnd(base, native60_flags_b);
  // Stutter trace: per-update runner displacement (+32) while it changes,
  // 30 samples per line, first 240 moving updates of each session.
  if (probe && runners >= 0x40000000) {
    static std::string line0, line1, line2, line3, line4, line5, line6, line7;
    static uint32_t samples = 0, last_bits = 0;
    const uint32_t bits = REX_LOAD_U32(runners + 32);
    if (samples < 600 && (bits != last_bits || samples > 0) && bits != 0) {
      line0 += fmt::format(" {:.4f}", Native60LoadFloat(base, runners + 32));
      line1 += fmt::format(" {:.4f}", Native60LoadFloat(base, runners + 192 + 32));
      line2 += fmt::format(" {:.4f}", Native60LoadFloat(base, 0x839BC1D0 + 48));
      // Motion records (834271A0 + k*144): root position +0/+8, frame +52.
      line3 += fmt::format(" {:.4f}", Native60LoadFloat(base, 0x834271A0 + 0));
      line4 += fmt::format(" {:.4f}", Native60LoadFloat(base, 0x834271A0 + 2 * 144 + 0));
      line5 += fmt::format(" {:.2f}", Native60LoadFloat(base, 0x834271A0 + 52));
      line6 += fmt::format(" {:.4f}", Native60LoadFloat(base, 0x833B8520 + 12));
      line7 += fmt::format(" {:.4f}", Native60LoadFloat(base, 0x833B8520 + 20));
      if (++samples % 30 == 0) {
        REXLOG_INFO("DOAX2 native60 runner trace: runner0{}", line0);
        REXLOG_INFO("DOAX2 native60 runner trace: runner1{}", line1);
        REXLOG_INFO("DOAX2 native60 runner trace: camera_x{}", line2);
        REXLOG_INFO("DOAX2 native60 runner trace: record0_x{}", line3);
        REXLOG_INFO("DOAX2 native60 runner trace: record2_x{}", line4);
        REXLOG_INFO("DOAX2 native60 runner trace: record0_frame{}", line5);
        REXLOG_INFO("DOAX2 native60 runner trace: player0_x{}", line6);
        REXLOG_INFO("DOAX2 native60 runner trace: player0_z{}", line7);
        line6.clear();
        line7.clear();
        line0.clear();
        line1.clear();
        line2.clear();
        line3.clear();
        line4.clear();
        line5.clear();
      }
    }
    last_bits = bits;
  }
}

// Pose-camera sequencer 82430628 (camera mode 8: volleyball after-point
// poses, Beach Flags win/loss pose) drives the camera from a pose record
// (834271A0 + k*288): elapsed whole frames (+80) of total (+112). It blends
// integer camera keys and truncates the result (fctiwz) to integer units of
// ~0.01 before scaling, so at 30 Hz the camera moved one unit per frame. At
// 60 Hz the record's elapsed count only advances on the half-frame updates
// (frame n.5 -> elapsed n; frame n+1.0 -> still n, log 239), and a half
// unit is always truncated, so the camera stepped at 30 Hz behind a smooth
// 60 Hz pose (logs 233-241). On the in-between updates, evaluate the game's
// camera at elapsed+1 as well and set the camera outputs to the mean of
// the two neighbouring frames (the camera's own path at elapsed + 0.5).
// The real call (elapsed unchanged) runs last; counters are restored.
REX_EXTERN(__imp__sub_82430628);
REX_HOOK_RAW(sub_82430628) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_82430628");
  const uint32_t object = ctx.r3.u32;
  std::array<uint32_t, 16> lagging{};
  uint32_t lagging_count = 0;
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f && object >= 0x40000000) {
    for (uint32_t k = 0; k < 16; ++k) {
      const uint32_t record = 0x834271A0 + k * 288;
      const uint32_t total = REX_LOAD_U16(record + 112);
      if (total == 0 || total > 5000) continue;
      const int32_t elapsed = int32_t(REX_LOAD_U32(record + 80));
      const float frame = Native60LoadFloat(base, record + 76);
      if (!std::isfinite(frame) || elapsed < 0 || elapsed + 1 >= int32_t(total)) continue;
      const float nearest = std::round(frame);
      if (std::fabs(frame - nearest) > 0.25f || int32_t(nearest) - 1 != elapsed) continue;
      lagging[lagging_count++] = record;
    }
  }
  // Camera outputs written by 82430628 on the camera object.
  constexpr std::array<std::pair<uint32_t, uint32_t>, 2> kOutputs = {{{32, 128}, {256, 304}}};
  std::array<float, 36> next{};
  bool averaged = false;
  if (lagging_count) {
    for (uint32_t i = 0; i < lagging_count; ++i)
      REX_STORE_U32(lagging[i] + 80, REX_LOAD_U32(lagging[i] + 80) + 1);
    PPCContext probe = ctx;
    __imp__sub_82430628(probe, base);
    for (uint32_t i = 0; i < lagging_count; ++i)
      REX_STORE_U32(lagging[i] + 80, REX_LOAD_U32(lagging[i] + 80) - 1);
    uint32_t n = 0;
    for (auto [from, to] : kOutputs)
      for (uint32_t off = from; off < to; off += 4) next[n++] = Native60LoadFloat(base, object + off);
    averaged = true;
  }
  __imp__sub_82430628(ctx, base);
  uint32_t blended = 0;
  if (averaged) {
    uint32_t n = 0;
    for (auto [from, to] : kOutputs)
      for (uint32_t off = from; off < to; off += 4) {
        const float now = Native60LoadFloat(base, object + off);
        const float later = next[n++];
        if (!std::isfinite(now) || !std::isfinite(later) || now == later ||
            std::fabs(later - now) > 1.0f)
          continue;
        REX_STORE_U32(object + off, std::bit_cast<uint32_t>(0.5f * (now + later)));
        ++blended;
      }
    static uint64_t calls = 0;
    if (++calls <= 4 || calls % 600 == 0)
      REXLOG_INFO("DOAX2 native60 pose camera half frame: calls={} records={} blended_fields={}",
                  calls, lagging_count, blended);
  }
  if (REXCVAR_GET(doax2_native60_counter_scan)) {
    // Trace (diagnostics): 80 consecutive calls at the start of each pose.
    static uint64_t last_frame = 0, traced = 0;
    const uint64_t frame = Doax2EngineFrameCount();
    if (frame > last_frame + 30) traced = 0;
    last_frame = frame;
    if (traced++ < 80 && object >= 0x40000000)
      REXLOG_INFO("DOAX2 native60 pose camera trace: n={} engine_frame={} lagging={} "
                  "blended={} c32=({:.4f},{:.4f}) c96=({:.4f},{:.4f})",
                  traced, frame, lagging_count, blended,
                  Native60LoadFloat(base, object + 32), Native60LoadFloat(base, object + 36),
                  Native60LoadFloat(base, object + 96), Native60LoadFloat(base, object + 100));
  }
}

// Attack diagnosis: 8268DF20 attacks only while player +69 (hit window) is
// set and 8268DCF8 finds the hand sphere on the ball. Log window edges per
// player and every hand/ball test with its inputs and result.
void ReportNative60HitWindows(uint8_t* base) {
  static std::array<uint8_t, 4> last{};
  static std::array<uint64_t, 4> opened_at{};
  static uint64_t updates = 0, lines = 0;
  ++updates;
  for (uint32_t p = 0; p < 4; ++p) {
    const uint32_t state = 0x833B8520 + p * 152;
    const uint8_t window = REX_LOAD_U8(state + 69);
    if (window == last[p]) continue;
    if (window) opened_at[p] = updates;
    if (++lines <= 3000)
      REXLOG_INFO("DOAX2 native60 hit window: player={} {} after={} motion={} "
                  "f44={} f48={} f96={} ball_t={} ball=({:.3f},{:.3f},{:.3f}) "
                  "step={} update={}",
                  p, window ? "open" : "close",
                  window ? 0 : updates - opened_at[p],
                  REX_LOAD_U16(state + 64), Native60LoadFloat(base, state + 44),
                  Native60LoadFloat(base, state + 48),
                  Native60LoadFloat(base, state + 96),
                  Native60LoadFloat(base, kNative60Ball + 7364),
                  Native60LoadFloat(base, kNative60Ball + 0),
                  Native60LoadFloat(base, kNative60Ball + 4),
                  Native60LoadFloat(base, kNative60Ball + 8),
                  Native60LoadFloat(base, kNative60GameStep), updates);
    last[p] = window;
  }
}

REX_EXTERN(__imp__sub_8268DCF8);
REX_HOOK_RAW(sub_8268DCF8) {
  const uint32_t hand = ctx.r3.u32, ball = ctx.r4.u32, predicted = ctx.r5.u32;
  const double radius = ctx.f1.f64;
  std::array<float, 3> h{}, b{}, q{};
  for (uint32_t i = 0; i < 3; ++i) {
    h[i] = Native60LoadFloat(base, hand + i * 4);
    b[i] = Native60LoadFloat(base, ball + i * 4);
    q[i] = Native60LoadFloat(base, predicted + i * 4);
  }
  __imp__sub_8268DCF8(ctx, base);
  static uint64_t lines = 0;
  if (++lines <= 4000)
    REXLOG_INFO("DOAX2 native60 hand test: result={} radius={} "
                "hand=({:.3f},{:.3f},{:.3f}) ball=({:.3f},{:.3f},{:.3f}) "
                "aux=({:.3f},{:.3f},{:.3f}) ball_t={} step={}",
                ctx.r3.u32, radius, h[0], h[1], h[2], b[0], b[1], b[2],
                q[0], q[1], q[2], Native60LoadFloat(base, kNative60Ball + 7364),
                Native60LoadFloat(base, kNative60GameStep));
}

// Fixed per-update duration counters. With the base speed at 0.5, undo an
// exact +-1 change on alternate calls so the counter counts original 30 Hz
// frames. Each site keeps its own cadence; resets and jumps are untouched.
template <size_t N>
struct Native60HeldCounters {
  const char* name;
  std::array<uint32_t, N> addresses;
  std::array<uint32_t, N> before{};
  bool hold = false;
  uint64_t calls = 0, held = 0;
  bool Begin(uint8_t* base) {
    if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f) return false;
    Native60NoteFiberContext(base, name);
    if (Native60InScriptFiber(base)) return false;
    for (size_t i = 0; i < N; ++i) before[i] = REX_LOAD_U32(addresses[i]);
    return true;
  }
  void End(uint8_t* base) {
    hold = !hold;
    ++calls;
    if (hold) {
      for (size_t i = 0; i < N; ++i) {
        const int32_t delta = int32_t(REX_LOAD_U32(addresses[i]) - before[i]);
        if (delta != 1 && delta != -1) continue;
        REX_STORE_U32(addresses[i], before[i]);
        ++held;
      }
    }
    if (calls <= 4 || calls % 1800 == 0)
      REXLOG_INFO("DOAX2 native60 held counter: site={} calls={} held={} "
                  "value={}", name, calls, held,
                  int32_t(REX_LOAD_U32(addresses[0])));
  }
};

// Tug of War CPU (camera mode 12): 826A1FD8 runs 826A2670(player) for CPU
// girls each update (826A2220 for the human). In the pulling states (player
// state +62 == 4/5) it adds a random per-character amount to the pull
// accumulator 83957930 + p*16 + 4 and pulls each time it passes 30 (player
// state +48 = pull value, else 1.0); it also rolls the CPU's other random
// decisions (826A2818/826A2B68). At 60 Hz the CPU pulled twice as often
// (user, log 265). Run it on alternate updates per CPU girl; on skipped
// updates keep +48 at the no-pull value so a pull is never applied twice.
REX_EXTERN(__imp__sub_826A2670);
REX_HOOK_RAW(sub_826A2670) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_826A2670");
  const uint32_t player = ctx.r3.u32 & 0xFF;
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f || player >= 4) {
    __imp__sub_826A2670(ctx, base);
    return;
  }
  static std::array<bool, 4> skip{};
  static uint64_t skipped = 0;
  skip[player] = !skip[player];
  if (!skip[player]) {
    __imp__sub_826A2670(ctx, base);
    return;
  }
  const uint32_t state = REX_LOAD_U32(0x83957C80 + player * 64);
  if (state >= 0x40000000 && state < 0xA0000000) {
    const uint8_t phase = REX_LOAD_U8(state + 62);
    // 82063DE0 is the 1.0f constant 826A2670 stores when it does not pull.
    if ((phase == 4 || phase == 5) && Native60LoadFloat(base, 0x82063DE0) == 1.0f)
      REX_STORE_U32(state + 48, std::bit_cast<uint32_t>(1.0f));
  }
  if (++skipped <= 4 || skipped % 1800 == 0)
    REXLOG_INFO("DOAX2 native60 tug of war CPU paced: skipped={} player={}", skipped, player);
}

// Niki Marina jet controls (logs 281/282/283): 82631908(rec) runs once per
// update for every racer (records 834CD960 + 0x344*k) and smooths the
// controls with per-update constants, so at the 60 Hz update rate they
// respond twice as fast in real time (the "overturning" at 60 FPS):
// - +796 (int) steering eases toward the stick target by a fixed fraction
//   per update (rate at 821CB088 when growing, 82200B40 when easing back),
//   +780 = +796 * K; rebuilt here from a fractional state with the
//   per-update fraction 1 - sqrt(1 - rate) (same response per second).
// - +744 (shoulder lean) moves/decays by fixed amounts per update: halved.
// - +672/+676 (frames held, thresholds 30/80): held on alternate updates.
// - 82631818: +784 (int) throttle +[788] while accelerating, -8 otherwise
//   per update, +772/+776 derived: the step is halved (remainder carried).
// Bit 0/1 of +824 (forced values from +816/+820) are left untouched.
inline bool Native60JetControlsHalf() {
  return float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f;
}
inline int Native60JetRecordIndex(uint32_t record) {
  if (record < 0x834CD960) return -1;
  const uint32_t offset = record - 0x834CD960;
  if (offset % 0x344 || offset / 0x344 >= 8) return -1;
  return int(offset / 0x344);
}
REX_HOOK_RAW(sub_82631818) {
  const uint32_t record = ctx.r3.u32;
  const int index = Native60JetRecordIndex(record);
  if (!Native60JetControlsHalf() || index < 0 || (REX_LOAD_U32(record + 824) & 1)) {
    __imp__sub_82631818(ctx, base);
    return;
  }
  static std::array<int32_t, 8> carry{};
  const int32_t before = int32_t(REX_LOAD_U32(record + 784));
  __imp__sub_82631818(ctx, base);
  const int32_t after = int32_t(REX_LOAD_U32(record + 784));
  const int32_t limit = int32_t(REX_LOAD_U32(record + 792));
  const int32_t total = after - before + carry[index];
  const int32_t half = total >= 0 ? total / 2 : -((-total + 1) / 2);
  carry[index] = total - half * 2;
  int32_t value = before + half;
  if (value < 0) value = 0;
  if (value > limit) value = limit;
  if (value == 0 || value == limit) carry[index] = 0;
  REX_STORE_U32(record + 784, uint32_t(value));
  REX_STORE_U32(record + 772, std::bit_cast<uint32_t>(float(value) * Native60LoadFloat(base, 0x823EBAEC)));
  if (limit) REX_STORE_U32(record + 776, std::bit_cast<uint32_t>(float(value) / float(limit)));
}
REX_HOOK_RAW(sub_82631908) {
  const uint32_t record = ctx.r3.u32;
  const int index = Native60JetRecordIndex(record);
  if (index == 0) {
    static uint64_t last_frame = 0;
    const uint64_t frame = Doax2EngineFrameCount();
    if (frame - last_frame > 120)
      REXLOG_INFO("DOAX2 native60 jet controls start: engine_frame={} tick_scale={}", frame,
                  float(REXCVAR_GET(doax2_native60_tick_scale)));
    last_frame = frame;
  }
  if (!Native60JetControlsHalf() || index < 0) {
    __imp__sub_82631908(ctx, base);
    return;
  }
  static std::array<double, 8> steer{};
  static std::array<bool, 8> hold{};
  static uint64_t calls = 0;
  const bool forced = (REX_LOAD_U32(record + 824) & 2) != 0;
  const int32_t before796 = int32_t(REX_LOAD_U32(record + 796));
  const float before744 = std::bit_cast<float>(REX_LOAD_U32(record + 744));
  const uint32_t before672 = REX_LOAD_U32(record + 672), before676 = REX_LOAD_U32(record + 676);
  __imp__sub_82631908(ctx, base);
  hold[index] = !hold[index];
  if (hold[index]) {
    REX_STORE_U32(record + 672, before672);
    REX_STORE_U32(record + 676, before676);
  }
  const float after744 = std::bit_cast<float>(REX_LOAD_U32(record + 744));
  if (std::isfinite(after744) && std::isfinite(before744))
    REX_STORE_U32(record + 744, std::bit_cast<uint32_t>(before744 + (after744 - before744) * 0.5f));
  if (forced) {
    steer[index] = double(int32_t(REX_LOAD_U32(record + 796)));
    return;
  }
  // Target exactly as 82631908 computes it (stick x at +740, lean +744).
  const float stick = std::bit_cast<float>(REX_LOAD_U32(record + 740));
  const float target = float(float(std::fma(double(stick), double(Native60LoadFloat(base, 0x821CE820)),
                                             double(float(after744 * Native60LoadFloat(base, 0x821CD678))))) *
                             Native60LoadFloat(base, 0x823EBF24));
  if (!std::isfinite(target)) return;
  double& state = steer[index];
  if (std::fabs(state - double(before796)) >= 1.0) state = double(before796);
  const float rate = std::fabs(target) > float(std::abs(before796)) ? Native60LoadFloat(base, 0x821CB088)
                                                                      : Native60LoadFloat(base, 0x82200B40);
  if (!(rate > 0.0f && rate <= 1.0f)) return;
  const double round = double(Native60LoadFloat(base, 0x820BD90C));
  // The original stops short of the target where one step rounds back to
  // the current value (full lock 157 for target 159.9, not 160); stop at
  // the same integer so full lock and the turn it produces match.
  const double original = double(before796) + (double(target) - double(before796)) * double(rate);
  const int32_t original_next = int32_t(original >= 0.0 ? original + round : original - round);
  int32_t value;
  if (original_next == before796) {
    state = double(before796);
    value = before796;
  } else {
    // Turn-in (growing toward the target) is 30% quicker than the original
  // by request; easing back keeps the original response.
  const bool growing = std::fabs(target) > float(std::abs(before796));
  const double response = std::min(0.95, double(rate) * (growing ? 1.3 : 1.0));
  state += (double(target) - state) * (1.0 - std::sqrt(1.0 - response));
    value = int32_t(state >= 0.0 ? state + round : state - round);
    // Never step past where the original step would land.
    if ((original_next > before796 && value > original_next) ||
        (original_next < before796 && value < original_next)) {
      value = original_next;
      state = double(value);
    }
  }
  REX_STORE_U32(record + 796, uint32_t(value));
  REX_STORE_U32(record + 780, std::bit_cast<uint32_t>(float(value) * Native60LoadFloat(base, 0x823EBF20)));
  if (++calls <= 4 || calls % 3600 == 0)
    REXLOG_INFO("DOAX2 native60 jet controls paced: calls={} racer={} steer {}->{} target={:.2f} rate={:.4f} round={:.3f}",
                calls, index, before796, value, target, rate, round);
}

// Niki Marina water (logs 280/284): 82659568 is called once per update for
// each of the 4 water patches (834F7ED0 + 160*p) and runs the height-field
// step 826572A0, which reads buffer [(idx+1)&1] and writes buffer [idx]
// (patch +8/+12, 256x256 cells of 8 bytes, idx = 834F7EA8 toggles every
// update). Its update map has per-update constants (only the restoring
// term uses the game step) and a mode that flips sign every update; at 60
// updates per second that mode shows as an every-other-frame bob of the
// rider (player0+16 alternation correlates -0.80 with idx) and the camera
// following it, and the water animates twice as fast. Here the patch steps
// on alternate updates at the original step (1.0): after a step the new
// state S1 is kept host-side and the buffer the next update reads gets the
// midpoint of S0 and S1; on the skipped update S1 goes into the buffer the
// following step reads. Readers see S0, mid, S1, mid, ... (constant within
// an update since a step only writes the buffer not read that update).
namespace native60_water {
constexpr uint32_t kCells = 256 * 256;
constexpr uint32_t kWords = kCells * 2;
struct Patch {
  bool stepped = false;  // last call ran the step (next call is the skip)
  int32_t last_index = -1;
  std::vector<uint32_t> next;
};
inline std::array<Patch, 4> patches;
// Patches step on worker threads in parallel: per-thread flags, atomic
// counters, and no writes to shared globals (the game step 833B84CC is read
// by everything else; forcing it to 1.0 during a step leaked double speed
// into animations on other threads, log 287).
inline thread_local bool suppress = false;
inline std::atomic<uint64_t> steps{0}, skips{0}, resyncs{0};
}  // namespace native60_water
REX_HOOK_RAW(sub_82657020) {
  if (native60_water::suppress) return;
  __imp__sub_82657020(ctx, base);
}
REX_HOOK_RAW(sub_82657B60) {
  if (native60_water::suppress) return;
  __imp__sub_82657B60(ctx, base);
}
REX_HOOK_RAW(sub_826572A0) {
  if (native60_water::suppress) return;
  __imp__sub_826572A0(ctx, base);
}
REX_HOOK_RAW(sub_82659568) {
  using namespace native60_water;
  const uint32_t racer = ctx.r3.u32;
  // Same selection as 82659568: mode 2..5 -> table {2,3,0,1} + 834F7EBC.
  const bool active = Native60JetControlsHalf() && REX_LOAD_U32(0x833B7454) == 1 &&
                      REX_LOAD_U8(0x8342D1FA) != 0 && REX_LOAD_U8(0x833B74B1) == 0;
  const uint32_t mode = active ? REX_LOAD_U32(racer + 32) : 0;
  if (!active || mode < 2 || mode > 5) {
    __imp__sub_82659568(ctx, base);
    return;
  }
  static constexpr int32_t kTable[4] = {2, 3, 0, 1};
  const int32_t sum = kTable[mode - 2] + int32_t(REX_LOAD_U32(0x834F7EBC));
  const uint32_t index = uint32_t(((sum % 4) + 4) % 4);
  const uint32_t patch_address = 0x834F7ED0 + index * 160;
  Patch& patch = patches[index];
  const int32_t idx = int32_t(REX_LOAD_U32(0x834F7EA8)) & 1;
  const uint32_t write_buffer = REX_LOAD_U32(patch_address + 8 + uint32_t(idx) * 4);
  const uint32_t read_buffer = REX_LOAD_U32(patch_address + 8 + uint32_t(idx ^ 1) * 4);
  const bool sane = write_buffer >= 0x40000000 && read_buffer >= 0x40000000 &&
                    write_buffer != read_buffer;
  const bool in_sequence = sane && patch.last_index >= 0 && idx != patch.last_index;
  patch.last_index = idx;
  if (patch.stepped && in_sequence && patch.next.size() == kWords) {
    // Skipped update: store S1 where the next step reads it; leave the
    // reader buffer (midpoint) untouched. 826557B8 and the rest still run.
    std::memcpy(base + write_buffer, patch.next.data(), kWords * 4);
    patch.stepped = false;
    ++skips;
    // Run 82659568 with the step block (82657020 boundary, 826572A0 step,
    // 82657B60 wakes, patch +144 counter) suppressed.
    const uint32_t counter = REX_LOAD_U32(patch_address + 144);
    suppress = true;
    __imp__sub_82659568(ctx, base);
    suppress = false;
    REX_STORE_U32(patch_address + 144, counter);
    return;
  }
  if (patch.stepped && !in_sequence) ++resyncs;
  __imp__sub_82659568(ctx, base);
  if (!sane) return;
  // After the step: S0 in the read buffer, S1 in the write buffer.
  patch.next.resize(kWords);
  uint32_t* s1 = reinterpret_cast<uint32_t*>(base + write_buffer);
  const uint32_t* s0 = reinterpret_cast<const uint32_t*>(base + read_buffer);
  // The step's only game-step term is the restoring force on the second
  // float, (rest[+44] - a0) * step * K[821CD4D8]; at the original rate the
  // step is 1.0, so add the missing (1 - step) part here.
  const float rest = std::bit_cast<float>(REX_LOAD_U32(patch_address + 44));
  const float missing = (1.0f - Native60LoadFloat(base, 0x833B84CC)) * Native60LoadFloat(base, 0x821CD4D8);
  for (uint32_t i = 0; i < kWords; i += 2) {
    const float a0 = std::bit_cast<float>(_byteswap_ulong(s0[i]));
    const float b0 = std::bit_cast<float>(_byteswap_ulong(s0[i + 1]));
    const float a1 = std::bit_cast<float>(_byteswap_ulong(s1[i]));
    const float b1 = std::bit_cast<float>(_byteswap_ulong(s1[i + 1])) + (rest - a0) * missing;
    patch.next[i] = s1[i];
    patch.next[i + 1] = _byteswap_ulong(std::bit_cast<uint32_t>(b1));
    s1[i] = _byteswap_ulong(std::bit_cast<uint32_t>((a0 + a1) * 0.5f));
    s1[i + 1] = _byteswap_ulong(std::bit_cast<uint32_t>((b0 + b1) * 0.5f));
  }
  patch.stepped = true;
  const uint64_t step_count = ++steps;
  if (step_count <= 4 || step_count % 3600 == 0)
    REXLOG_INFO("DOAX2 native60 jet water paced: steps={} skips={} resyncs={} patch={} idx={} "
                "buffers {:08X}/{:08X}",
                step_count, skips.load(), resyncs.load(), index, idx, write_buffer, read_buffer);
}

// Jet race state machine (logs 295/296): 82640B70(racer) runs once per
// update for each racer record 834D8CA0 + 44k (+16 state). +20 counts
// frames in the state in steps of 3 per update (90 per second at 30 Hz;
// thresholds 15/150/630: the intro ends at 630 = 210 updates, 7 s at 30
// but 3.5 s at 60), and in state 1 (READY/GO) +24 counts down by 3 per
// update with the digit +28 = +24/90 + 1 (race camera -> controls 2.94 s
// at 30, 1.57 s at 60). Halve both: steps of 1 and 2 alternately.
REX_HOOK_RAW(sub_82640B70) {
  const uint32_t racer = ctx.r3.u32;
  const bool half = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f;
  const uint32_t offset = racer - 0x834D8CA0;
  if (!half || racer < 0x834D8CA0 || offset % 44 || offset / 44 >= 4) {
    __imp__sub_82640B70(ctx, base);
    return;
  }
  const uint32_t k = offset / 44;
  static std::array<bool, 4> frame_parity{}, count_parity{};
  static uint64_t paced = 0;
  const int32_t state = int32_t(REX_LOAD_U32(racer + 16));
  const int32_t frames = int32_t(REX_LOAD_U32(racer + 20));
  const int32_t count = int32_t(REX_LOAD_U32(racer + 24));
  __imp__sub_82640B70(ctx, base);
  const int32_t state_after = int32_t(REX_LOAD_U32(racer + 16));
  if (state_after == state && int32_t(REX_LOAD_U32(racer + 20)) == frames + 3) {
    frame_parity[k] = !frame_parity[k];
    REX_STORE_U32(racer + 20, uint32_t(frames + (frame_parity[k] ? 1 : 2)));
  }
  if (state == 1 && state_after == 1 && int32_t(REX_LOAD_U32(racer + 24)) == count - 3) {
    count_parity[k] = !count_parity[k];
    const int32_t value = count - (count_parity[k] ? 1 : 2);
    REX_STORE_U32(racer + 24, uint32_t(value));
    int32_t digit = value / 3 / 30 + 1;
    if (digit > 3) digit = 3;
    if (digit < 0) digit = 0;
    REX_STORE_U32(racer + 28, uint32_t(digit));
  }
  if (++paced <= 4 || paced % 3600 == 0)
    REXLOG_INFO("DOAX2 native60 jet race state paced: calls={} racer={} state={} frames={} count={}",
                paced, k, state_after, int32_t(REX_LOAD_U32(racer + 20)),
                int32_t(REX_LOAD_U32(racer + 24)));
}

// Character model update 82513930(character) (log 308: writes the girls'
// rig matrices incl. the jet-ski steering pole via 82514BC8/825153A0). It
// steps three per-update blend counters with 30 Hz constants: +378 (u16)
// frames toward +376 with +372 = +378/+376, +400 (u8) +3 toward 30 / -10,
// +401 (u8) +5 toward 30 / -10. At 60 updates per second those blends ran
// twice as fast: the steering pole dropped 1.8 s before the girls' hands
// in the jet intro (logs 306/307). During the jet race the steps are
// halved (remainders carried).
REX_HOOK_RAW(sub_82513930) {
  const uint32_t character = ctx.r3.u32;
  Native60NoteCharacter(character);
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f || !Native60JetRaceActive()) {
    __imp__sub_82513930(ctx, base);
    return;
  }
  struct Carry { int32_t frames = 0, blend_a = 0, blend_b = 0; };
  static std::map<uint32_t, Carry> carries;
  static uint64_t paced = 0;
  const uint16_t total = REX_LOAD_U16(character + 376);
  const uint16_t frames = REX_LOAD_U16(character + 378);
  const uint8_t blend_a = REX_LOAD_U8(character + 400);
  const uint8_t blend_b = REX_LOAD_U8(character + 401);
  __imp__sub_82513930(ctx, base);
  Carry& carry = carries[character];
  auto halve = [](int32_t delta, int32_t& rest) {
    const int32_t sum = delta + rest;
    const int32_t half = sum >= 0 ? sum / 2 : -((-sum) / 2);
    rest = sum - half * 2;
    return half;
  };
  bool changed = false;
  const uint16_t frames_after = REX_LOAD_U16(character + 378);
  if (REX_LOAD_U16(character + 376) == total && frames_after == frames + 1) {
    const uint16_t value = uint16_t(frames + halve(1, carry.frames));
    REX_STORE_U16(character + 378, value);
    if (total)
      REX_STORE_U32(character + 372, std::bit_cast<uint32_t>(float(value) / float(total)));
    changed = true;
  }
  const int32_t a_delta = int32_t(REX_LOAD_U8(character + 400)) - int32_t(blend_a);
  if (a_delta) {
    int32_t value = blend_a + halve(a_delta, carry.blend_a);
    value = std::clamp(value, 0, 30);
    REX_STORE_U8(character + 400, uint8_t(value));
    changed = true;
  }
  const int32_t b_delta = int32_t(REX_LOAD_U8(character + 401)) - int32_t(blend_b);
  if (b_delta) {
    int32_t value = blend_b + halve(b_delta, carry.blend_b);
    value = std::clamp(value, 0, 30);
    REX_STORE_U8(character + 401, uint8_t(value));
    changed = true;
  }
  if (changed && (++paced <= 6 || paced % 3600 == 0))
    REXLOG_INFO("DOAX2 native60 character blend paced: calls={} character={:08X} frames {}->{} / {} "
                "blendA {}->{} blendB {}->{}",
                paced, character, frames, REX_LOAD_U16(character + 378), total, blend_a,
                REX_LOAD_U8(character + 400), blend_b, REX_LOAD_U8(character + 401));
}

// Rider attachment smoothing (logs 318-320, F10 bursts 305/317): 825153A0
// (player) poses the jet-ski steering pole / rider attachment from the
// girl's bones in block 833B5310 + 2048p and keeps smoothed rotations that
// 8262C8C0 (slerp(a, b, t), t = weight of b) blends once per update with
// constant factors: +1792 = slerp(new, +1792, t) and +1856 likewise
// (retention t), +768 = slerp(+768, target, t) and +384 likewise (weight
// toward the target). At 60 updates per second the pole eased down in half
// the real time and left the girls' hands in the jet intro (bone easing
// 74 vs 143 samples). During the jet race the factors are converted to the
// same response per second: retention sqrt(t), weight 1 - sqrt(1 - t).
inline thread_local uint32_t native60_attachment_block = 0;
REX_HOOK_RAW(sub_825153A0) {
  const bool active = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f && Native60JetRaceActive();
  const uint32_t previous = native60_attachment_block;
  native60_attachment_block = active ? 0x833B5310 + ((ctx.r3.u32 & 0xFF) << 11) : 0;
  __imp__sub_825153A0(ctx, base);
  native60_attachment_block = previous;
}
REX_HOOK_RAW(sub_8262C8C0) {
  const uint32_t block = native60_attachment_block;
  if (block) {
    const uint32_t out = ctx.r3.u32;
    const uint32_t offset = out - block;
    const double t = ctx.f1.f64;
    static uint64_t converted = 0;
    if (t > 0.0 && t < 1.0) {
      if ((offset == 1792 || offset == 1856) && ctx.r5.u32 == out) {
        ctx.f1.f64 = double(float(std::sqrt(t)));
        if (++converted <= 4) REXLOG_INFO("DOAX2 native60 pole smoothing: retention {:.4f} -> {:.4f}", t, ctx.f1.f64);
      } else if ((offset == 768 || offset == 384) && ctx.r4.u32 == out) {
        ctx.f1.f64 = double(float(1.0 - std::sqrt(1.0 - t)));
        if (++converted <= 4) REXLOG_INFO("DOAX2 native60 pole smoothing: weight {:.4f} -> {:.4f}", t, ctx.f1.f64);
      }
    }
  }
  __imp__sub_8262C8C0(ctx, base);
}

// Per-jet update 82630458(record 834CD960 + 0x344k) (logs 322/323): frame
// counters with 30 Hz steps per update: +68 (frames in jet state, +3),
// +664 and +668 (+3; +664 reaches 480 when the girls grab the lowering
// steering pole at 5.3 s into the intro at 30, 2.7 s at 60), +728 (+3),
// +660 (+1), +720 and +88 (-1 countdowns). +604 (input history ring) is
// left alone. During the jet race each regular step is halved (1/2
// alternately for 3, carried remainder for 1); resets and jumps pass.
REX_HOOK_RAW(sub_82630458) {
  if (Native60IntroBisectSkip(base, 1)) return;
  const uint32_t record = ctx.r3.u32;
  const int index = Native60JetRecordIndex(record);
  if (!Native60JetControlsHalf() || index < 0) {
    __imp__sub_82630458(ctx, base);
    return;
  }
  struct Counter { uint32_t offset; int32_t step; };
  // +724 (-1 to 0) drives the rider/steering-pole blend in jet state 2
  // (82643028: body+104 = lerp by int(+724) * k): unpaced, the pole dropped
  // ~1.7 s early in the intro (logs 324-326).
  static constexpr std::array<Counter, 8> kCounters = {{{68, 3}, {664, 3}, {668, 3}, {728, 3},
                                                        {660, 1}, {720, -1}, {724, -1}, {88, -1}}};
  static std::array<std::array<int32_t, 8>, 8> carry{};
  std::array<int32_t, 8> before{};
  for (size_t i = 0; i < kCounters.size(); ++i)
    before[i] = int32_t(REX_LOAD_U32(record + kCounters[i].offset));
  __imp__sub_82630458(ctx, base);
  static uint64_t paced = 0;
  for (size_t i = 0; i < kCounters.size(); ++i) {
    const int32_t after = int32_t(REX_LOAD_U32(record + kCounters[i].offset));
    if (after - before[i] != kCounters[i].step) continue;
    const int32_t sum = kCounters[i].step + carry[index][i];
    const int32_t half = sum >= 0 ? sum / 2 : -((-sum) / 2);
    carry[index][i] = sum - half * 2;
    REX_STORE_U32(record + kCounters[i].offset, uint32_t(before[i] + half));
    ++paced;
  }
  if (paced && (paced <= 7 || paced % 20000 == 0))
    REXLOG_INFO("DOAX2 native60 jet counters paced: steps={} racer={} +68={} +664={} +668={}", paced, index,
                int32_t(REX_LOAD_U32(record + 68)), int32_t(REX_LOAD_U32(record + 664)),
                int32_t(REX_LOAD_U32(record + 668)));
}

// Rider update 82643028 (all racers): body record 834D99C0 + 208k, +132
// accumulates trunc(step * a + b) per update (a = 2, b = 1: 3 per update
// at 30, 2 at 60 = 1.33x real time, logs 322/323). Pace it to 1.5 per
// update (1 and 2 alternately) during the jet race.
REX_HOOK_RAW(sub_82643028) {
  if (Native60IntroBisectSkip(base, 2)) return;
  if (!Native60JetControlsHalf() || !Native60JetRaceActive()) {
    __imp__sub_82643028(ctx, base);
    return;
  }
  std::array<int32_t, 4> before{};
  for (uint32_t k = 0; k < 4; ++k) before[k] = int32_t(REX_LOAD_U32(0x834D99C0 + 208 * k + 132));
  __imp__sub_82643028(ctx, base);
  static std::array<bool, 4> parity{};
  for (uint32_t k = 0; k < 4; ++k) {
    const uint32_t address = 0x834D99C0 + 208 * k + 132;
    if (int32_t(REX_LOAD_U32(address)) - before[k] != 2) continue;
    parity[k] = !parity[k];
    REX_STORE_U32(address, uint32_t(before[k] + (parity[k] ? 1 : 2)));
  }
}

// Jet pole animation 82633FE0 (r3 = record [82B498D4] + 2528k): +1260 is
// the steering pole's animation frame, advanced by trunc(a * step + b)
// per update (3 at 30, 2 at 60 = 1.33x real time, so the pole lowers out
// of the girls' hands in the Niki Marina intro; it drives body+104 via
// 82643028). Pace it to 1.5 per update (1 and 2 alternately).
REX_HOOK_RAW(sub_82633FE0) {
  const uint32_t record = ctx.r3.u32;
  if (!Native60JetControlsHalf() || !Native60JetRaceActive() || !record) {
    __imp__sub_82633FE0(ctx, base);
    return;
  }
  // Two paths: +1260 += trunc(a * step + b), or (intro) +1252 += +1256
  // and +1260 = +1252. The rounded step is always 3 per call at 30 but 2
  // (sometimes 3, from step jitter) at 60, so every advance is replaced by
  // exactly 1.5 per call (1 and 2 alternately), the 30 FPS rate in real time.
  const float before60 = Native60LoadFloat(base, record + 1260);
  const float before52 = Native60LoadFloat(base, record + 1252);
  __imp__sub_82633FE0(ctx, base);
  const float after60 = Native60LoadFloat(base, record + 1260);
  const float after52 = Native60LoadFloat(base, record + 1252);
  static std::array<double, 8> carry60{}, carry52{};
  const uint32_t k = REX_LOAD_U32(record + 0) & 7;
  static std::atomic<uint32_t> logged{0};
  auto pace = [](double& carry, float before, float after) {
    (void)after;
    carry += 1.5;
    const double step = std::floor(carry);
    carry -= step;
    return before + float(step);
  };
  const float d52 = after52 - before52;
  if (d52 >= 1.0f && d52 <= 4.0f && d52 == std::floor(d52)) {
    const float paced = pace(carry52[k], before52, after52);
    REX_STORE_U32(record + 1252, std::bit_cast<uint32_t>(paced));
    if (after60 == after52) REX_STORE_U32(record + 1260, std::bit_cast<uint32_t>(paced));
    if (logged.fetch_add(1) < 8)
      REXLOG_INFO("DOAX2 native60 jet pole frame paced (+1252): racer={} {} -> {} (raw {})", k, before52, paced,
                  after52);
    return;
  }
  const float d60 = after60 - before60;
  if (!(d60 >= 1.0f && d60 <= 4.0f && d60 == std::floor(d60))) return;
  const float paced = pace(carry60[k], before60, after60);
  REX_STORE_U32(record + 1260, std::bit_cast<uint32_t>(paced));
  if (logged.fetch_add(1) < 8)
    REXLOG_INFO("DOAX2 native60 jet pole frame paced (+1260): racer={} {} -> {} (raw {})", k, before60, paced,
                after60);
}

// Niki Marina intro bisect (diagnostics, doax2_native60_intro_bisect): the
// selected group runs only on alternate updates while racer 0 is in race
// state 0 (intro). Whichever group brings the steering pole back in sync
// with the girls' hands contains the unpaced per-update logic.
inline bool Native60IntroBisectSkip(uint8_t* base, int group) {
  if (REXCVAR_GET(doax2_native60_intro_bisect) != group) return false;
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f || !Native60JetRaceActive()) return false;
  if (REX_LOAD_U32(0x834D8CA0 + 16) != 0) return false;
  return (Doax2EngineFrameCount() & 1) != 0;
}
#define DOAX2_NATIVE60_BISECT_HOOK(name, group)                     \
  REX_EXTERN(__imp__##name);                                        \
  REX_HOOK_RAW(name) {                                              \
    if (Native60IntroBisectSkip(base, group)) return;                     \
    __imp__##name(ctx, base);                                       \
  }
DOAX2_NATIVE60_BISECT_HOOK(sub_8263B1A0, 3)
DOAX2_NATIVE60_BISECT_HOOK(sub_8263C310, 3)
DOAX2_NATIVE60_BISECT_HOOK(sub_82642E08, 4)
DOAX2_NATIVE60_BISECT_HOOK(sub_826448A8, 4)
DOAX2_NATIVE60_BISECT_HOOK(sub_8263D340, 4)
DOAX2_NATIVE60_BISECT_HOOK(sub_82638CE0, 4)
DOAX2_NATIVE60_BISECT_HOOK(sub_826328D8, 4)
DOAX2_NATIVE60_BISECT_HOOK(sub_82513158, 5)

// Boot FMV skip (all modes): CRI Sofdec MWSFD 3.97. 829BF270 =
// mwPlyStartFname(handle, fname), 829BD948 = mwPlyGetStat(handle) (status
// 2 = playing, 3 = play end). Boot sequence: ninja_vi_hd.sfd (Team Ninja
// logo) then promotion_video.sfd. While one of those (or opening.sfd) plays, Start or A (any pad), Enter or Space makes the status
// query report play end once, so the game moves on as if the movie ended.
namespace native60_fmv {
inline std::mutex mutex;
inline std::map<uint32_t, std::string> names;  // handle -> file name
inline std::map<uint32_t, std::chrono::steady_clock::time_point> started;
inline std::set<uint32_t> skipped;
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, void*);
inline bool SkipPressed() {
  // The pads exactly as the guest sees them (SDL drivers: any controller).
  if (rex::Runtime* runtime = rex::Runtime::instance()) {
    if (auto* input = dynamic_cast<rex::input::InputSystem*>(runtime->input_system())) {
      for (uint32_t user = 0; user < 4; ++user) {
        rex::input::X_INPUT_STATE state{};
        if (input->GetState(user, &state) == 0 &&
            (uint16_t(state.gamepad.buttons) & (0x0010 | 0x1000)))  // START | A
          return true;
      }
    }
  }
#if defined(_WIN32)
  static XInputGetStateFn get_state = [] {
    HMODULE module = LoadLibraryA("xinput1_4.dll");
    if (!module) module = LoadLibraryA("xinput1_3.dll");
    return module ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState")) : nullptr;
  }();
  if (get_state) {
    for (DWORD pad = 0; pad < 4; ++pad) {
      struct { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } state{};
      if (get_state(pad, &state) == 0 && (state.buttons & (0x0010 | 0x1000))) return true;  // START | A
    }
  }
  const HWND foreground = GetForegroundWindow();
  DWORD process = 0;
  GetWindowThreadProcessId(foreground, &process);
  if (process == GetCurrentProcessId() &&
      ((GetAsyncKeyState(VK_RETURN) & 0x8000) || (GetAsyncKeyState(VK_SPACE) & 0x8000)))
    return true;
#endif
  return false;
}
inline bool IsBootMovie(const std::string& name) {
  std::string lower = name;
  for (char& c : lower) c = char(std::tolower(uint8_t(c)));
  return lower.find("ninja_vi_hd") != std::string::npos || lower.find("promotion_video") != std::string::npos ||
         lower.find("opening") != std::string::npos;
}
}  // namespace native60_fmv
REX_EXTERN(__imp__sub_829BF270);
REX_HOOK_RAW(sub_829BF270) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t fname = ctx.r4.u32;
  std::string name;
  if (fname) name.assign(reinterpret_cast<const char*>(base + fname), strnlen(reinterpret_cast<const char*>(base + fname), 255));
  {
    std::lock_guard lock(native60_fmv::mutex);
    native60_fmv::names[handle] = name;
    native60_fmv::started[handle] = std::chrono::steady_clock::now();
    native60_fmv::skipped.erase(handle);
  }
  REXLOG_INFO("DOAX2 movie start: handle={:08X} file={}", handle, name);
  native60_movie_event_ms = Native60NowMs();
  // Movies stop the title music. The game fades it out and stops XMP before
  // every movie, but the keep-song option ignores title stops; stop the host
  // player here so the game starts a fresh playlist after the movie.
  if (rex::audio::GetTitlePlaylistDiagnostics().playing) {
    rex::audio::StopTitlePlaylist();
    REXLOG_INFO("DOAX2 movie start: title music stopped ({})", name);
  }
  __imp__sub_829BF270(ctx, base);
}
REX_EXTERN(__imp__sub_829BD948);
REX_HOOK_RAW(sub_829BD948) {
  const uint32_t handle = ctx.r3.u32;
  __imp__sub_829BD948(ctx, base);
  if (ctx.r3.u32 != 2) return;  // only while playing
  std::string name;
  std::chrono::steady_clock::time_point start;
  {
    std::lock_guard lock(native60_fmv::mutex);
    const auto it = native60_fmv::names.find(handle);
    if (it == native60_fmv::names.end()) return;
    if (native60_fmv::skipped.count(handle)) {
      ctx.r3.u64 = 3;  // keep reporting play end after a skip
      return;
    }
    name = it->second;
    start = native60_fmv::started[handle];
  }
  if (!native60_fmv::IsBootMovie(name)) return;
  // Ignore a button still held from the warning screen.
  static bool released = false;
  const bool pressed = native60_fmv::SkipPressed();
  if (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300)) {
    released = !pressed;
    return;
  }
  if (!pressed) {
    released = true;
    return;
  }
  if (!released) return;
  released = false;
  {
    std::lock_guard lock(native60_fmv::mutex);
    native60_fmv::skipped.insert(handle);
  }
  REXLOG_INFO("DOAX2 movie skipped: {}", name);
  native60_movie_event_ms = Native60NowMs();
  ctx.r3.u64 = 3;  // play end
}

// Butt Battle (camera mode 11): 8269A070(game = 83CAF200) runs once per
// update from the minigame fiber (82699CC0). Its state block counts per
// update (logs 243/245, scan widened to 833-83C): +0x10 frames in phase
// (the intro/READY-GO cut the 180-entry intro shot after 91 updates), +0x70
// a match clock, and the CPU/move cooldowns +0x264, +0x274, +0x284..+0x2A0
// tick 1 per update while active, so the whole minigame and the CPU ran at
// double speed. Hold every other +-1 step of each (per counter), never a
// step that reaches zero (an action due at zero must fire exactly once).
REX_EXTERN(__imp__sub_8269A070);
REX_HOOK_RAW(sub_8269A070) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_8269A070");
  constexpr std::array<uint32_t, 12> kOffsets = {0x10, 0x70, 0x264, 0x274, 0x284, 0x288,
                                                 0x28C, 0x290, 0x294, 0x298, 0x29C, 0x2A0};
  const uint32_t game = ctx.r3.u32;
  const bool scaled = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f &&
                      game >= 0x40000000 && game < 0xA0000000;
  std::array<uint32_t, kOffsets.size()> before{};
  if (scaled)
    for (size_t i = 0; i < kOffsets.size(); ++i) before[i] = REX_LOAD_U32(game + kOffsets[i]);
  const int32_t phase_pre = game >= 0x40000000 && game < 0xA0000000
                                ? int32_t(REX_LOAD_U32(game + 0x10)) : 0;
  __imp__sub_8269A070(ctx, base);
  if (REXCVAR_GET(doax2_native60_counter_scan) && game >= 0x40000000 && game < 0xA0000000) {
    // Timeline (diagnostics): game state (+4) and each girl's battle record
    // action fields (+128/+132/+136 of game + 208*p) with wall time, to
    // compare durations against the 30 FPS reference mode.
    static const auto start = std::chrono::steady_clock::now();
    static std::array<uint32_t, 7> last{};
    static uint64_t lines = 0;
    const std::array<uint32_t, 7> now = {
        REX_LOAD_U32(game + 4),         REX_LOAD_U32(game + 128),
        REX_LOAD_U32(game + 132),       REX_LOAD_U32(game + 136),
        REX_LOAD_U32(game + 208 + 128), REX_LOAD_U32(game + 208 + 132),
        REX_LOAD_U32(game + 208 + 136)};
    if (now != last && ++lines <= 4000) {
      const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      const uint32_t table = 0x82AC1CC8 + now[0] * 12;
      REXLOG_INFO("DOAX2 native60 butt battle timeline: t={:.3f} state={} p0=({},{},{}) "
                  "p1=({},{},{}) phase_pre={} phase_frames={} clock={} handlers={:08X}/{:08X}/{:08X} "
                  "tick_scale={} engine_frame={}",
                  t, now[0], now[1], now[2], now[3], now[4], now[5], now[6], phase_pre,
                  int32_t(REX_LOAD_U32(game + 0x10)), int32_t(REX_LOAD_U32(game + 0x70)),
                  REX_LOAD_U32(table), REX_LOAD_U32(table + 4), REX_LOAD_U32(table + 8),
                  float(REXCVAR_GET(doax2_native60_tick_scale)), Doax2EngineFrameCount());
      last = now;
    }
  }
  if (!scaled) return;
  static std::array<bool, kOffsets.size()> hold{};
  static uint64_t calls = 0, held = 0;
  for (size_t i = 0; i < kOffsets.size(); ++i) {
    const uint32_t now = REX_LOAD_U32(game + kOffsets[i]);
    const int32_t delta = int32_t(now - before[i]);
    if (delta != 1 && !(delta == -1 && now != 0)) continue;
    hold[i] = !hold[i];
    if (!hold[i]) continue;
    REX_STORE_U32(game + kOffsets[i], before[i]);
    ++held;
  }
  if (++calls <= 4 || calls % 1800 == 0)
    REXLOG_INFO("DOAX2 native60 butt battle counters held: calls={} held={} phase_frames={} clock={}",
                calls, held, int32_t(REX_LOAD_U32(game + 0x10)), int32_t(REX_LOAD_U32(game + 0x70)));
}

// Volleyball phase countdown/count-up 839528E4: 82688388 decrements it and
// ends the between-point/win sequence at zero (poses were cut at ~60% of
// their authored length in log 152); 826886D0 counts it up during play.
Native60HeldCounters<1> native60_phase_countdown{"82688388", {0x839528E4}};
Native60HeldCounters<1> native60_phase_countup{"826886D0", {0x839528E4}};
REX_EXTERN(__imp__sub_82688388);
REX_HOOK_RAW(sub_82688388) {
  const bool active = native60_phase_countdown.Begin(base);
  __imp__sub_82688388(ctx, base);
  if (active) native60_phase_countdown.End(base);
}
REX_EXTERN(__imp__sub_826886D0);
REX_HOOK_RAW(sub_826886D0) {
  const bool active = native60_phase_countup.Begin(base);
  __imp__sub_826886D0(ctx, base);
  if (active) native60_phase_countup.End(base);
}

// Volleyball phase machine 82687C20 (phase record 839528D8: +4 current,
// +8 requested, handler table 81AE4A10) increments frames-in-phase
// 839525B4 every update; phase handlers compare it with authored 30 Hz
// durations (intro 9.8 s originally, 6.8 s at 60 Hz in log 154).
// 82687B38 advances the match clock 839525C0 the same way.
Native60HeldCounters<1> native60_phase_frames{"82687C20", {0x839525B4}};
Native60HeldCounters<1> native60_match_clock{"82687B38", {0x839525C0}};
REX_EXTERN(__imp__sub_82687C20);
REX_HOOK_RAW(sub_82687C20) {
  Native60MarkMinigameUpdate();
  const bool active = native60_phase_frames.Begin(base);
  const uint32_t phase_before = REX_LOAD_U32(0x839528D8 + 4);
  const uint32_t frames_before = REX_LOAD_U32(0x839525B4);
  __imp__sub_82687C20(ctx, base);
  if (active) native60_phase_frames.End(base);
  if (REXCVAR_GET(doax2_native60_counter_scan) &&
      REX_LOAD_U32(0x839528D8 + 4) != phase_before)
    REXLOG_INFO("DOAX2 native60 volley phase: {} -> {} after_frames={} "
                "match_phase={} engine_frame={}", int32_t(phase_before),
                int32_t(REX_LOAD_U32(0x839528D8 + 4)), int32_t(frames_before),
                REX_LOAD_U8(0x833B745D), Doax2EngineFrameCount());
}
REX_EXTERN(__imp__sub_82687B38);
REX_HOOK_RAW(sub_82687B38) {
  const bool active = native60_match_clock.Begin(base);
  __imp__sub_82687B38(ctx, base);
  if (active) native60_match_clock.End(base);
}

// Spikers start motion 504 late at 60 Hz (hit window at motion frame 23
// opens at ball time 38-39 originally, never before contact at 60 Hz).
// Log every volleyball motion start with its caller and the ball clock.
REX_EXTERN(__imp__sub_8268DAE8);
REX_HOOK_RAW(sub_8268DAE8) {
  const uint32_t state = ctx.r3.u32;
  const uint32_t motion = ctx.r4.u32 & 0xFFFF;
  const uint32_t player = state >= 0x833B8520 && state < 0x833B8520 + 4 * 152
                              ? (state - 0x833B8520) / 152 : 9;
  static uint64_t lines = 0;
  if (REXCVAR_GET(doax2_native60_counter_scan) && motion != 0xFFFF &&
      ++lines <= 6000)
    REXLOG_INFO("DOAX2 native60 motion start: player={} motion={} rate={} "
                "previous={} caller={:08X} ball_t={} planned_T={} "
                "ball_state={} step={} engine_frame={}",
                player, motion, ctx.f1.f64, REX_LOAD_U16(state + 64),
                uint32_t(ctx.lr), Native60LoadFloat(base, kNative60Ball + 7364),
                Native60LoadFloat(base, kNative60Ball + 7388),
                REX_LOAD_U8(kNative60Ball + 7378),
                Native60LoadFloat(base, kNative60GameStep),
                Doax2EngineFrameCount());
  if (player < 4) {
    native60_motion_start_update[player] = Doax2EngineFrameCount();
    native60_queue_target[player] = 0;
  }
  __imp__sub_8268DAE8(ctx, base);
}

// Spike slow motion. 8268B3D8 drives the controller at 83952998 (+1
// active, +2 frame counter, +4 duration, +8 current modifier published to
// the speed modifier 834262A4, +12 per-frame ramp, +16 end value): each
// call publishes +8, then counter++ and +8 += ramp until the duration. At
// 60 Hz the slow motion ramped and ended twice as fast (spikes looked too
// fast). With the base speed scaled, apply half the ramp per call and hold
// the counter on alternate calls so the authored duration is kept.
REX_EXTERN(__imp__sub_8268B3D8);
REX_HOOK_RAW(sub_8268B3D8) {
  constexpr uint32_t kSlowMotion = 0x83952998;
  Native60NoteFiberContext(base, "8268B3D8 slow motion");
  const bool scaled = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f &&
                      !Native60InScriptFiber(base);
  const uint8_t active = REX_LOAD_U8(kSlowMotion + 1);
  const uint16_t counter = REX_LOAD_U16(kSlowMotion + 2);
  const float value = Native60LoadFloat(base, kSlowMotion + 8);
  const float ramp = Native60LoadFloat(base, kSlowMotion + 12);
  __imp__sub_8268B3D8(ctx, base);
  if (!scaled || !active) return;
  if (REX_LOAD_U16(kSlowMotion + 2) != uint16_t(counter + 1) ||
      Native60LoadFloat(base, kSlowMotion + 8) != value + ramp)
    return;  // not a ramp step (start, end or reset)
  REX_STORE_U32(kSlowMotion + 8, std::bit_cast<uint32_t>(value + ramp * 0.5f));
  static bool hold = false;
  hold = !hold;
  if (hold) REX_STORE_U16(kSlowMotion + 2, counter);
  static uint64_t steps = 0;
  if (++steps <= 4 || steps % 600 == 0)
    REXLOG_INFO("DOAX2 native60 spike slow motion paced: steps={} "
                "counter={} duration={} modifier={}", steps,
                REX_LOAD_U16(kSlowMotion + 2), REX_LOAD_U16(kSlowMotion + 4),
                Native60LoadFloat(base, kSlowMotion + 8));
}

// Volleyball intro: 82689E58 advances intro step counter 839528C8 by one
// per call and 82689D88 ends each girl's step at 75 (150 for type 7), i.e.
// 2.5 s at 30 Hz. Results/final phase 82689370 waits on a 60-frame
// countdown at 8341F984 (decremented per call) and counts 839525BC.
Native60HeldCounters<1> native60_intro_step{"82689E58", {0x839528C8}};
Native60HeldCounters<2> native60_results{"82689370", {0x8341F984, 0x839525BC}};
REX_EXTERN(__imp__sub_82689E58);
REX_HOOK_RAW(sub_82689E58) {
  const bool active = native60_intro_step.Begin(base);
  const uint32_t step_before = REX_LOAD_U32(0x839528CC);
  const uint32_t count_before = REX_LOAD_U32(0x839528C8);
  __imp__sub_82689E58(ctx, base);
  if (active) native60_intro_step.End(base);
  // Diagnostic: log each intro step change with the count it ended at.
  if (REXCVAR_GET(doax2_native60_counter_scan) && step_before >= 0x82000000 &&
      REX_LOAD_U32(0x839528CC) >= 0x82000000 &&
      REX_LOAD_U32(0x839528CC) != step_before)
    REXLOG_INFO("DOAX2 native60 intro step: ended_at={} step_type={} "
                "next_type={} done={} engine_frame={}",
                int32_t(count_before), REX_LOAD_U8(step_before),
                REX_LOAD_U8(REX_LOAD_U32(0x839528CC)), ctx.r3.u32,
                Doax2EngineFrameCount());
}
REX_EXTERN(__imp__sub_82689370);
REX_HOOK_RAW(sub_82689370) {
  const bool active = native60_results.Begin(base);
  const uint32_t step_before = REX_LOAD_U32(0x8395059C);
  const uint32_t countdown_before = REX_LOAD_U32(0x8341F984);
  __imp__sub_82689370(ctx, base);
  if (active) native60_results.End(base);
  if (REXCVAR_GET(doax2_native60_counter_scan) &&
      REX_LOAD_U32(0x8395059C) != step_before)
    REXLOG_INFO("DOAX2 native60 results step: {} -> {} countdown={} "
                "frames={} done={} engine_frame={}",
                int32_t(step_before), int32_t(REX_LOAD_U32(0x8395059C)),
                int32_t(countdown_before), int32_t(REX_LOAD_U32(0x839525BC)),
                ctx.r3.u32, Doax2EngineFrameCount());
}

// Queued-motion probe: wrap every volleyball writer of player +66 and log
// which routine queued which motion, with the ball clock and the player's
// per-update frame counters (83947DB0 block) at that moment.
void ReportNative60Queue(uint8_t* base, const char* fn,
                         const std::array<uint16_t, 4>& before) {
  static uint64_t lines = 0;
  for (uint32_t p = 0; p < 4; ++p) {
    const uint32_t state = 0x833B8520 + p * 152;
    const uint16_t now = REX_LOAD_U16(state + 66);
    if (now == before[p] || now == 0xFFFF || ++lines > 6000) continue;
    REXLOG_INFO("DOAX2 native60 queue: fn={} player={} queued={} "
                "current={} frame={} ball_t={} planned_T={} ball_state={} "
                "state_frames={} motion_frames={} set_timer={} step={}",
                fn, p, now, REX_LOAD_U16(state + 64),
                Native60LoadFloat(base, state + 96),
                Native60LoadFloat(base, kNative60Ball + 7364),
                Native60LoadFloat(base, kNative60Ball + 7388),
                REX_LOAD_U8(kNative60Ball + 7378),
                int32_t(REX_LOAD_U32(0x83947DB0 + 12 + 12 * p)),
                REX_LOAD_U16(0x83947DB0 + 18 + 12 * p),
                Native60LoadFloat(base, 0x8394E7FC),
                Native60LoadFloat(base, kNative60GameStep));
  }
}
#define DOAX2_NATIVE60_QUEUE_PROBE(name)                                  \
  REX_EXTERN(__imp__##name);                                              \
  REX_HOOK_RAW(name) {                                                    \
    std::array<uint16_t, 4> before{};                                     \
    const bool probe = REXCVAR_GET(doax2_native60_counter_scan);          \
    if (probe)                                                            \
      for (uint32_t p = 0; p < 4; ++p)                                    \
        before[p] = REX_LOAD_U16(0x833B8520 + p * 152 + 66);              \
    __imp__##name(ctx, base);                                             \
    if (probe) ReportNative60Queue(base, #name, before);                  \
  }
DOAX2_NATIVE60_QUEUE_PROBE(sub_82678990)
DOAX2_NATIVE60_QUEUE_PROBE(sub_82679128)
DOAX2_NATIVE60_QUEUE_PROBE(sub_8267FA40)
DOAX2_NATIVE60_QUEUE_PROBE(sub_8268B890)
DOAX2_NATIVE60_QUEUE_PROBE(sub_8268BEE8)
DOAX2_NATIVE60_QUEUE_PROBE(sub_8268C198)
DOAX2_NATIVE60_QUEUE_PROBE(sub_8268D258)
DOAX2_NATIVE60_QUEUE_PROBE(sub_8268D840)
DOAX2_NATIVE60_QUEUE_PROBE(sub_8268D9D8)
#undef DOAX2_NATIVE60_QUEUE_PROBE

std::array<bool, 4> native60_ai_hold{};
// Shared by the AI scheduler states 82679A30, 82679D28 and 826797F0, which
// all tick the same record's countdowns; one cadence per player.
struct Native60AiCountdowns {
  uint32_t player = 4;
  std::array<uint16_t, 3> before{};
};
Native60AiCountdowns Native60AiCountdownsBegin(uint8_t* base, uint32_t player) {
  Native60AiCountdowns state;
  if (player >= 4 || float(REXCVAR_GET(doax2_native60_tick_scale)) != 0.5f)
    return state;
  Native60NoteFiberContext(base, "AI countdowns");
  if (Native60InScriptFiber(base)) return state;
  state.player = player;
  const uint32_t record = 0x839525C8 + player * 92;
  for (uint32_t i = 0; i < 3; ++i) state.before[i] = REX_LOAD_U16(record + 10 + i * 2);
  return state;
}
void Native60AiCountdownsEnd(uint8_t* base, const Native60AiCountdowns& state,
                             const char* site) {
  if (state.player >= 4) return;
  const uint32_t record = 0x839525C8 + state.player * 92;
  bool ticked = false;
  for (uint32_t i = 0; i < 3; ++i)
    ticked |= REX_LOAD_U16(record + 10 + i * 2) == uint16_t(state.before[i] - 1) &&
              state.before[i] > 0;
  if (!ticked) return;
  bool& hold = native60_ai_hold[state.player];
  hold = !hold;
  if (!hold) return;
  uint32_t held = 0;
  for (uint32_t i = 0; i < 3; ++i) {
    const uint32_t address = record + 10 + i * 2;
    if (state.before[i] > 1 &&
        REX_LOAD_U16(address) == uint16_t(state.before[i] - 1)) {
      REX_STORE_U16(address, state.before[i]);
      ++held;
    }
  }
  static uint64_t calls = 0;
  if (held && (++calls <= 6 || calls % 600 == 0))
    REXLOG_INFO("DOAX2 native60 AI countdown held: site={} player={} "
                "held={} delay10={} delay12={} blend14={}", site, state.player,
                held, REX_LOAD_U16(record + 10), REX_LOAD_U16(record + 12),
                REX_LOAD_U16(record + 14));
}
#define DOAX2_NATIVE60_AI_STATE(name, label)                              \
  REX_EXTERN(__imp__##name);                                              \
  REX_HOOK_RAW(name) {                                                    \
    const auto countdowns = Native60AiCountdownsBegin(base, ctx.r3.u32);  \
    std::array<uint16_t, 4> before{};                                     \
    const bool probe = REXCVAR_GET(doax2_native60_counter_scan);          \
    if (probe)                                                            \
      for (uint32_t p = 0; p < 4; ++p)                                    \
        before[p] = REX_LOAD_U16(0x833B8520 + p * 152 + 66);              \
    __imp__##name(ctx, base);                                             \
    Native60AiCountdownsEnd(base, countdowns, label);                     \
    if (probe) ReportNative60Queue(base, #name, before);                  \
  }
// Approach wait (T - 21 original frames before motion 541) lives here.
DOAX2_NATIVE60_AI_STATE(sub_82679D28, "82679D28")
DOAX2_NATIVE60_AI_STATE(sub_826797F0, "826797F0")
#undef DOAX2_NATIVE60_AI_STATE

// Attack swing timer. 8268CAB0 increments the jump-frames byte 83950595
// (block 8395057C + 25) once per call while the attacker is airborne, and the
// CPU AI (8267D318 -> 8267D66C) queues the swing (546/548/558/560) once it
// reaches the character's delay from 8267EC18. At 60 Hz the swing came ~7-10
// ball units early and CPU spikes missed (logs 172/173). With the base speed
// scaled, hold the increment on alternate calls so it counts 30 Hz frames.
REX_EXTERN(__imp__sub_8268CAB0);
REX_HOOK_RAW(sub_8268CAB0) {
  constexpr uint32_t kJumpFrames = 0x83950595;
  Native60NoteFiberContext(base, "8268CAB0 jump frames");
  const bool scaled = float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f &&
                      !Native60InScriptFiber(base);
  const uint8_t jump_before = REX_LOAD_U8(kJumpFrames);
  std::array<uint16_t, 4> before{};
  const bool probe = REXCVAR_GET(doax2_native60_counter_scan);
  if (probe)
    for (uint32_t p = 0; p < 4; ++p)
      before[p] = REX_LOAD_U16(0x833B8520 + p * 152 + 66);
  __imp__sub_8268CAB0(ctx, base);
  if (scaled) {
    static bool hold = false;
    const uint8_t jump_after = REX_LOAD_U8(kJumpFrames);
    if (jump_after == uint8_t(jump_before + 1)) {
      hold = !hold;
      if (hold) REX_STORE_U8(kJumpFrames, jump_before);
    }
  }
  if (probe) ReportNative60Queue(base, "sub_8268CAB0", before);
}

// Volleyball AI action scheduler 82679A30(player): the AI record at
// 839525C8 + p*92 holds u16 countdowns +10 (start the planned action from
// 83947E78 + p*56 + 24 at zero, e.g. the spike 14 updates into approach
// 471), +12 (secondary delay) and +14 (heading blend over n updates), each
// decremented once per call. At 60 Hz the spiker jumped at half the
// authored delay (log 167: caller 82679CB4, every set missed). Hold each
// decrement on alternate calls per player; never hold a 1 -> 0 step
// because the action fires on reaching zero.
REX_EXTERN(__imp__sub_82679A30);
REX_HOOK_RAW(sub_82679A30) {
  const auto countdowns = Native60AiCountdownsBegin(base, ctx.r3.u32);
  __imp__sub_82679A30(ctx, base);
  Native60AiCountdownsEnd(base, countdowns, "82679A30");
}

// Spike trigger probe. The AI queues the spike (504 etc.) through 82678FF0
// exactly once, 13 updates into approach motion 471 at 60 Hz (~14 motion
// frames originally), so the swing happens while the ball is still high.
// Deferring the queue (log 160) failed: the AI never re-issues it and it
// froze other motion changes. Instead dump every candidate per-player field
// when a spike is queued out of 471 to find the per-update trigger.
void DumpNative60SpikeTrigger(uint8_t* base, uint32_t player, uint32_t motion,
                              uint32_t caller) {
  static uint64_t dumps = 0;
  if (++dumps > 120) return;
  struct Block { const char* name; uint32_t address; uint32_t bytes; };
  const std::array<Block, 3> blocks = {{
      {"state", 0x833B8520 + player * 152, 152},
      {"ai", 0x839525C8 + player * 92, 92},
      {"aicount", 0x83947DE8 + player * 16, 16}}};
  const float frames = float(Doax2EngineFrameCount() -
                             native60_motion_start_update[player]);
  for (const auto& block : blocks) {
    std::string words;
    for (uint32_t off = 0; off < block.bytes; off += 2) {
      const uint16_t half = REX_LOAD_U16(block.address + off);
      if (half == 0 || half == 0xFFFF) continue;
      words += fmt::format(" {}:{}", off, half);
    }
    REXLOG_INFO("DOAX2 native60 spike trigger: player={} motion={} "
                "caller={:08X} updates_in_approach={} block={} halves{}",
                player, motion, caller, frames, block.name, words);
  }
}
REX_EXTERN(__imp__sub_82678FF0);
REX_HOOK_RAW(sub_82678FF0) {
  const uint32_t player = ctx.r3.u32;
  const uint32_t motion = ctx.r4.u32 & 0xFFFF;
  if (player < 4 && REXCVAR_GET(doax2_native60_counter_scan) &&
      (REX_LOAD_U16(0x833B8520 + player * 152 + 64) == 471 ||
       REX_LOAD_U16(0x833B8520 + player * 152 + 64) == 542 ||
       REX_LOAD_U16(0x833B8520 + player * 152 + 64) == 543) &&
      motion != 471 && motion != 0xFFFF)
    DumpNative60SpikeTrigger(base, player, motion, uint32_t(ctx.lr));
  std::array<uint16_t, 4> before{};
  const bool probe = REXCVAR_GET(doax2_native60_counter_scan);
  if (probe)
    for (uint32_t p = 0; p < 4; ++p)
      before[p] = REX_LOAD_U16(0x833B8520 + p * 152 + 66);
  __imp__sub_82678FF0(ctx, base);
  if (probe) ReportNative60Queue(base, "sub_82678FF0", before);
}



// Jet-ski race manager 826496A0 increments the intro/race clock 839865CC
// once per call and steps the girls' pre-race intro on fixed frame counts
// (e.g. == 30); the intro lasted 211 updates at both 30 and 60 Hz (logs
// 186/192), i.e. half the time at 60 Hz. Hold it on alternate calls.
Native60HeldCounters<1> native60_jet_clock{"826496A0", {0x839865CC}};
REX_EXTERN(__imp__sub_826496A0);
REX_HOOK_RAW(sub_826496A0) {
  Native60MarkMinigameUpdate();
  native60_jet_frame.store(Doax2EngineFrameCount(), std::memory_order_relaxed);
  const bool active = native60_jet_clock.Begin(base);
  __imp__sub_826496A0(ctx, base);
  if (active) native60_jet_clock.End(base);
}

// Screen/sequence manager 834BCFE0: 825A0FD8 advances +48 (total frames)
// and +52 (frames in state) by one per update; +52 is compared with the
// state duration (+56) and drives 825A16A0's transition curve. Intro steps
// and fades therefore ran at double speed.
Native60HeldCounters<2> native60_screen_frames{"825A0FD8",
                                               {0x834BD014, 0x834BD010}};
// Title music: the screen manager runs the current screen's update function
// from its descriptor table ([834BCFE0+68] + screen*36, +4); 825A0420 is the
// title menu. Returning to the title (quit, demo end, end of a girl's week)
// must stop the soundtrack so a new playlist starts when the game is entered
// again; the keep-song option ignores the game's own stops, so stop the host
// player when the title screen is entered. Screen changes are logged.
inline uint32_t Native60ScreenUpdateFunction(uint8_t* base) {
  constexpr uint32_t kManager = 0x834BCFE0;
  const int32_t screen = int8_t(REX_LOAD_U8(kManager + 0));
  const uint32_t table = REX_LOAD_U32(kManager + 68);
  if (screen < 0 || !table) return 0;
  return REX_LOAD_U32(table + uint32_t(screen) * 36 + 4);
}
// Title screen entry: 824C0B78 (called through a pointer) runs whenever the
// "Press Start" title appears - after the boot movies, quitting from the
// game, leaving or finishing the idle demo, the end of a girl's week - and
// requests the random title voice. Stop the soundtrack there (the keep-song
// option ignores the game's own stops on the way), so a new playlist starts
// when the game is entered again.
//
// Skipping the boot movies enters the title twice, 14 updates apart (log
// test-v1.0.0 002: voice dropped 234 ms after the first). The second entry
// restarts the title and cut the first voice off mid-line, so a second
// entry within 2 s of the first, right after a boot movie, is skipped.
REX_EXTERN(__imp__sub_824C0B78);
REX_HOOK_RAW(sub_824C0B78) {
  static int64_t last_entry_ms = -1000000;
  const int64_t now = Native60NowMs();
  const bool after_movie = now - native60_movie_event_ms.load() < 10000;
  if (after_movie && now - last_entry_ms < 2000) {
    REXLOG_INFO("DOAX2 title screen entered again {} ms after the first (boot movie skip): skipped, lr={:08X}",
                now - last_entry_ms, uint32_t(ctx.lr));
    return;
  }
  last_entry_ms = now;
  if (rex::audio::GetTitlePlaylistDiagnostics().playing) {
    rex::audio::StopTitlePlaylist();
    REXLOG_INFO("DOAX2 title screen entered: title music stopped");
  }
  REXLOG_INFO("DOAX2 title screen entered: lr={:08X}", uint32_t(ctx.lr));
  __imp__sub_824C0B78(ctx, base);
}

// Sound stops by priority (82669180) after the boot movies are logged, in
// case anything else cuts the title voice.
REX_EXTERN(__imp__sub_82669180);
REX_HOOK_RAW(sub_82669180) {
  if (Native60NowMs() - native60_movie_event_ms.load() < 10000) {
    static uint32_t lines = 0;
    if (++lines <= 50)
      REXLOG_INFO("DOAX2 sound stop (82669180): level={} lr={:08X}", ctx.r3.u32, uint32_t(ctx.lr));
  }
  __imp__sub_82669180(ctx, base);
}

// The game's music fade-out + stop (82745140, 19 callers). Called from the
// title code (82590000-825A3000: 825A2028, 82591298, 82591DB0) it is the
// return to the title's "Press Start" screen, which the screen manager does
// not track; the stop that ends this fade is honored even with the keep-song
// option, so the song does not carry onto the title. Every call is logged
// with its caller.
REX_EXTERN(__imp__sub_82745140);
REX_HOOK_RAW(sub_82745140) {
  const uint32_t caller = uint32_t(ctx.lr);
  // Quit to the title from the game (log game-exp 002, 22:09:36): 826CE878
  // and 826CDB10 (next to 826CDA50, which runs the screen manager) fade out
  // together, then 824C0608; a scene change fades out from 8265E118 only.
  // Leaving the idle demo (it plays its own one-song playlist, faded in from
  // 8265FE18) fades out from 824C1160 (log game-exp 003, 22:12:22).
  const bool title = (caller >= 0x82590000 && caller < 0x825A3000) || caller == 0x826CE8A4 ||
                     caller == 0x826CDB54 || caller == 0x824C11A4;
  static uint32_t lines = 0;
  if (++lines <= 200) REXLOG_INFO("DOAX2 music fade-out: caller={:08X}{}", caller, title ? " (title)" : "");
  __imp__sub_82745140(ctx, base);
  // Let the game's own stop at the end of this fade go through (runtime
  // cvar; the keep option ignores the scene-change stops otherwise).
  if (title && rex::audio::GetTitlePlaylistDiagnostics().playing &&
      rex::cvar::SetFlagByName("xmp_keep_song_honor_next_stop", "true"))
    REXLOG_INFO("DOAX2 title screen: the music stops after this fade-out");
}
REX_EXTERN(__imp__sub_825A0FD8);
REX_HOOK_RAW(sub_825A0FD8) {
  {
    static uint32_t last_function = ~0u;
    const uint32_t function = Native60ScreenUpdateFunction(base);
    if (function != last_function) {
      static uint32_t lines = 0;
      if (++lines <= 200)
        REXLOG_INFO("DOAX2 screen: {} update={:08X}", int32_t(int8_t(REX_LOAD_U8(0x834BCFE0))), function);
      if (function == 0x825A0420 && rex::audio::GetTitlePlaylistDiagnostics().playing) {
        rex::audio::StopTitlePlaylist();
        REXLOG_INFO("DOAX2 title screen: title music stopped");
      }
      last_function = function;
    }
  }
  const bool active = native60_screen_frames.Begin(base);
  const uint32_t state_before = REX_LOAD_U32(0x834BCFE0 + 40);
  const uint32_t frames_before = REX_LOAD_U32(0x834BCFE0 + 52);
  __imp__sub_825A0FD8(ctx, base);
  if (active) native60_screen_frames.End(base);
  if (REXCVAR_GET(doax2_native60_counter_scan) &&
      REX_LOAD_U32(0x834BCFE0 + 40) != state_before)
    REXLOG_INFO("DOAX2 native60 screen state: {} -> {} after_frames={} "
                "duration={} engine_frame={}", int32_t(state_before),
                int32_t(REX_LOAD_U32(0x834BCFE0 + 40)), int32_t(frames_before),
                int32_t(REX_LOAD_U32(0x834BCFE0 + 56)), Doax2EngineFrameCount());
}

// Block touches are a discrete collision test: 82690160 checks each player
// body sphere (826902C0) against the ball and commits action 8 when it
// overlaps while approaching. The original sampled this at 30 Hz, i.e. at
// whole ball times; sampling every 60 Hz update catches grazes the original
// never saw, and opposing blockers touched 9 of 10 sets (logs 146/149/151;
// holding the match frame counters in log 151 did not change this). With
// the base speed scaled, run the test only at the original sample instants.
REX_EXTERN(__imp__sub_82690160);
REX_HOOK_RAW(sub_82690160) {
  if (float(REXCVAR_GET(doax2_native60_tick_scale)) == 0.5f) {
    const float ball_time = Native60LoadFloat(base, kNative60Ball + 7364);
    if (ball_time != std::floor(ball_time)) {
      static uint64_t skipped = 0;
      if (++skipped <= 4 || skipped % 1200 == 0)
        REXLOG_INFO("DOAX2 native60 block test deferred to 30 Hz instant: "
                    "skipped={} ball_t={}", skipped, ball_time);
      return;
    }
  }
  __imp__sub_82690160(ctx, base);
}

// 82682590(player, action, f1) commits the hitter's action code to ball
// +7376; the ball controller dispatches on it next update. Player state is
// 833B8520 + player * 152 (+64 current motion, +66 queued motion, +62 byte).
REX_EXTERN(__imp__sub_82682590);
REX_HOOK_RAW(sub_82682590) {
  const uint32_t player = ctx.r3.u32, action = ctx.r4.u32;
  const uint32_t state = 0x833B8520 + player * 152;
  const uint8_t pending = REX_LOAD_U8(kNative60Ball + 7376);
  static uint64_t commits = 0;
  if (++commits <= 2000)
    REXLOG_INFO("DOAX2 native60 ball commit: n={} player={} action={} "
                "pending_before={} motion={} queued={} arg={} ball_t={} "
                "caller={:08X} step={} engine_frame={}",
                commits, player, action, pending,
                REX_LOAD_U16(state + 64), REX_LOAD_U16(state + 66), ctx.f1.f64,
                Native60LoadFloat(base, kNative60Ball + 7364),
                uint32_t(ctx.lr), Native60LoadFloat(base, kNative60GameStep),
                Doax2EngineFrameCount());
  __imp__sub_82682590(ctx, base);
}

REX_EXTERN(__imp__sub_82684B08);
REX_HOOK_RAW(sub_82684B08) {
  ReportNative60BallLaunch(base, "target_T", uint32_t(ctx.lr), float(ctx.f1.f64), ctx);
  __imp__sub_82684B08(ctx, base);
}
REX_EXTERN(__imp__sub_82684C30);
REX_HOOK_RAW(sub_82684C30) {
  ReportNative60BallLaunch(base, "target_speed", uint32_t(ctx.lr), float(ctx.f1.f64), ctx);
  __imp__sub_82684C30(ctx, base);
}
REX_EXTERN(__imp__sub_826836F8);
REX_HOOK_RAW(sub_826836F8) {
  ReportNative60BallLaunch(base, "826836F8", uint32_t(ctx.lr), 0.0f, ctx);
  __imp__sub_826836F8(ctx, base);
}
REX_EXTERN(__imp__sub_82684D80);
REX_HOOK_RAW(sub_82684D80) {
  // Velocity launch (f1..f3); runs every update while the ball is carried,
  // so log only when the caller changes.
  static uint32_t last_caller = 0;
  if (uint32_t(ctx.lr) != last_caller) {
    last_caller = uint32_t(ctx.lr);
    ReportNative60BallLaunch(base, "velocity", uint32_t(ctx.lr), 0.0f, ctx);
  }
  __imp__sub_82684D80(ctx, base);
}
REX_EXTERN(__imp__sub_82424558);
REX_HOOK_RAW(sub_82424558) {
  Native60WriterProbeScope native60_probe_scope(base, "sub_82424558");
  const uint32_t selected = Native60SelectedCameraObject(base);
  const uint32_t object = selected == ctx.r3.u32 ? selected : 0;
  const auto before = SnapshotNative60ObjectCamera(base, object);
  const uint32_t timer_before = object ? REX_LOAD_U32(object + 1944) : 0;
  const uint32_t gate_before = object ? REX_LOAD_U32(object + 1940) : 0;
  {
    Native60UnscaledStep unscaled(base);
    __imp__sub_82424558(ctx, base);
  }
  if (!object) return;
  const auto after = SnapshotNative60ObjectCamera(base, object);
  const uint32_t timer_after = REX_LOAD_U32(object + 1944);
  const uint32_t native_gate_after = REX_LOAD_U32(object + 1940);
  const bool correct = REXCVAR_GET(doax2_native60_mode3_gate_half) &&
                       IsNative60SceneObject(base, object) &&
                       REX_LOAD_U32(object + 3580) == 3;
  if (correct) {
    const auto corrected = doax2::HalfExpectedNativePhaseStep(
        std::bit_cast<float>(gate_before),
        std::bit_cast<float>(native_gate_after), -1.0f);
    if (corrected) {
      REX_STORE_U32(object + 1940, std::bit_cast<uint32_t>(*corrected));
      static thread_local uint64_t corrections = 0;
      ++corrections;
      if (corrections <= 8 || corrections % 120 == 0)
        REXLOG_INFO("DOAX2 native60 mode3 gate correction: calls={} "
                    "object={:08X} before={} native_after={} corrected_after={}; "
                    "original scene update still executed",
                    corrections, object,
                    std::bit_cast<float>(gate_before),
                    std::bit_cast<float>(native_gate_after), *corrected);
    }
  }
  const uint32_t gate_after = REX_LOAD_U32(object + 1940);
  static thread_local uint64_t calls = 0, decrements = 0, source_changes = 0;
  ++calls;
  decrements += std::bit_cast<float>(timer_after) <
                std::bit_cast<float>(timer_before);
  source_changes += before.source != after.source;
  if (calls <= 8 || calls % 120 == 0)
    REXLOG_INFO("DOAX2 native60 mode3 clock: calls={} decrements={} "
                "source_changes={} object={:08X} timer_before={} "
                "timer_after={} gate_before={} gate_after={}; read-only",
                calls, decrements, source_changes, object,
                std::bit_cast<float>(timer_before),
                std::bit_cast<float>(timer_after),
                std::bit_cast<float>(gate_before),
                std::bit_cast<float>(gate_after));
}
DOAX2_NATIVE60_CHILD_PROBE(sub_8252ABB0)
DOAX2_NATIVE60_CHILD_PROBE(sub_8252C068)
REX_EXTERN(__imp__sub_825C19C8);
REX_HOOK_RAW(sub_825C19C8) {
  const auto phase_before = SnapshotNative60Phase(base);
  const auto camera_before = SnapshotNative60Camera(base);
  const uint32_t native60_index_before_sub_825C19C8 = Native60SceneIndex(base);
  __imp__sub_825C19C8(ctx, base);
  Native60SceneIndexCheck(base, "sub_825C19C8", native60_index_before_sub_825C19C8);
  ReportNative60Camera(base, "sub_825C19C8", camera_before, 1);
  ReportNative60PhaseBoundary(base, "sub_825C19C8", phase_before);
}
DOAX2_NATIVE60_CHILD_PROBE(sub_8248D858)
DOAX2_NATIVE60_CHILD_PROBE(sub_82546280)
DOAX2_NATIVE60_CHILD_PROBE(sub_825C1E88)
DOAX2_NATIVE60_CHILD_PROBE(sub_825C2130)
DOAX2_NATIVE60_CHILD_PROBE(sub_8262D0F8)
DOAX2_NATIVE60_CHILD_PROBE(sub_82492000)
DOAX2_NATIVE60_CHILD_PROBE(sub_824894C0)
// Direct children of 825C19C8, which the automatic preview showed encloses
// the recurring manager's +1 phase change. If none accounts for that write,
// inspect this parent's four indirect dispatches next.
DOAX2_NATIVE60_CHILD_PROBE(sub_825C03E0)
DOAX2_NATIVE60_CHILD_PROBE(sub_826713D8)
DOAX2_NATIVE60_CHILD_PROBE(sub_825C2508)
DOAX2_NATIVE60_CHILD_PROBE(sub_827580E0)
DOAX2_NATIVE60_CHILD_PROBE(sub_825C3670)
DOAX2_NATIVE60_CHILD_PROBE(sub_825C32A8)
DOAX2_NATIVE60_CHILD_PROBE(sub_8266FBF8)
DOAX2_NATIVE60_CHILD_PROBE(sub_826696D0)
DOAX2_NATIVE60_CHILD_PROBE(sub_82667130)
DOAX2_NATIVE60_CHILD_PROBE(sub_82667B00)
DOAX2_NATIVE60_CHILD_PROBE(sub_826715F8)
DOAX2_NATIVE60_CHILD_PROBE(sub_825C93C8)
DOAX2_NATIVE60_CHILD_PROBE(sub_825AB408)
DOAX2_NATIVE60_CHILD_PROBE(sub_825C29A0)
DOAX2_NATIVE60_CHILD_PROBE(sub_825AB130)
DOAX2_NATIVE60_CHILD_PROBE(sub_82673698)
DOAX2_NATIVE60_CHILD_PROBE(sub_82538048)
#undef DOAX2_NATIVE60_CHILD_PROBE

REX_EXTERN(__imp__sub_825C27C8);
REX_HOOK_RAW(sub_825C27C8) {
  const auto native60_phase_before = SnapshotNative60Phase(base);
  if (REXCVAR_GET(doax2_native60_eval_probe)) {
    static thread_local uint64_t calls = 0;
    ++calls;
    if (calls == 1 || calls % 512 == 0) {
      // Exact index and table expression from the original function's
      // 825C27F8/825C2804 branch. Observe its two late virtual targets.
      const uint32_t slot = REX_LOAD_U8(0x833B745F);
      const uint32_t mapped = REX_LOAD_U8(0x833B851C + slot);
      const uint32_t index = mapped >= 44 ? slot : mapped * 2 + slot;
      const uint32_t object = REX_LOAD_U32(0x82B82CF8 + index * 4);
      const uint32_t vtable = REX_LOAD_U32(object);
      REXLOG_INFO("DOAX2 native60 825C27C8 dispatch: calls={} slot={} "
                  "mapped={} object={:08X} virtual_56={:08X} virtual_16={:08X}",
                  calls, slot, mapped, object, REX_LOAD_U32(vtable + 56),
                  REX_LOAD_U32(vtable + 16));
    }
  }
  const uint32_t native60_index_before_sub_825C27C8 = Native60SceneIndex(base);
  __imp__sub_825C27C8(ctx, base);
  Native60SceneIndexCheck(base, "sub_825C27C8", native60_index_before_sub_825C27C8);
  ReportNative60PhaseBoundary(base, "sub_825C27C8", native60_phase_before);
}

// 825C27C8 dispatches these as live virtual +16 updates. Unlike the older
// 825B0028 hypothesis, their absolute phase fields match the recurring
// evaluator managers observed in the automatic preview.
REX_EXTERN(__imp__sub_82606B98);
REX_HOOK_RAW(sub_82606B98) {
  constexpr uint32_t phase = 0x82E498AC;
  const uint32_t before = REXCVAR_GET(doax2_native60_phase_half)
      ? REX_LOAD_U32(phase) : 0;
  __imp__sub_82606B98(ctx, base);
  CorrectNative60PhaseField(base, "82606B98", phase, before);
}

REX_EXTERN(__imp__sub_82612778);
REX_HOOK_RAW(sub_82612778) {
  constexpr uint32_t phase_a = 0x82E4E0DC;
  constexpr uint32_t phase_b = 0x82E4E15C;
  const bool correction = REXCVAR_GET(doax2_native60_phase_half);
  const uint32_t before_a = correction ? REX_LOAD_U32(phase_a) : 0;
  const uint32_t before_b = correction ? REX_LOAD_U32(phase_b) : 0;
  __imp__sub_82612778(ctx, base);
  if (correction) {
    CorrectNative60PhaseField(base, "82612778.a", phase_a, before_a);
    CorrectNative60PhaseField(base, "82612778.b", phase_b, before_b);
  }
}

// The title-preview scene switches virtual +16 to 825E6900. Its three
// absolute manager phases are advanced directly from the same shared unit;
// the recurring 82E3AC68 manager is the second of these records.
REX_EXTERN(__imp__sub_825E6900);
REX_HOOK_RAW(sub_825E6900) {
  constexpr uint32_t phases[] = {0x82E3AC04, 0x82E3AC84, 0x82E3ACB8};
  const bool correction = REXCVAR_GET(doax2_native60_phase_half);
  uint32_t before[3]{};
  if (correction)
    for (size_t i = 0; i < 3; ++i) before[i] = REX_LOAD_U32(phases[i]);
  __imp__sub_825E6900(ctx, base);
  if (correction)
    for (size_t i = 0; i < 3; ++i)
      CorrectNative60PhaseField(base, "825E6900", phases[i], before[i]);
}

// These wrappers are observed runtime types. Identify the callback and its
// upload sources without reading shader buffers or interpreting them as bones.
REX_EXTERN(__imp__sub_825B3E10);
REX_EXTERN(__imp__sub_8248A348);
REX_HOOK_RAW(sub_8248A348) {
  const uint32_t slot = ctx.r3.u32;
  if (!REXCVAR_GET(doax2_render_queue_probe) || slot >= 4) {
    __imp__sub_8248A348(ctx, base); return;
  }
  const uint32_t source = 0x833A10D0u + 384u * slot;
  const auto read_float = [&](uint32_t address) {
    return std::bit_cast<float>(REX_LOAD_U32(address));
  };
  doax2::DeformationRecipe host;
  const bool supported = doax2::EvaluateDeformationRecipe(
      read_float(source + 224), read_float(source + 232),
      read_float(source + 4), read_float(source + 116), host);
  __imp__sub_8248A348(ctx, base);
  static thread_local uint64_t checked = 0, skipped = 0, mismatches = 0;
  static thread_local float maximum_error = 0;
  if (!supported) { ++skipped; return; }
  const uint32_t output = 0x833B4330u + 48u * slot;
  bool mismatch = false;
  for (size_t i = 0; i < 4; ++i)
    mismatch |= host.indices[i] != REX_LOAD_U32(output + 4u * i);
  for (size_t i = 0; i < 8; ++i) {
    const float original = read_float(output + 16u + 4u * i);
    const float error = std::abs(original - host.weights[i]);
    mismatch |= !std::isfinite(original) || error > 1e-6f * (1 + std::abs(original));
    if (std::isfinite(error) && error > maximum_error) maximum_error = error;
  }
  ++checked;
  if (mismatch) ++mismatches;
  if (checked == 1 || checked % 120 == 0 || (mismatch && mismatches == 1))
    REXLOG_INFO("DOAX2 deformation recipe: checked {}, skipped {}, mismatches {}, max error {}, slot {}, indices {} {} {} {}; host comparison only, original accumulator and output unchanged",
        checked, skipped, mismatches, maximum_error, slot,
        host.indices[0], host.indices[1], host.indices[2], host.indices[3]);
}
REX_HOOK_RAW(sub_825B3E10) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) { __imp__sub_825B3E10(ctx, base); return; }
  const uint32_t object = ctx.r3.u32;
  // Both fields are consumed unconditionally by the original wrapper.
  const uint32_t callback = REX_LOAD_U32(object + 8);
  const uint32_t payload = REX_LOAD_U32(object + 12);
  shader_input_summary.Observe({0x825B3E10, static_cast<uint32_t>(ctx.lr), callback,
      std::hash<std::thread::id>{}(std::this_thread::get_id()), capture_epoch.load(),
      object, payload, ctx.r4.u32, 0});
  ShaderInputScope scope(callback);
  __imp__sub_825B3E10(ctx, base);
}
REX_EXTERN(__imp__sub_824B5F48);
REX_EXTERN(__imp__sub_824B7130);
REX_HOOK_RAW(sub_824B7130) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) { __imp__sub_824B7130(ctx, base); return; }
  const uint32_t list = ctx.r3.u32, slot = ctx.r4.u32;
  const uint32_t model = ctx.r8.u32, resource = ctx.r6.u32;
  __imp__sub_824B7130(ctx, base);
  // The audited function appends its allocation and stores the tail at list+4.
  const uint32_t object = REX_LOAD_U32(list + 4);
  if (transient_registry.Add(object, slot, model, resource)) ++transient_created;
  else ++transient_rejected;
}
REX_EXTERN(__imp__sub_8275AE18);
REX_HOOK_RAW(sub_8275AE18) {
  // Invalidate before destructors run. This registry is observation-only.
  transient_registry.Retire();
  ++transient_retired;
  __imp__sub_8275AE18(ctx, base);
  // Destructors may themselves allocate temporary nodes.
  transient_registry.Retire();
}
REX_HOOK_RAW(sub_824B5F48) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) { __imp__sub_824B5F48(ctx, base); return; }
  ShaderInputScope scope(0x824B5F48);
  DeformationDrawScope draw_scope;
  const auto* allocation = transient_registry.Find(ctx.r3.u32);
  if (allocation) ++transient_matched;
  else ++transient_missing;
  if (allocation && allocation->slot < 4) {
    // Both the geometry pointer and its layout are consumed by the original.
    const uint32_t geometry = REX_LOAD_U32(ctx.r4.u32 + 12);
    if (geometry && REX_LOAD_U32(geometry + 4) == 7) {
      const uint32_t source = 0x833A10D0u + 384u * allocation->slot;
      const auto read = [&](uint32_t a) { return std::bit_cast<float>(REX_LOAD_U32(a)); };
      auto& packet = deformation_draw;
      packet.object = ctx.r3.u32;
      packet.ticket = allocation->ticket;
      packet.valid = doax2::EvaluateDeformationRecipe(read(source + 224), read(source + 232),
          read(source + 4), read(source + 116), packet.recipe);
      // A reset can publish a default recipe without updating these inputs.
      // Do not pair stale host input with a different live selection table.
      const uint32_t table = 0x833B4330u + 48u * allocation->slot;
      for (size_t i = 0; i < 4; ++i)
        packet.valid &= packet.recipe.indices[i] == REX_LOAD_U32(table + 4u * i);
      packet.valid &= REX_LOAD_U32(packet.object + 16) == allocation->slot;
    }
  }
  if (deformation_draw.valid) ++deformation_draw_prepared;
  else ++deformation_draw_skipped;
  __imp__sub_824B5F48(ctx, base);
}
REX_EXTERN(__imp__sub_826FA7D0);
REX_HOOK_RAW(sub_826FA7D0) {
  if (pass_trace.active) {
    // Key by starting constant, not only by caller: one call site may upload
    // multiple ranges. Retain numeric labels, never dereference the source.
    geometry_input_summary.Observe({0x826FA7D0, static_cast<uint32_t>(ctx.lr), ctx.r4.u32,
        std::hash<std::thread::id>{}(std::this_thread::get_id()), capture_epoch.load(),
        ctx.r6.u32, ctx.r5.u32, ctx.r3.u32, 0});
  }
  if (shader_input_parent) {
    shader_input_summary.Observe({0x826FA7D0, static_cast<uint32_t>(ctx.lr), shader_input_parent,
        std::hash<std::thread::id>{}(std::this_thread::get_id()), capture_epoch.load(),
        ctx.r4.u32, ctx.r6.u32, ctx.r5.u32, 0});
  }
  __imp__sub_826FA7D0(ctx, base);
}
REX_EXTERN(__imp__sub_82703AD0);
REX_HOOK_RAW(sub_82703AD0) {
  if (shader_input_parent == 0x824B5F48) deformation_draw.Bind(ctx.r4.u32, ctx.r5.u32);
  if (pass_trace.active) {
    geometry_input_summary.Observe({0x82703AD0, static_cast<uint32_t>(ctx.lr), ctx.r4.u32,
        std::hash<std::thread::id>{}(std::this_thread::get_id()), capture_epoch.load(),
        ctx.r5.u32, ctx.r3.u32, 0, 0});
  }
  __imp__sub_82703AD0(ctx, base);
}

REX_EXTERN(__imp__sub_8258E778);
REX_HOOK_RAW(sub_8258E778) {
  const auto native60_phase_before = SnapshotNative60Phase(base);
  if (!REXCVAR_GET(doax2_render_queue_probe)) {
    __imp__sub_8258E778(ctx, base);
    ReportNative60PhaseBoundary(base, "sub_8258E778", native60_phase_before);
    return;
  }
  pass_trace.Begin();
  __imp__sub_8258E778(ctx, base);
  ReportNative60PhaseBoundary(base, "sub_8258E778", native60_phase_before);
  pass_trace.End();
  static thread_local uint64_t frames = 0;
  static thread_local bool reported_draws = false;
  ++frames;
  if (frames % 120 == 0 || (!reported_draws && pass_trace.draws)) {
    reported_draws |= pass_trace.draws != 0;
    REXLOG_INFO("DOAX2 pass trace: render call {}, draw calls {}, inline begins {}, boundary calls {}, dropped {}; root-thread CPU sequence only; not GPU completion or replay data",
                frames, pass_trace.draws, pass_trace.inline_draws, pass_trace.count, pass_trace.dropped);
    for (size_t i = 0; i < pass_trace.count; ++i) {
      const auto& e = pass_trace.events[i];
      REXLOG_INFO("DOAX2 pass boundary: order {}, fn {:08X}, caller {:08X}, r3-r10 {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}, draws before {}, inline before {}",
                  i, e.function, e.caller, e.a0, e.a1, e.a2,
                  e.r6_to_r10[0], e.r6_to_r10[1], e.r6_to_r10[2],
                  e.r6_to_r10[3], e.r6_to_r10[4], e.draws_before, e.inline_before);
    }
  }
}

#define DOAX2_PASS_HOOK(name, address) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    RecordPassCall(address, ctx); \
    __imp__##name(ctx, base); \
  }
DOAX2_PASS_HOOK(sub_82703438, 0x82703438)
DOAX2_PASS_HOOK(sub_82703768, 0x82703768)
DOAX2_PASS_HOOK(sub_82708CC0, 0x82708CC0)
#undef DOAX2_PASS_HOOK
REX_EXTERN(__imp__sub_826FA358);
REX_HOOK_RAW(sub_826FA358) {
  pass_trace.Draw();
  bool supply = false;
  const uint32_t device = ctx.r3.u32;
  std::array<uint32_t, 8> saved{}, replacement{};
  if (shader_input_parent == 0x824B5F48 && static_cast<uint32_t>(ctx.lr) == 0x824B695C) {
    const auto* allocation = transient_registry.Find(deformation_draw.object);
    if (allocation && deformation_draw.Ready(allocation->ticket)) {
      std::array<float, 8> actual;
      for (size_t i = 0; i < 8; ++i) {
        saved[i] = REX_LOAD_U32(device + doax2::DeformationWeightOffset(i));
        actual[i] = std::bit_cast<float>(saved[i]);
        replacement[i] = std::bit_cast<uint32_t>(deformation_draw.recipe.weights[i]);
      }
      ++deformation_draw_checked;
      if (!deformation_draw.Matches(actual)) ++deformation_draw_mismatch;
      else supply = REXCVAR_GET(doax2_host_deformation_test);
    } else ++deformation_draw_incomplete;
  }
  if (supply) {
    for (size_t i = 0; i < 8; ++i) {
      deformation_changed_words += saved[i] != replacement[i];
      REX_STORE_U32(device + doax2::DeformationWeightOffset(i), replacement[i]);
    }
    REX_STORE_U64(device, REX_LOAD_U64(device) | doax2::kDeformationDirtyMask);
    // Restoring values also requires marking them dirty: the preceding draw
    // may have consumed/cleared these flags and later draws may reuse caches.
    const auto restore = [&]() {
      for (size_t i = 0; i < 8; ++i)
        REX_STORE_U32(device + doax2::DeformationWeightOffset(i), saved[i]);
      REX_STORE_U64(device, REX_LOAD_U64(device) | doax2::kDeformationDirtyMask);
    };
    try { __imp__sub_826FA358(ctx, base); }
    catch (...) { restore(); throw; }
    restore();
    ++deformation_draw_supplied;
    return;
  }
  __imp__sub_826FA358(ctx, base);
}

REX_EXTERN(__imp__sub_826F9E58);
REX_HOOK_RAW(sub_826F9E58) {
  pass_trace.InlineDraw();
  __imp__sub_826F9E58(ctx, base);
}

REX_EXTERN(__imp__sub_825B8AD8);
REX_EXTERN(__imp__sub_825B8810);
REX_HOOK_RAW(sub_825B8AD8) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) {
    __imp__sub_825B8AD8(ctx, base);
    return;
  }
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t manager = ctx.r3.u32, a1 = ctx.r4.u32, a2 = ctx.r5.u32;
  const uint32_t parent = capture_parent;
  CaptureParentScope scope(0x825B8AD8);
  hierarchy_packet.Begin(manager);
  __imp__sub_825B8AD8(ctx, base);
  const bool complete = hierarchy_packet.End();
  CaptureEdge(0x825B8AD8, caller, parent, manager, a1, a2, ctx.r3.u32);
  static thread_local uint64_t calls = 0, rejected = 0;
  ++calls;
  if (!complete) ++rejected;
  if (calls == 1 || calls % 120 == 0 || (!complete && rejected == 1)) {
    REXLOG_INFO("DOAX2 hierarchy packet: calls {}, rejected {}, latest manager {:08X}, nodes {}, complete {}; copied submission matrices only; no interpolation or resubmission",
                calls, rejected, manager, hierarchy_packet.size(), complete);
  }
}

REX_HOOK_RAW(sub_825B8810) {
  if (REXCVAR_GET(doax2_render_queue_probe) && hierarchy_packet.active()) {
    const uint32_t node = ctx.r3.u32;
    const uint32_t index = REX_LOAD_U32(node + 40);
    // Exact original target arithmetic and matrix input; no tree pre-walk and
    // no extra virtual calls. Observe each normal recursive invocation once.
    const uint32_t target = ctx.r4.u32 + index * 140u;
    doax2::MatrixBits matrix;
    for (uint32_t i = 0; i < 16; ++i) matrix[i] = REX_LOAD_U32(node + 64 + i * 4);
    hierarchy_packet.Add(node, target, matrix);
  }
  __imp__sub_825B8810(ctx, base);
}

REX_HOOK_RAW(sub_825ABAE0) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) {
    __imp__sub_825ABAE0(ctx, base);
    return;
  }
  const auto check = CaptureInstanceUpload(ctx, base, false);
  __imp__sub_825ABAE0(ctx, base);
  VerifyInstanceUpload(ctx, base, check, false);
}

REX_HOOK_RAW(sub_825AC548) {
  if (!REXCVAR_GET(doax2_render_queue_probe)) {
    __imp__sub_825AC548(ctx, base);
    return;
  }
  const auto check = CaptureInstanceUpload(ctx, base, true);
  __imp__sub_825AC548(ctx, base);
  VerifyInstanceUpload(ctx, base, check, true);
}

REX_HOOK_RAW(sub_8275AAB0) {
  if (REXCVAR_GET(doax2_render_queue_probe)) ObserveInlineMatrices(ctx, base, 2);
  __imp__sub_8275AAB0(ctx, base);
}

REX_HOOK_RAW(sub_8275AB70) {
  if (REXCVAR_GET(doax2_render_queue_probe)) ObserveInlineMatrices(ctx, base, 3);
  __imp__sub_8275AB70(ctx, base);
}

REX_HOOK_RAW(sub_8275A800) {
  if (REXCVAR_GET(doax2_render_queue_probe)) {
    const uint32_t object = ctx.r3.u32;
    ObserveRenderType(ctx, base, object);
    const uint32_t table = REX_LOAD_U32(object);
    const uint32_t getter = REX_LOAD_U32(table + 28);
    uint32_t matrix = 0;
    // These exact leaf getters were disassembled. Other implementations need
    // classification; do not execute a virtual getter an extra time to probe it.
    if (getter == 0x827AB060) matrix = REX_LOAD_U32(object + 12);
    if (getter == 0x8248A848) matrix = object + 32;
    if (matrix) {
      doax2::MatrixBits values;
      for (size_t i = 0; i < values.size(); ++i) values[i] = REX_LOAD_U32(matrix + uint32_t(i * 4));
      render_history.Observe({object, table, matrix, REX_LOAD_U32(object + 8), 2}, values);
    }
  }
  __imp__sub_8275A800(ctx, base);
}

REX_HOOK_RAW(sub_8275BDD0) {
  if (REXCVAR_GET(doax2_render_queue_probe)) {
    const uint32_t object = ctx.r4.u32;
    ObserveRenderType(ctx, base, object);
    const uint32_t table = REX_LOAD_U32(object);
    // Known forwarding methods dereference object+4 as the render delegate.
    // Only inspect the child layout for the exact verified forwarding pair.
    if (REX_LOAD_U32(table + 16) == 0x82470660 &&
        REX_LOAD_U32(table + 20) == 0x8275EF00) {
      const uint32_t child = REX_LOAD_U32(object + 4);
      if (child) ObserveRenderType(ctx, base, child, true);
    }
  }
  __imp__sub_8275BDD0(ctx, base);
}

// Observe exactly the record fields consumed by this dispatcher. Do not read
// payload memory until callback-specific ownership and sizes are established.
// The original function still executes exactly once with its original context.
REX_HOOK_RAW(sub_827571F8) {
  if (REXCVAR_GET(doax2_render_queue_probe)) {
    struct Seen { uint32_t caller; uint32_t callback; };
    static thread_local std::array<Seen, 128> seen{};
    static thread_local size_t seen_count = 0;
    static thread_local uint64_t calls = 0;
    constexpr uint32_t starts[] = {116, 120, 124, 136, 128, 132};
    constexpr uint32_t ends[] = {24, 40, 56, 104, 72, 88};
    const uint32_t queue = ctx.r3.u32;
    const uint32_t caller = static_cast<uint32_t>(ctx.lr);
    // Bound diagnostic cost. All dereferenced fields are also read by the
    // original dispatcher; malformed ranges are skipped by the observer.
    size_t entries = 0;
    size_t enabled = 0;
    for (size_t list = 0; list < 6; ++list) {
      const uint32_t first = REX_LOAD_U32(queue + starts[list]);
      const uint32_t end = REX_LOAD_U32(queue + ends[list]);
      if (end < first || ((first | end) & 3) || end - first > 4096 * 4) continue;
      for (uint32_t at = first; at < end && entries < 4096; at += 4) {
        ++entries;
        const uint32_t record = REX_LOAD_U32(at);
        if (!(REX_LOAD_U16(record + 12) & 1)) continue;
        ++enabled;
        const uint32_t callback = REX_LOAD_U32(record + 4);
        bool known = false;
        for (size_t i = 0; i < seen_count; ++i) {
          known |= seen[i].caller == caller && seen[i].callback == callback;
        }
        if (known || seen_count == seen.size()) continue;
        seen[seen_count++] = {caller, callback};
        REXLOG_INFO("DOAX2 CPU render queue: caller {:08X}, list {}, callback {:08X}, payload {:08X}, record {:08X}; observation only",
                    caller, list, callback, REX_LOAD_U32(record + 8), record);
      }
    }
    if ((++calls % 120) == 0) {
      REXLOG_INFO("DOAX2 CPU render queue: {} dispatcher calls, {} entries / {} enabled in this invocation, {} distinct caller/callback pairs (capacity 128)",
                  calls, entries, enabled, seen_count);
    }
  }
  __imp__sub_827571F8(ctx, base);
}

REX_EXTERN(__imp__sub_8258C910);
REX_EXTERN(__imp__sub_8258E000);
REX_EXTERN(__imp__sub_8258E500);
REX_EXTERN(__imp__sub_8258E0D8);
REX_EXTERN(__imp__sub_8273C140);
REX_EXTERN(__imp__sub_826F90B0);
REX_EXTERN(__imp__sub_82754E68);
REX_EXTERN(__imp__sub_8273BCA0);
REX_EXTERN(__imp__sub_827033A8);
REX_EXTERN(__imp__sub_82704D00);
REX_EXTERN(__imp__sub_82705BC8);
REX_EXTERN(__imp__sub_82705D50);
REX_EXTERN(__imp__sub_82704B40);
REX_EXTERN(__imp__sub_82703B90);
REX_EXTERN(__imp__sub_82710F08);
REX_EXTERN(__imp__sub_827115F8);
REX_EXTERN(__imp__sub_82711270);
REX_EXTERN(__imp__sub_82711D60);
REX_EXTERN(__imp__sub_82704210);
REX_EXTERN(__imp__sub_82704C28);
REX_EXTERN(__imp__sub_8270AA18);
REX_EXTERN(__imp__sub_827008B8);

namespace {

struct StageTiming {
  uint64_t total_us = 0;
  uint64_t max_us = 0;
  uint64_t calls = 0;
};

StageTiming timing_begin_frame;
StageTiming timing_update;
StageTiming timing_render_present;
StageTiming timing_guest_present;
StageTiming timing_present_prepare;
StageTiming timing_present_flush;
StageTiming timing_present_queue;
StageTiming timing_present_swap;
StageTiming timing_swap_flush;
StageTiming timing_swap_buffer;
StageTiming timing_swap_surface;
StageTiming timing_swap_edram;
StageTiming timing_swap_fence;
StageTiming timing_swap_wait;
StageTiming timing_swap_retire;
StageTiming timing_swap_finalize;
uint64_t timing_frames = 0;
bool inside_guest_present = false;
double timing_channel_a_seconds = 0.0;
double timing_channel_b_seconds = 0.0;
double timing_channel_a_rate = 0.0;
double timing_channel_b_rate = 0.0;
uint64_t timing_channel_samples = 0;
double time_probe_channel_a_seconds = 0.0;
double time_probe_channel_b_seconds = 0.0;
double time_probe_channel_a_rate = 0.0;
double time_probe_channel_b_rate = 0.0;
double time_probe_common_delta = 0.0;
float time_probe_common_min = std::numeric_limits<float>::infinity();
float time_probe_common_max = -std::numeric_limits<float>::infinity();
uint64_t time_probe_channel_samples = 0;
uint64_t time_probe_frames = 0;

void RecordStage(StageTiming& timing,
                 std::chrono::steady_clock::time_point start) {
  const uint64_t elapsed_us = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
  timing.total_us += elapsed_us;
  ++timing.calls;
  if (elapsed_us > timing.max_us) {
    timing.max_us = elapsed_us;
  }
}

void ResetStage(StageTiming& timing) { timing = {}; }

}  // namespace

// Keep the end-of-frame hook for frame-rate telemetry. Earlier diagnostics
// proved that 0x83484F40 is normally zero and is not DOAX2's 30 Hz policy, so
// this hook must not manufacture an additional wait.
REX_HOOK_RAW(sub_8258C910) {
  static uint64_t frame_count = 0;
  static auto rate_window_start = std::chrono::steady_clock::now();
  static uint64_t overlay_window_frames = 0;
  static auto overlay_window_start = rate_window_start;

  doax2_engine_frame_count.fetch_add(1, std::memory_order_relaxed);
  ++overlay_window_frames;
  const auto overlay_now = std::chrono::steady_clock::now();
  const double overlay_elapsed =
      std::chrono::duration<double>(overlay_now - overlay_window_start).count();
  if (overlay_elapsed >= 0.5) {
    doax2_engine_fps.store(double(overlay_window_frames) / overlay_elapsed,
                           std::memory_order_relaxed);
    overlay_window_frames = 0;
    overlay_window_start = overlay_now;
  }

  if (REXCVAR_GET(doax2_engine_60fps)) {
    ++frame_count;
    if (frame_count % 300 == 0) {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - rate_window_start).count();
      if (elapsed > 0.0) {
        REXLOG_INFO("DOAX2 engine frame rate: {:.1f} FPS ({} frames / {:.2f} s)",
                    300.0 / elapsed, 300, elapsed);
      }
      rate_window_start = now;
    }
  }

  ProbeMusicSetting(base);

  __imp__sub_8258C910(ctx, base);
}

// The vblank-countdown experiment formerly hooked sub_82705B38 here. Runtime
// validation showed that the field is a signed relative semaphore (for
// example, -26 was observed as 4294967270), not a literal frame-count policy.
// More importantly, shortening it runs the entire fixed-step game loop twice
// as often. Keep the guest's original simulation and animation cadence intact;
// the 60 Hz implementation belongs at the renderer boundary.

// Stage telemetry identifies the remaining 30 Hz gate without changing game
// behavior. DOAX2 moves from 60 Hz in menus to 30 Hz in gameplay; measuring
// each top-level stage tells us whether the wait is in timing, simulation, or
// the guest presentation path.
REX_HOOK_RAW(sub_8258E000) {
  const auto start = std::chrono::steady_clock::now();
  __imp__sub_8258E000(ctx, base);
  if (REXCVAR_GET(doax2_engine_60fps)) {
    RecordStage(timing_begin_frame, start);
    // sub_8258E000 publishes two independently sampled timing channels. Keep
    // them observational here until their consumers have been classified.
    timing_channel_a_seconds +=
        std::bit_cast<float>(REX_LOAD_U32(0x834A2C08));
    timing_channel_a_rate +=
        std::bit_cast<float>(REX_LOAD_U32(0x834A2C0C));
    timing_channel_b_rate +=
        std::bit_cast<float>(REX_LOAD_U32(0x834A2C10));
    timing_channel_b_seconds +=
        std::bit_cast<float>(REX_LOAD_U32(0x834A2C14));
    ++timing_channel_samples;
  }
  if (REXCVAR_GET(doax2_time_source_probe)) {
    time_probe_channel_a_seconds += std::bit_cast<float>(REX_LOAD_U32(0x834A2C08));
    time_probe_channel_a_rate += std::bit_cast<float>(REX_LOAD_U32(0x834A2C0C));
    time_probe_channel_b_rate += std::bit_cast<float>(REX_LOAD_U32(0x834A2C10));
    time_probe_channel_b_seconds += std::bit_cast<float>(REX_LOAD_U32(0x834A2C14));
    ++time_probe_channel_samples;
  }
}

// Pool Slide drop into the lake (results phase, trace log 444): player0
// only moved on step updates (6.6859, 6.6862, 6.9122, 6.9122, ...) and the
// camera zigzagged (5.05, 4.76, 5.35, 5.09, 6.04, 5.72): a period-2 pattern
// the phase midpoints do not reach (other code rewrites both every update).
// For display only: after the game update each listed float shows the mean of
// this and the previous update's exact value (a 2-tap filter removes any
// every-other-update component exactly, ~8 ms lag); the exact values are put
// back before the next update's logic.
namespace native60_display_filter {
struct Range { uint32_t guest, words; };
constexpr Range kRanges[] = {{0x833B8520, 64}, {0x839BC1D0, 128}};
constexpr size_t kWords = 64 + 128;
inline std::array<uint32_t, kWords> exact{}, prev{}, shown{};
inline std::array<uint8_t, kWords> written{};
inline bool have_prev = false, applied = false;
inline uint64_t last_frame = 0;
}  // namespace native60_display_filter
void Native60DisplayFilterRestore(uint8_t* base) {
  using namespace native60_display_filter;
  if (!applied) return;
  size_t k = 0;
  for (const Range& r : kRanges)
    for (uint32_t i = 0; i < r.words; ++i, ++k)
      if (written[k] && REX_LOAD_U32(r.guest + i * 4) == shown[k]) REX_STORE_U32(r.guest + i * 4, exact[k]);
  applied = false;
}
void Native60DisplayFilterApply(uint8_t* base) {
  using namespace native60_display_filter;
  const uint64_t frame = Doax2EngineFrameCount();
  const bool drop = false;  // no visible effect (log 445); kept for reference
  if (!drop) {
    have_prev = false;
    return;
  }
  size_t k = 0;
  for (const Range& r : kRanges)
    for (uint32_t i = 0; i < r.words; ++i, ++k) exact[k] = REX_LOAD_U32(r.guest + i * 4);
  const bool continuous = have_prev && last_frame + 1 == frame;
  last_frame = frame;
  written.fill(0);
  if (continuous && Native60SlideSmoothing() != 3) {
    k = 0;
    for (const Range& r : kRanges)
      for (uint32_t i = 0; i < r.words; ++i, ++k) {
        const uint32_t a = prev[k], b = exact[k];
        if (a == b || Native60PointerLike(a, b) || !Native60InterpolatableScanned(a, b)) continue;
        // Camera cuts / teleports: leave large steps alone.
        if (std::fabs(std::bit_cast<float>(a) - std::bit_cast<float>(b)) > 5.0f) continue;
        shown[k] = std::bit_cast<uint32_t>(0.5f * (std::bit_cast<float>(a) + std::bit_cast<float>(b)));
        written[k] = 1;
        REX_STORE_U32(r.guest + i * 4, shown[k]);
        applied = true;
      }
  }
  prev = exact;
  have_prev = true;
}

REX_HOOK_RAW(sub_8258E500) {
  Native60DisplayFilterRestore(base);
  const auto start = std::chrono::steady_clock::now();
  const auto native60_phase_before = SnapshotNative60Phase(base);
  const bool capture = REXCVAR_GET(doax2_render_queue_probe);
  if (capture) {
    if (capture_epoch.fetch_add(1) == 0)
      REXLOG_INFO("DOAX2 capture v1.3.25 enabled: optional same-phase host deformation consumption; no replay or timestep changes");
    render_history.Begin();
  }
  // 82E2DD44 is a statically initialized 1.0 with 24 static readers and no
  // static writers. Rewrite it before every update so an indirect writer
  // cannot silently restore it; log when the observed value was not ours.
  const float tick_scale = float(REXCVAR_GET(doax2_native60_tick_scale));
  static std::atomic<bool> config_logged{false};
#if defined(_WIN32)
  // The guest vblank worker (GraphicsSystem) polls with Sleep(1 ms); under
  // the default ~15.6 ms Windows timer resolution the 120 Hz guest vblanks
  // arrive in bursts and 60 Hz frames land 12-25 ms apart (logs 177-181:
  // ~20% of frames missed a display refresh, visible as hitching on fast
  // motion). Request 1 ms resolution for the 60 Hz mode.
  static std::atomic<bool> timer_resolution{false};
  if (REXCVAR_GET(doax2_engine_60fps) && !timer_resolution.exchange(true)) {
    const MMRESULT result = timeBeginPeriod(1);
    REXLOG_INFO("DOAX2 native60 timer resolution 1 ms requested: result={}",
                uint32_t(result));
  }
#endif
  if (!config_logged.exchange(true))
    REXLOG_INFO("DOAX2 native60 config: engine_60fps={} tick_scale={} "
                "phase_half={} keyframe_half={} scene_timeline_half={} "
                "scene_counters_half={} mode3_gate_half={} "
                "preview_motion_half={} time_source_probe={}",
                REXCVAR_GET(doax2_engine_60fps), tick_scale,
                REXCVAR_GET(doax2_native60_phase_half),
                REXCVAR_GET(doax2_native60_keyframe_half),
                REXCVAR_GET(doax2_native60_scene_timeline_half),
                REXCVAR_GET(doax2_native60_scene_counters_half),
                REXCVAR_GET(doax2_native60_mode3_gate_half),
                REXCVAR_GET(doax2_native60_preview_motion_half),
                REXCVAR_GET(doax2_time_source_probe));
  const uint32_t native60_index_before_sub_8258E500 = Native60SceneIndex(base);
  __imp__sub_8258E500(ctx, base);
  Native60SceneIndexCheck(base, "sub_8258E500", native60_index_before_sub_8258E500);
  ReportNative60GameStep(base);
  Native60DisplayFilterApply(base);
  ReportNative60PhaseBoundary(base, "sub_8258E500", native60_phase_before);
  if (capture) {
    const auto stats = render_history.End();
    static thread_local uint64_t frames = 0;
    if ((++frames % 120) == 0) {
      REXLOG_INFO("DOAX2 transform history v2: root frame {}, unique {}, repeated {}, ambiguous {}, dropped {}, adjacent address-tuples {}, changed {}; complete {}; observational only, lifetime identity unproven",
                  frames, stats.unique, stats.repeated, stats.ambiguous,
                  stats.dropped, stats.adjacent, stats.changed, stats.complete);
      ReportCapture();
    }
  }
  if (REXCVAR_GET(doax2_engine_60fps)) {
    RecordStage(timing_update, start);
  }
  if (REXCVAR_GET(doax2_time_source_probe)) {
    const float common_delta = std::bit_cast<float>(REX_LOAD_U32(0x82E2DD44));
    if (std::isfinite(common_delta)) {
      time_probe_common_delta += common_delta;
      time_probe_common_min = std::min(time_probe_common_min, common_delta);
      time_probe_common_max = std::max(time_probe_common_max, common_delta);
    }
    if ((++time_probe_frames % 120) == 0) {
      auto take = [](TimeProbeSite site) {
        return time_probe_calls[size_t(site)].exchange(0, std::memory_order_relaxed);
      };
      const uint64_t animation = take(TimeProbeSite::kAnimationKeyframe);
      const uint64_t procedural = take(TimeProbeSite::kProceduralPhase);
      const uint64_t camera_scene = take(TimeProbeSite::kCameraSceneTransform);
      const uint64_t effects = take(TimeProbeSite::kEffectOscillator);
      const uint64_t spring = take(TimeProbeSite::kSpringPhysics);
      const uint64_t gameplay = take(TimeProbeSite::kGameplayCountdowns);
      const uint64_t camera_publish = take(TimeProbeSite::kCameraPublish);
      const double samples = double(std::max<uint64_t>(time_probe_channel_samples, 1));
      REXLOG_INFO(
          "DOAX2 time-source census: frames 120, common unit 82E2DD44 avg/min/max {:.6f}/{:.6f}/{:.6f}; timing A value/rate {:.6f}/{:.2f}Hz, B value/rate {:.6f}/{:.2f}Hz; value units unresolved, observation only",
          time_probe_common_delta / 120.0, time_probe_common_min,
          time_probe_common_max, time_probe_channel_a_seconds / samples,
          time_probe_channel_a_rate / samples,
          time_probe_channel_b_seconds / samples,
          time_probe_channel_b_rate / samples);
      REXLOG_INFO(
          "DOAX2 time-source consumers: common_delta animation_keyframe(825A4550)={} procedural_phase(825B0028)={} camera_scene_candidate(825E04C8)={} effect_oscillator(82605298)={}; independent_fixed_step spring(825B7C08)={} gameplay_countdowns(8265E7B8)={}; camera_publish(82740E08)={}",
          animation, procedural, camera_scene, effects, spring, gameplay,
          camera_publish);
      time_probe_channel_a_seconds = time_probe_channel_b_seconds = 0.0;
      time_probe_channel_a_rate = time_probe_channel_b_rate = 0.0;
      time_probe_common_delta = 0.0;
      time_probe_common_min = std::numeric_limits<float>::infinity();
      time_probe_common_max = -std::numeric_limits<float>::infinity();
      time_probe_channel_samples = 0;
    }
  }
}

REX_HOOK_RAW(sub_8273C140) {
  const auto start = std::chrono::steady_clock::now();
  inside_guest_present = REXCVAR_GET(doax2_engine_60fps);
  __imp__sub_8273C140(ctx, base);
  inside_guest_present = false;
  if (REXCVAR_GET(doax2_engine_60fps)) {
    RecordStage(timing_guest_present, start);
  }
}

REX_HOOK_RAW(sub_8258E0D8) {
  const auto start = std::chrono::steady_clock::now();
  const uint32_t capture_caller = static_cast<uint32_t>(ctx.lr);
  __imp__sub_8258E0D8(ctx, base);
  if (REXCVAR_GET(doax2_native60_counter_scan)) {
    // Frame pacing histogram: wall time between guest presents.
    static auto last = std::chrono::steady_clock::now();
    static std::array<uint32_t, 6> buckets{};
    static uint32_t frames = 0;
    static double worst = 0;
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    const size_t bucket = ms < 12 ? 0 : ms < 15.5 ? 1 : ms < 18 ? 2 :
                          ms < 25 ? 3 : ms < 40 ? 4 : 5;
    ++buckets[bucket];
    worst = std::max(worst, ms);
    if (++frames == 300) {
      REXLOG_INFO("DOAX2 native60 frame pacing: <12ms={} 12-15.5={} "
                  "15.5-18={} 18-25={} 25-40={} >40={} worst={:.1f}ms",
                  buckets[0], buckets[1], buckets[2], buckets[3], buckets[4],
                  buckets[5], worst);
      buckets = {};
      frames = 0;
      worst = 0;
    }
  }
  if (REXCVAR_GET(doax2_render_queue_probe))
    CaptureEdge(0x8258E0D8, capture_caller, capture_parent, 0, 0, 0, 0);
  if (!REXCVAR_GET(doax2_engine_60fps)) {
    return;
  }

  RecordStage(timing_render_present, start);
  ++timing_frames;
  if (timing_frames % 300 != 0) {
    return;
  }

  constexpr double kWindowFrames = 300.0;
  REXLOG_INFO(
      "DOAX2 frame stages (avg/max ms): timing {:.2f}/{:.2f}, update "
      "{:.2f}/{:.2f}, render+present {:.2f}/{:.2f}, guest present "
      "{:.2f}/{:.2f}",
      timing_begin_frame.total_us / (1000.0 * kWindowFrames),
      timing_begin_frame.max_us / 1000.0,
      timing_update.total_us / (1000.0 * kWindowFrames),
      timing_update.max_us / 1000.0,
      timing_render_present.total_us / (1000.0 * kWindowFrames),
      timing_render_present.max_us / 1000.0,
      timing_guest_present.total_us / (1000.0 * kWindowFrames),
      timing_guest_present.max_us / 1000.0);
  if (timing_channel_samples) {
    REXLOG_INFO(
        "DOAX2 engine timing channels (average): A {:.6f} s / {:.2f} Hz, "
        "B {:.6f} s / {:.2f} Hz ({} samples)",
        timing_channel_a_seconds / timing_channel_samples,
        timing_channel_a_rate / timing_channel_samples,
        timing_channel_b_seconds / timing_channel_samples,
        timing_channel_b_rate / timing_channel_samples,
        timing_channel_samples);
  }
  REXLOG_INFO(
      "DOAX2 guest-present internals (total/frame ms, max ms, calls): prepare "
      "{:.2f}/{:.2f}/{}, flush {:.2f}/{:.2f}/{}, queue {:.2f}/{:.2f}/{}, "
      "swap {:.2f}/{:.2f}/{}",
      timing_present_prepare.total_us / (1000.0 * kWindowFrames),
      timing_present_prepare.max_us / 1000.0, timing_present_prepare.calls,
      timing_present_flush.total_us / (1000.0 * kWindowFrames),
      timing_present_flush.max_us / 1000.0, timing_present_flush.calls,
      timing_present_queue.total_us / (1000.0 * kWindowFrames),
      timing_present_queue.max_us / 1000.0, timing_present_queue.calls,
      timing_present_swap.total_us / (1000.0 * kWindowFrames),
      timing_present_swap.max_us / 1000.0, timing_present_swap.calls);
  REXLOG_INFO(
      "DOAX2 swap internals A (total/frame ms, max ms, calls): flush "
      "{:.2f}/{:.2f}/{}, buffer {:.2f}/{:.2f}/{}, surface "
      "{:.2f}/{:.2f}/{}, edram {:.2f}/{:.2f}/{}",
      timing_swap_flush.total_us / (1000.0 * kWindowFrames),
      timing_swap_flush.max_us / 1000.0, timing_swap_flush.calls,
      timing_swap_buffer.total_us / (1000.0 * kWindowFrames),
      timing_swap_buffer.max_us / 1000.0, timing_swap_buffer.calls,
      timing_swap_surface.total_us / (1000.0 * kWindowFrames),
      timing_swap_surface.max_us / 1000.0, timing_swap_surface.calls,
      timing_swap_edram.total_us / (1000.0 * kWindowFrames),
      timing_swap_edram.max_us / 1000.0, timing_swap_edram.calls);
  REXLOG_INFO(
      "DOAX2 swap internals B (total/frame ms, max ms, calls): fence "
      "{:.2f}/{:.2f}/{}, wait {:.2f}/{:.2f}/{}, retire "
      "{:.2f}/{:.2f}/{}, finalize {:.2f}/{:.2f}/{}",
      timing_swap_fence.total_us / (1000.0 * kWindowFrames),
      timing_swap_fence.max_us / 1000.0, timing_swap_fence.calls,
      timing_swap_wait.total_us / (1000.0 * kWindowFrames),
      timing_swap_wait.max_us / 1000.0, timing_swap_wait.calls,
      timing_swap_retire.total_us / (1000.0 * kWindowFrames),
      timing_swap_retire.max_us / 1000.0, timing_swap_retire.calls,
      timing_swap_finalize.total_us / (1000.0 * kWindowFrames),
      timing_swap_finalize.max_us / 1000.0, timing_swap_finalize.calls);

  ResetStage(timing_begin_frame);
  ResetStage(timing_update);
  ResetStage(timing_render_present);
  ResetStage(timing_guest_present);
  ResetStage(timing_present_prepare);
  ResetStage(timing_present_flush);
  ResetStage(timing_present_queue);
  ResetStage(timing_present_swap);
  ResetStage(timing_swap_flush);
  ResetStage(timing_swap_buffer);
  ResetStage(timing_swap_surface);
  ResetStage(timing_swap_edram);
  ResetStage(timing_swap_fence);
  ResetStage(timing_swap_wait);
  ResetStage(timing_swap_retire);
  ResetStage(timing_swap_finalize);
  timing_channel_a_seconds = 0.0;
  timing_channel_b_seconds = 0.0;
  timing_channel_a_rate = 0.0;
  timing_channel_b_rate = 0.0;
  timing_channel_samples = 0;
}

#define DOAX2_PROFILE_PRESENT_HOOK(function_name, timing_bucket)        \
  REX_HOOK_RAW(function_name) {                                        \
    const bool profile = inside_guest_present;                         \
    const auto start = std::chrono::steady_clock::now();               \
    __imp__##function_name(ctx, base);                                  \
    if (profile) {                                                      \
      RecordStage(timing_bucket, start);                               \
    }                                                                  \
  }

#define DOAX2_PROFILE_PASS_HOOK(name, address) \
  REX_HOOK_RAW(name) { \
    RecordPassCall(address, ctx); \
    const bool profile = inside_guest_present; \
    const auto start = std::chrono::steady_clock::now(); \
    __imp__##name(ctx, base); \
    if (profile) RecordStage(timing_present_prepare, start); \
  }
DOAX2_PROFILE_PASS_HOOK(sub_826F90B0, 0x826F90B0)
DOAX2_PROFILE_PASS_HOOK(sub_82754E68, 0x82754E68)
#undef DOAX2_PROFILE_PASS_HOOK
DOAX2_PROFILE_PRESENT_HOOK(sub_8273BCA0, timing_present_prepare)
DOAX2_PROFILE_PRESENT_HOOK(sub_827033A8, timing_present_flush)
DOAX2_PROFILE_PRESENT_HOOK(sub_82704D00, timing_present_queue)
DOAX2_PROFILE_PRESENT_HOOK(sub_82705BC8, timing_present_queue)
DOAX2_PROFILE_PRESENT_HOOK(sub_82705D50, timing_present_swap)
DOAX2_PROFILE_PRESENT_HOOK(sub_82704B40, timing_swap_flush)
DOAX2_PROFILE_PRESENT_HOOK(sub_82703B90, timing_swap_buffer)
DOAX2_PROFILE_PRESENT_HOOK(sub_82710F08, timing_swap_surface)
DOAX2_PROFILE_PRESENT_HOOK(sub_827115F8, timing_swap_surface)
DOAX2_PROFILE_PRESENT_HOOK(sub_82711270, timing_swap_surface)
DOAX2_PROFILE_PRESENT_HOOK(sub_82711D60, timing_swap_edram)
DOAX2_PROFILE_PRESENT_HOOK(sub_82704210, timing_swap_fence)
DOAX2_PROFILE_PRESENT_HOOK(sub_82704C28, timing_swap_wait)
DOAX2_PROFILE_PRESENT_HOOK(sub_8270AA18, timing_swap_retire)
DOAX2_PROFILE_PRESENT_HOOK(sub_827008B8, timing_swap_finalize)

#undef DOAX2_PROFILE_PRESENT_HOOK

// Jet-ski race diagnostics: per-update change statistics over the racer
// blocks found by the counter scan (4 records 0x344 apart at 834CDB00 and
// 4 records 0xA0 apart at 834F7E00). Update-to-update deltas, reported every
// 300 updates, for comparison between 60 Hz and the 30 Hz reference.
void ReportNative60StaticRegions(uint8_t* base) {
  struct Block { const char* name; uint32_t address; uint32_t words; };
  static const std::array<Block, 8> blocks = {{
      {"jet_racer0", 0x834CDB00, 209}, {"jet_racer1", 0x834CDE44, 209},
      {"jet_racer2", 0x834CE188, 209}, {"jet_racer3", 0x834CE4CC, 209},
      {"jet_rec0", 0x834F7E00, 40}, {"jet_rec1", 0x834F7EA0, 40},
      {"jet_rec2", 0x834F7F40, 40}, {"jet_rec3", 0x834F7FE0, 40}}};
  static std::array<Native60RegionStats, 8> stats;
  static bool begun = false;
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (begun) stats[i].End(base);
    stats[i].name = blocks[i].name;
    stats[i].Begin(base, blocks[i].address, blocks[i].words);
  }
  begun = true;
}
