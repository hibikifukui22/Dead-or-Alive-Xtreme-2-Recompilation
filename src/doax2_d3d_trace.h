#pragma once
// Direct3D call trace (diagnostics for the native renderer, doax2_d3d_trace):
// records the render-pass structure of sampled frames from the game's D3D
// entry points (render targets, clears, draws, resolves, present).
#include <cstdint>

struct PPCContext;

namespace doax2_d3d_trace {

// Guest D3D entry points (see docs/NATIVE-RENDERER.md).
constexpr uint32_t kSetRenderTarget = 0x82703438;   // (device, index, surface)
constexpr uint32_t kSetDepthStencil = 0x82703768;   // (device, surface)
constexpr uint32_t kResolve = 0x826F90B0;           // (device, flags, src rect*, dest texture*, dest point*)
constexpr uint32_t kDrawIndexed = 0x826FA358;       // (device, prim, ?, start index, index count)
constexpr uint32_t kDrawUP = 0x826F9E58;            // (device, prim, vertex count, stride)
constexpr uint32_t kClear = 0x826F7BE8;             // internal: (device, flags, rt index, rect...)
constexpr uint32_t kClearF = 0x826F86D0;            // (device, flags, rect*, color float4*; f1 = z, r7 = stencil)
constexpr uint32_t kBeginTiling = 0x827088B8;       // (device, ?, tile count, tile rects*, ...)
constexpr uint32_t kEndTiling = 0x82708CC0;         // (device, ...)
constexpr uint32_t kMakeRoom = 0x82704B40;          // (device) -> command buffer write pointer

// Argument registers saved on entry, for work after the original call.
struct Args {
  uint32_t r3, r4, r5, r6, r7, r8;
  static Args From(const PPCContext& ctx);
};

bool Enabled();
// One D3D call (registers as on entry).
void Call(uint32_t function, PPCContext& ctx, uint8_t* base);
// After the original call returned: writes the native renderer's in-band
// marker (doax2_native_markers) into the D3D command buffer.
void After(uint32_t function, const Args& args, PPCContext& ctx, uint8_t* base);
// ClearF: marker before the call (returns whether one was written; then
// ClearEnd writes the closing marker after the call).
bool ClearBegin(PPCContext& ctx, uint8_t* base);
void ClearEnd(const Args& args, PPCContext& ctx, uint8_t* base);
// End of a frame (present).
void Present(uint8_t* base);

}  // namespace doax2_d3d_trace
