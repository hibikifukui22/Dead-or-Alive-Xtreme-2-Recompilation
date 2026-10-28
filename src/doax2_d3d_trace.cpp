// Direct3D call trace for the native renderer work. With doax2_d3d_trace on,
// every 5 seconds one complete frame is written to
// out/game/logs/d3d_trace_<run>.txt as a render-pass list: render target /
// depth surface changes, clears, draw counts between them and resolves (source
// rectangle, destination texture fetch constant). Every surface and texture
// object seen is described once with its raw words, so their layouts can be
// decoded offline.
#include "generated/default/doax2_pch.h"
#include "doax2_d3d_trace.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(doax2_d3d_trace, false, "DOAX2",
                    "Diagnostics: write the D3D render-pass structure of one frame every 5 s to "
                    "logs/d3d_trace_<run>.txt");
REXCVAR_DEFINE_BOOL(doax2_native_markers, false, "DOAX2",
                    "Native renderer: write in-band markers (render target / depth surface, "
                    "tiling) into the D3D command buffer for the GPU command processor");

REX_EXTERN(__imp__sub_82704B40);

namespace doax2_d3d_trace {

// In-band marker: a PM4 NOP whose payload starts with this magic ('DXNR'),
// then the kind and its values (see rex::graphics::CommandProcessor::OnNativeMarker).
constexpr uint32_t kMarkerMagic = 0x44584E52;
enum MarkerKind : uint32_t {
  kMarkerRenderTarget = 1,  // index, surface, pitch|width, info (RB_COLOR_INFO), dims, size
  kMarkerDepthStencil = 2,  // surface, pitch|width, info (RB_DEPTH_INFO), dims, size
  kMarkerBeginTiling = 3,   // tile count, then x1 y1 x2 y2 per tile (up to 4)
  kMarkerEndTiling = 4,
  // flags, rect x1 y1 x2 y2, color rgba (float bits), z (float bits), stencil,
  // RT0 surface info / color info / dims, depth surface info / depth info / dims,
  // device RB_SURFACE_INFO
  kMarkerClearBegin = 5,
  kMarkerClearEnd = 6,
};

Args Args::From(const PPCContext& ctx) {
  return {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32};
}

namespace {

std::mutex mutex;
FILE* file = nullptr;
bool capturing = false;      // the current frame is being written
uint64_t frame = 0;
auto next_capture = std::chrono::steady_clock::now() + std::chrono::seconds(10);
uint32_t draws_indexed = 0, draws_up = 0, indices = 0;
std::set<uint32_t> described;  // surface / texture objects already described
std::vector<std::string> lines;

std::string Words(uint8_t* base, uint32_t address, int count) {
  std::string out;
  if (address < 0x40000000u || address >= 0xA0000000u) return "(invalid)";
  for (int i = 0; i < count; ++i) out += fmt::format("{}{:08X}", i ? " " : "", REX_LOAD_U32(address + i * 4));
  return out;
}

void Describe(uint8_t* base, const char* kind, uint32_t object, int words) {
  if (!object || !described.insert(object).second) return;
  lines.push_back(fmt::format("  [{} {:08X}] {}", kind, object, Words(base, object, words)));
}

void FlushDraws() {
  if (draws_indexed || draws_up) {
    lines.push_back(fmt::format("  draws: {} indexed ({} indices), {} UP", draws_indexed, indices, draws_up));
    draws_indexed = draws_up = indices = 0;
  }
}

bool ValidGuest(uint32_t address) { return address >= 0x40000000u && address < 0xA0000000u; }

// Appends a marker to the D3D command buffer the way D3D itself writes
// packets: the device's write pointer (+0x28) is the last dword written; past
// the soft end (+0x30) the buffer is advanced by D3D's own make-room routine.
void EmitMarker(PPCContext& ctx, uint8_t* base, uint32_t device, const uint32_t* payload,
                uint32_t count) {
  uint32_t write = REX_LOAD_U32(device + 0x28);
  if (write > REX_LOAD_U32(device + 0x30)) {
    PPCContext call = ctx;
    call.r3.u64 = device;
    __imp__sub_82704B40(call, base);
    write = call.r3.u32;
  }
  const uint32_t words = count + 1;  // magic + payload
  REX_STORE_U32(write + 4, 0xC0001000u | ((words - 1) << 16));  // type-3 NOP
  REX_STORE_U32(write + 8, kMarkerMagic);
  for (uint32_t i = 0; i < count; ++i) REX_STORE_U32(write + 12 + i * 4, payload[i]);
  REX_STORE_U32(device + 0x28, write + 4 * (words + 1));
}

// Surface values: pitch|width (+0x18), RB_*_INFO (+0x1C), dims (+0x24), size (+0x2C).
void SurfaceWords(uint8_t* base, uint32_t surface, uint32_t* out) {
  if (!ValidGuest(surface)) {
    out[0] = out[1] = out[2] = out[3] = 0;
    return;
  }
  out[0] = REX_LOAD_U32(surface + 0x18);
  out[1] = REX_LOAD_U32(surface + 0x1C);
  out[2] = REX_LOAD_U32(surface + 0x24);
  out[3] = REX_LOAD_U32(surface + 0x2C);
}

}  // namespace

void After(uint32_t function, const Args& args, PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(doax2_native_markers) || !ValidGuest(args.r3)) return;
  const uint32_t device = args.r3;
  uint32_t payload[2 + 4 * 4] = {};
  switch (function) {
    case kSetRenderTarget: {
      payload[0] = kMarkerRenderTarget;
      payload[1] = args.r4;
      payload[2] = ValidGuest(args.r5) ? args.r5 : 0;
      SurfaceWords(base, args.r5, payload + 3);
      EmitMarker(ctx, base, device, payload, 7);
      return;
    }
    case kSetDepthStencil: {
      payload[0] = kMarkerDepthStencil;
      payload[1] = ValidGuest(args.r4) ? args.r4 : 0;
      SurfaceWords(base, args.r4, payload + 2);
      EmitMarker(ctx, base, device, payload, 6);
      return;
    }
    case kBeginTiling: {
      const uint32_t tiles = std::min<uint32_t>(args.r5, 4);
      payload[0] = kMarkerBeginTiling;
      payload[1] = args.r5;
      if (ValidGuest(args.r6))
        for (uint32_t i = 0; i < tiles * 4; ++i) payload[2 + i] = REX_LOAD_U32(args.r6 + i * 4);
      EmitMarker(ctx, base, device, payload, 2 + tiles * 4);
      return;
    }
    default:
      return;
  }
}

