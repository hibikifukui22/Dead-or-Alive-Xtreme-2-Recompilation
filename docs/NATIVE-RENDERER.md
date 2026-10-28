# Native renderer

The launcher's **Native Renderer (Experimental)** option (on by default since
1.1.5) makes the GPU emulation aware of the game's Direct3D render passes
instead of reconstructing everything from the Xbox 360's 10 MB EDRAM.

## How it works

The game's Direct3D library (XDK 2.0.3529, D3D9 link-time code generation) is
hooked at a few entry points in `src/doax2_60fps.cpp`; `src/doax2_d3d_trace.cpp`
writes **in-band markers** into the D3D command buffer at each call: a PM4
type-3 NOP whose payload starts with `DXNR` (0x44584E52), then a kind and its
values. The GPU command processor (`sdk/.../graphics/command_processor.cpp`)
sees them in order with the draws (`CommandProcessor::OnNativeMarker`).

| Kind | D3D call | Address | Payload |
| --- | --- | --- | --- |
| 1 | SetRenderTarget | 82703438 | index, surface, RB_SURFACE_INFO, RB_COLOR_INFO, dims, size |
| 2 | SetDepthStencilSurface | 82703768 | surface, RB_SURFACE_INFO, RB_DEPTH_INFO, dims, size |
| 3 | BeginTiling | 827088B8 | tile count, x1 y1 x2 y2 per tile |
| 4 | EndTiling (before its resolves) | 82708CC0 | - |
| 5 | ClearF (before) | 826F86D0 | flags, rect, color, Z, stencil, RT0 and depth surface words |
| 6 | ClearF (after) | 826F86D0 | - |

Surface object: +0x18 RB_SURFACE_INFO, +0x1C RB_COLOR_INFO / RB_DEPTH_INFO,
+0x24 `((width-1) << 18) | ((height-1) << 3)`, +0x2C size.

Runtime settings (all off unless the launcher turns them on):

- `doax2_native_markers` (game): write the markers.
- `gpu_native_tiling`: **predicated tiling collapse.** 1280x720 with 2x MSAA
  colour and depth does not fit the EDRAM, so the game renders the main scene
  in two 640x720 tiles, replaying the whole command buffer section once per
  tile. With host render targets there is no EDRAM limit: the first replay is
  drawn at full size (surface pitch and scissors widened, bin predication
  ignored for draws, resolves widened and sourced from the full-size render
  target) and the later replays are skipped. About half the draws in 3D scenes.
- `gpu_native_clears`: **native clears.** D3D's ClearF draws rectangles,
  partly through MSAA aliasing tricks, which the EDRAM model turns into
  render-target ownership transfers. A clear of whole surfaces (8-bit colour,
  depth + stencil) is done as a host clear of the render targets instead
  (`D3D12RenderTargetCache::NativeClear`) and its draws are skipped.
- `gpu_force_1x_msaa` (launcher MSAA Mode "Off"): the game's multisampled
  surfaces are drawn and resolved single-sampled (RB_SURFACE_INFO rewritten
  per draw). D3D's own clear draws keep their sample counts, since they alias
  surfaces with other sample counts; the clear values reach the single-sampled
  render targets through the EDRAM ownership transfers. Needs the markers.
- `gpu_native_marker_log`: diagnostics, logs one frame's markers and register
  state every 5 seconds.

## Measured (RTX 3090, 60 FPS mode)

| Scene | Emulated | Native |
| --- | --- | --- |
| Pool Side, 1x: command processor draws / busy | 1766 / 7.6 ms | 873 / 5.7 ms |
| Pool Side, 4K: GPU busy | 13.3 ms | 12.0 ms |
| Swimsuit menu, 4K: GPU busy | 18.2 ms | 17.4 ms |

## Next steps

Remaining render-target ownership transfers (render-to-texture passes that
alias EDRAM), an MSAA-off mode (1x host render targets for the game's 2x
surfaces, relying on FXAA), then the Vulkan backend for Android.