bool ClearBegin(PPCContext& ctx, uint8_t* base) {
  const uint32_t device = ctx.r3.u32;
  if (!REXCVAR_GET(doax2_native_markers) || !ValidGuest(device)) return false;
  uint32_t flags = ctx.r4.u32;
  const uint32_t rect = ctx.r5.u32, color = ctx.r6.u32;
  const uint32_t rt0 = REX_LOAD_U32(device + 0x2F88), ds = REX_LOAD_U32(device + 0x2F98);
  // Every clear is marked (its draws are told apart from the game's), but
  // flags 0 = not to be done natively: in tiling or the other mode where
  // ClearF clips to stored extents, or with several color targets (only
  // render target 0 is described).
  if (REX_LOAD_U8(device + 0x2A38) & 0x30) flags = 0;
  if (flags & 0xE)
    for (uint32_t i = 1; i < 4; ++i)
      if (REX_LOAD_U32(device + 0x2F88 + 4 * i)) flags = 0;
  uint32_t payload[19] = {};
  payload[0] = kMarkerClearBegin;
  payload[1] = flags;
  if (ValidGuest(rect)) {
    for (uint32_t i = 0; i < 4; ++i) payload[2 + i] = REX_LOAD_U32(rect + i * 4);
  } else {
    payload[2] = payload[3] = 0;
    payload[4] = payload[5] = 0xFFFFFFFFu;  // whole surface
  }
  if (ValidGuest(color))
    for (uint32_t i = 0; i < 4; ++i) payload[6 + i] = REX_LOAD_U32(color + i * 4);
  const float z = float(ctx.f1.f64);
  std::memcpy(&payload[10], &z, sizeof(z));
  payload[11] = ctx.r7.u32;
  if (ValidGuest(rt0)) {
    payload[12] = REX_LOAD_U32(rt0 + 0x18);
    payload[13] = REX_LOAD_U32(rt0 + 0x1C);
    payload[14] = REX_LOAD_U32(rt0 + 0x24);
  }
  if (ValidGuest(ds)) {
    payload[15] = REX_LOAD_U32(ds + 0x18);
    payload[16] = REX_LOAD_U32(ds + 0x1C);
    payload[17] = REX_LOAD_U32(ds + 0x24);
  }
  payload[18] = REX_LOAD_U32(device + 0x2800);  // RB_SURFACE_INFO as D3D will program it
  EmitMarker(ctx, base, device, payload, 19);
  return true;
}

void ClearEnd(const Args& args, PPCContext& ctx, uint8_t* base) {
  const uint32_t payload = kMarkerClearEnd;
  EmitMarker(ctx, base, args.r3, &payload, 1);
}

bool Enabled() { return REXCVAR_GET(doax2_d3d_trace); }

void Call(uint32_t function, PPCContext& ctx, uint8_t* base) {
  // EndTiling's marker goes before the call: EndTiling itself issues the
  // per-tile resolves, which the command processor must see as tiling's end.
  if (function == kEndTiling && REXCVAR_GET(doax2_native_markers) && ValidGuest(ctx.r3.u32)) {
    const uint32_t payload = kMarkerEndTiling;
    EmitMarker(ctx, base, ctx.r3.u32, &payload, 1);
  }
  if (!Enabled()) return;
  std::lock_guard lock(mutex);
  if (!capturing) return;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  switch (function) {
    case kDrawIndexed:
      ++draws_indexed;
      indices += ctx.r7.u32;
      return;
    case kDrawUP:
      ++draws_up;
      return;
    case kSetRenderTarget:
      FlushDraws();
      lines.push_back(fmt::format("RT{} = {:08X} (caller {:08X})", ctx.r4.u32, ctx.r5.u32, caller));
      Describe(base, "surface", ctx.r5.u32, 16);
      return;
    case kSetDepthStencil:
      FlushDraws();
      lines.push_back(fmt::format("DS = {:08X} (caller {:08X})", ctx.r4.u32, caller));
      Describe(base, "surface", ctx.r4.u32, 16);
      return;
    case kClear:
      FlushDraws();
      // Clear(device r3, flags r4, render target index r5, ...); f1 carries a float.
      lines.push_back(fmt::format("clear flags {:08X} rt {} (caller {:08X})", ctx.r4.u32, ctx.r5.s32, caller));
      return;
    case kResolve: {
      FlushDraws();
      const uint32_t rect = ctx.r5.u32, texture = ctx.r6.u32, point = ctx.r7.u32;
      lines.push_back(fmt::format("resolve flags {:08X} src rect [{}] -> texture {:08X} at point [{}] "
                                  "(caller {:08X})", ctx.r4.u32, rect ? Words(base, rect, 4) : "full",
                                  texture, point ? Words(base, point, 2) : "0,0", caller));
      Describe(base, "texture", texture, 14);
      return;
    }
    default:
      return;
  }
}

void Present(uint8_t* base) {
  if (!Enabled()) return;
  std::lock_guard lock(mutex);
  ++frame;
  if (capturing) {
    FlushDraws();
    if (!file) {
      const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      file = std::fopen(fmt::format("out/game/logs/d3d_trace_{}.txt", stamp).c_str(), "w");
    }
    if (file) {
      std::fprintf(file, "=== frame %llu\n", static_cast<unsigned long long>(frame));
      for (const auto& line : lines) std::fprintf(file, "%s\n", line.c_str());
      std::fflush(file);
    }
    lines.clear();
    capturing = false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now >= next_capture) {
    // Capture the next frame completely (from this present to the next).
    capturing = true;
    next_capture = now + std::chrono::seconds(5);
    draws_indexed = draws_up = indices = 0;
  }
}

}  // namespace doax2_d3d_trace
