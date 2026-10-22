#define UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr int kResolution = 100;
constexpr int kScale = 101;
constexpr int kAnisotropic = 102;
constexpr int kAntialiasing = 103;
constexpr int kVsync = 104;
constexpr int kFullscreen = 105;
constexpr int kLaunch = 106;
constexpr int kStatus = 107;
constexpr int kFrameRate = 108;
constexpr int kPostProcessAA = 109;
constexpr int kFpsOverlay = 111;
constexpr int kDiagnostics = 112;
constexpr int kCapture = 113;
constexpr int kDisplayLocked = 114;
constexpr int kAsyncOcclusion = 115;
constexpr int kBatchUploads = 116;
constexpr int kKeepSong = 117;

// Frame rate modes.
//   30  Stable 30 FPS: out/game/doax2.exe, production settings, no diagnostics.
//   65  60 FPS stable v1.0.0: out/game-60/doax2.60fps-stable.exe (own rexruntime.dll with
//       the XMP song info fix and keep-song option; out/game in the tester package) at native
//       60 FPS (base game speed halved, display-locked vblank), no diagnostics.
//       Permanent fixes are promoted here from the experimental build.
//   60  Native 60 FPS (experimental): out/game/doax2.native60-poc.exe with the
//       guest loop at 60 Hz and the title's base game speed halved.
//   31  30 FPS reference (experimental build): same executable and probes at
//       the original 30 Hz rate, for side-by-side diagnostic comparisons.
// Tester launcher (DOAX2_TESTER_LAUNCHER, target doax2_tester_launcher): only
// "60 FPS stable v1.0.0", optional diagnostic logging and the FPS overlay; game
// files are read from "extracted game files here".
#ifdef DOAX2_TESTER_LAUNCHER
constexpr bool kTester = true;
constexpr const wchar_t* kGameFolder = L"extracted game files here";
#else
constexpr bool kTester = false;
constexpr const wchar_t* kGameFolder = L"game";
#endif
constexpr int kStable30 = 30;
constexpr int kStable60 = 65;
constexpr int kNative60 = 60;
constexpr int kReference30 = 31;
// Same as 60 / 31 with diagnostics and the animation memory sweep (three
// snapshots of all guest memory: automatic in the Niki Marina intro, or F9
// pressed three times at matching moments; logs\anim_sweep_<rate>_<n>.txt).
constexpr int kNative60Sweep = 61;
constexpr int kReference30Sweep = 32;
// 60 FPS intro bisect A..E (71..75): one group of Niki Marina race-loop
// functions runs at half rate during the intro (doax2_native60_intro_bisect).
constexpr int kBisectFirst = 71, kBisectLast = 77;

struct Settings {
  int width = 1280;
  int height = 720;
  int scale = 1;
  int anisotropic = 3;
  bool native_2x_msaa = true;
  bool vsync = true;
  bool fullscreen = false;
  int frame_rate_mode = kStable30;
  int post_process_aa = 0;
  bool fps_overlay = true;
  bool diagnostics = false;
  bool capture = false;
  bool display_locked = true;
  bool async_occlusion = true;
  bool batch_uploads = true;
  bool keep_song = false;
};

std::filesystem::path g_root;
std::vector<std::pair<int, int>> g_modes;

std::filesystem::path ExecutableDirectory() {
  std::vector<wchar_t> buffer(32768);
  DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
  return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::map<std::string, std::string> ReadIni(const std::filesystem::path& path) {
  std::map<std::string, std::string> values;
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    const size_t separator = line.find('=');
    if (separator != std::string::npos) values[line.substr(0, separator)] = line.substr(separator + 1);
  }
  return values;
}

int ReadInt(const std::map<std::string, std::string>& values, const char* key, int fallback) {
  auto found = values.find(key);
  if (found == values.end()) return fallback;
  try { return std::stoi(found->second); } catch (...) { return fallback; }
}

bool ReadBool(const std::map<std::string, std::string>& values, const char* key, bool fallback) {
  auto found = values.find(key);
  if (found == values.end()) return fallback;
  return found->second == "true" || found->second == "1";
}

Settings LoadSettings() {
  Settings settings;
  const auto values = ReadIni(g_root / "config" / "launcher.ini");
  settings.width = std::clamp(ReadInt(values, "width", settings.width), 640, 4095);
  settings.height = std::clamp(ReadInt(values, "height", settings.height), 480, 4095);
  settings.scale = std::clamp(ReadInt(values, "resolution_scale", settings.scale), 1, 4);
  settings.anisotropic = std::clamp(ReadInt(values, "anisotropic_override", settings.anisotropic), -1, 5);
  settings.native_2x_msaa = ReadBool(values, "native_2x_msaa", settings.native_2x_msaa);
  settings.vsync = ReadBool(values, "vsync", settings.vsync);
  settings.fullscreen = ReadBool(values, "fullscreen", settings.fullscreen);
  settings.frame_rate_mode = ReadInt(values, "frame_rate_mode", settings.frame_rate_mode);
  // Retired diagnostic modes (60-73 of earlier launchers) fall back to stable.
  if (settings.frame_rate_mode != kStable30 && settings.frame_rate_mode != kStable60 &&
      settings.frame_rate_mode != kNative60 &&
      settings.frame_rate_mode != kReference30 && settings.frame_rate_mode != kNative60Sweep &&
      settings.frame_rate_mode != kReference30Sweep &&
      !(settings.frame_rate_mode >= kBisectFirst && settings.frame_rate_mode <= kBisectLast))
    settings.frame_rate_mode = kStable30;
  if (kTester) settings.frame_rate_mode = kStable60;
  settings.post_process_aa = std::clamp(ReadInt(values, "post_process_aa", settings.post_process_aa), 0, 2);
  settings.fps_overlay = ReadBool(values, "fps_overlay", settings.fps_overlay);
  settings.diagnostics = ReadBool(values, "diagnostics", settings.diagnostics);
  settings.capture = ReadBool(values, "f10_capture", settings.capture);
  settings.display_locked = ReadBool(values, "display_locked", settings.display_locked);
  settings.async_occlusion = ReadBool(values, "async_occlusion", settings.async_occlusion);
  settings.batch_uploads = ReadBool(values, "batch_uploads", settings.batch_uploads);
  settings.keep_song = ReadBool(values, "keep_song", settings.keep_song);
  return settings;
}

void SaveSettings(const Settings& settings) {
  std::filesystem::create_directories(g_root / "config");
  std::ofstream output(g_root / "config" / "launcher.ini", std::ios::trunc);
  output << "# DOAX2 Recomp launcher settings\n"
         << "width=" << settings.width << '\n'
         << "height=" << settings.height << '\n'
         << "resolution_scale=" << settings.scale << '\n'
         << "anisotropic_override=" << settings.anisotropic << '\n'
         << "native_2x_msaa=" << (settings.native_2x_msaa ? "true" : "false") << '\n'
         << "vsync=" << (settings.vsync ? "true" : "false") << '\n'
         << "fullscreen=" << (settings.fullscreen ? "true" : "false") << '\n'
         << "frame_rate_mode=" << settings.frame_rate_mode << '\n'
         << "post_process_aa=" << settings.post_process_aa << '\n'
         << "fps_overlay=" << (settings.fps_overlay ? "true" : "false") << '\n'
         << "diagnostics=" << (settings.diagnostics ? "true" : "false") << '\n'
         << "f10_capture=" << (settings.capture ? "true" : "false") << '\n'
         << "display_locked=" << (settings.display_locked ? "true" : "false") << '\n'
         << "async_occlusion=" << (settings.async_occlusion ? "true" : "false") << '\n'
         << "batch_uploads=" << (settings.batch_uploads ? "true" : "false") << '\n'
         << "keep_song=" << (settings.keep_song ? "true" : "false") << '\n';
}

HWND AddLabel(HWND parent, const wchar_t* text, int x, int y, int width = 220) {
  return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, 20, parent, nullptr, nullptr, nullptr);
}

HWND AddCombo(HWND parent, int id, int x, int y, int width) {
  return CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                         x, y, width, 260, parent, reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
}

HWND AddCheck(HWND parent, int id, const wchar_t* text, int x, int y, int width, bool checked) {
  HWND check = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                             x, y, width, 24, parent, reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
  SendMessageW(check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
  return check;
}

void AddComboValue(HWND combo, const wchar_t* label, LPARAM value, bool selected) {
  const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
  SendMessageW(combo, CB_SETITEMDATA, index, value);
  if (selected) SendMessageW(combo, CB_SETCURSEL, index, 0);
}

int ComboValue(HWND parent, int id, int fallback) {
  HWND combo = GetDlgItem(parent, id);
  const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (selection == CB_ERR) return fallback;
  const LRESULT value = SendMessageW(combo, CB_GETITEMDATA, selection, 0);
  return value == CB_ERR ? fallback : int(value);
}

bool Checked(HWND window, int id) {
  return SendMessageW(GetDlgItem(window, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void PopulateModes(HWND combo, const Settings& settings) {
  std::set<std::pair<int, int>> unique;
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  for (DWORD index = 0; EnumDisplaySettingsW(nullptr, index, &mode); ++index) {
    if (mode.dmPelsWidth >= 640 && mode.dmPelsWidth <= 4095 && mode.dmPelsHeight >= 480 && mode.dmPelsHeight <= 4095) {
      unique.emplace(int(mode.dmPelsWidth), int(mode.dmPelsHeight));
    }
  }
  unique.emplace(settings.width, settings.height);
  g_modes.assign(unique.begin(), unique.end());
  std::sort(g_modes.begin(), g_modes.end(), [](auto a, auto b) { return a.first * a.second < b.first * b.second; });
  for (size_t i = 0; i < g_modes.size(); ++i) {
    const auto [width, height] = g_modes[i];
    const std::wstring label = std::to_wstring(width) + L" x " + std::to_wstring(height);
    AddComboValue(combo, label.c_str(), LPARAM(i), width == settings.width && height == settings.height);
  }
}

Settings ReadControls(HWND window) {
  Settings settings;
  const int mode_index = ComboValue(window, kResolution, 0);
  if (mode_index >= 0 && mode_index < int(g_modes.size())) {
    settings.width = g_modes[mode_index].first;
    settings.height = g_modes[mode_index].second;
  }
  settings.scale = ComboValue(window, kScale, 1);
  settings.anisotropic = ComboValue(window, kAnisotropic, 3);
  settings.native_2x_msaa = ComboValue(window, kAntialiasing, 1) != 0;
  settings.vsync = Checked(window, kVsync);
  settings.fullscreen = Checked(window, kFullscreen);
  settings.frame_rate_mode = ComboValue(window, kFrameRate, kStable30);
  settings.post_process_aa = ComboValue(window, kPostProcessAA, 0);
  settings.fps_overlay = Checked(window, kFpsOverlay);
  settings.diagnostics = Checked(window, kDiagnostics);
  settings.capture = Checked(window, kCapture);
  settings.display_locked = Checked(window, kDisplayLocked);
  settings.async_occlusion = Checked(window, kAsyncOcclusion);
  settings.batch_uploads = Checked(window, kBatchUploads);
  settings.keep_song = Checked(window, kKeepSong);
  return settings;
}

std::wstring Quote(const std::filesystem::path& path) { return L"\"" + path.wstring() + L"\""; }

const wchar_t* PostProcessAAName(int setting) {
  switch (setting) {
    case 1: return L"fxaa";
    case 2: return L"fxaa_extreme";
    default: return L"none";
  }
}

void Launch(HWND window) {
  const Settings settings = ReadControls(window);
  const bool stable60 = settings.frame_rate_mode == kStable60;
  const bool experimental = settings.frame_rate_mode != kStable30 && !stable60;
  const bool sweep = settings.frame_rate_mode == kNative60Sweep ||
                     settings.frame_rate_mode == kReference30Sweep;
  const int bisect = settings.frame_rate_mode >= kBisectFirst && settings.frame_rate_mode <= kBisectLast
                         ? settings.frame_rate_mode - kBisectFirst + 1 : 0;
  const bool native60 = stable60 || settings.frame_rate_mode == kNative60 ||
                        settings.frame_rate_mode == kNative60Sweep || bisect != 0;
  // Diagnostics, capture and the overlay never apply to the stable build.
  const bool diagnostics = (experimental || kTester) && (settings.diagnostics || sweep);
  const bool capture = experimental && settings.capture;
  const bool fps_overlay = (experimental || kTester) && settings.fps_overlay;
  // rexgpu-xenos60.dll locks guest vblanks to the display refresh (even frame
  // delivery at 60 Hz); the stable build always keeps rexgpu-xenos.dll.
  const bool display_locked = experimental && settings.display_locked &&
      std::filesystem::exists(g_root / "out" / "game" / "rexgpu-xenos60.dll");
  if (native60 && !settings.vsync) {
    MessageBoxW(window,
                L"Native 60 FPS paces the game from the guest V-sync. Enable Guest V-sync "
                L"or select 30 FPS.",
                L"DOAX2 Launcher", MB_ICONINFORMATION | MB_OK);
    return;
  }
  // The experimental executable keeps one fixed name so the NVIDIA per-app
  // profile (RTX 3090) stays assigned between builds.
  // The experimental build (out/game-exp) and 60 FPS stable v1.0.0
  // (out/game-60) run from their own folders when present: their
  // rexruntime.dll (XMP song title/artist fix, keep-song option) leaves the
  // 30 FPS stable runtime in out/game untouched. The tester package keeps
  // its 60 FPS stable (with that runtime) in out/game.
  const bool experimental_folder =
      experimental && std::filesystem::exists(g_root / "out" / "game-exp" / "doax2.native60-poc.exe");
  const bool stable60_folder =
      stable60 && std::filesystem::exists(g_root / "out" / "game-60" / "doax2.60fps-stable.exe");
  const bool xmp_runtime = experimental_folder || stable60;
  const auto game_exe = g_root / "out" /
                        (experimental_folder ? "game-exp" : stable60_folder ? "game-60" : "game") /
                        (experimental ? L"doax2.native60-poc.exe"
                         // 60 FPS stable has its own executable so the 30 FPS
                         // stable stays untouched.
                         : stable60 ? L"doax2.60fps-stable.exe" : L"doax2.exe");
  const auto game_data = g_root / kGameFolder / "default.xex";
  if (!std::filesystem::exists(game_exe) || !std::filesystem::exists(game_data)) {
    MessageBoxW(window,
                (L"Missing " + game_exe.wstring() + L" or " + kGameFolder + L"\\default.xex.").c_str(),
                L"DOAX2 Launcher", MB_ICONERROR | MB_OK);
    return;
  }
  SaveSettings(settings);
  const auto true_false = [](bool value) { return value ? L"true" : L"false"; };
  std::wstring command = Quote(game_exe)
      + L" --game_data_root=" + Quote(g_root / kGameFolder)
      + L" --user_data_root=" + Quote(g_root / "userdata")
      // Validated D3D12 RTV path with the Intel stencil/viewport corrections.
      + L" --gpu_plugin=" + (display_locked ? L"xenos60" : L"xenos")
      + L" --render_target_path_d3d12=rtv"
      + L" --native_stencil_value_output_d3d12_intel=true"
      + L" --window_width=" + std::to_wstring(settings.width)
      + L" --window_height=" + std::to_wstring(settings.height)
      + L" --video_mode_width=" + std::to_wstring(settings.width)
      + L" --video_mode_height=" + std::to_wstring(settings.height)
      + L" --resolution_scale=" + std::to_wstring(settings.scale)
      + L" --anisotropic_override=" + std::to_wstring(settings.anisotropic)
      + L" --native_2x_msaa=" + true_false(settings.native_2x_msaa)
      + L" --vsync=" + true_false(settings.vsync)
      // Guest VBlank timing stays with the title; host presentation is not
      // serialized behind DXGI Present(1) (NVIDIA 20 FPS fallback fix).
      + L" --d3d12_vsync=false"
      + L" --d3d12_allow_variable_refresh_rate_and_tearing=false"
      + L" --d3d12_vsync_interval=1"
      // Native 60 FPS runs the guest at 120 Hz vblank (one 60 Hz frame per two
      // vblanks); the stable and reference modes keep the original 60 Hz.
      + L" --video_mode_refresh_rate=" + (native60 ? L"120" : L"60")
      + L" --vblank_counter_rate_multiplier=1"
      + L" --doax2_engine_60fps=" + true_false(experimental || stable60)
      // Stable baseline feature: Music Volume controls the host soundtrack.
      + L" --doax2_music_volume_bridge=true"
      + L" --gpu_allow_invalid_fetch_constants=true"
      + L" --fullscreen=" + true_false(settings.fullscreen)
      + L" --doax2_fps_overlay=" + true_false(fps_overlay)
      + L" --swap_post_effect=" + PostProcessAAName(settings.post_process_aa);
  if (experimental) {
    command +=
        // Base game speed: 0.5 at 60 Hz, original 1.0 for the 30 Hz reference.
        std::wstring(L" --doax2_native60_tick_scale=") + (native60 ? L"0.5" : L"1.0")
        // Retired per-function experiments stay off; the base-speed fixes
        // supersede them.
        + L" --doax2_native60_phase_half=false --doax2_native60_keyframe_half=false"
        + L" --doax2_native60_scene_timeline_half=false --doax2_native60_scene_counters_half=false"
        + L" --doax2_native60_mode3_gate_half=false --doax2_native60_preview_motion_half=false"
        + L" --doax2_native60_eval_probe=false --doax2_time_source_probe=false"
        // Diagnostic logging: counter scan, gameplay/AI probes, frame pacing.
        + L" --doax2_native60_counter_scan=" + true_false(diagnostics)
        // Experimental: F7 smoothing modes work without diagnostics.
        + L" --doax2_native60_dev_keys=true"
        // Menu transition stretch: 2 = 30 FPS timing (title popup, log 484/485).
        + L" --doax2_native60_menu_transition_scale=2"
        // Menu window pop in/out stretch (2 = 30 FPS timing; slower by request).
        + L" --doax2_native60_menu_window_scale=4"
        // Held-direction menu auto-repeat (2 = original 30 FPS speed).
        + L" --doax2_native60_menu_repeat_scale=2"
        // Music: keep the current song across scenes / minigames / events
        // (experimental runtime only).
        + (xmp_runtime ? std::wstring(L" --xmp_keep_song_across_playlists=") + true_false(settings.keep_song)
                               : std::wstring())
        // Diagnostics: hardware write breakpoints (armed at start, "*") on menu
        // counters: a scroll item counter and three popup timers (log 471).
        + (diagnostics ? std::wstring(L" --doax2_native60_write_watch=833B93E4,833B93E8,833B9400,833BB6A0,*")
                       : std::wstring())
        // Animation memory sweep (sweep modes only).
        + L" --doax2_native60_anim_sweep=" + true_false(sweep)
        + L" --doax2_native60_intro_bisect=" + std::to_wstring(bisect)
        // Automatic F10 bursts at fixed Niki Marina intro times (capture + diagnostics).
        + L" --doax2_native60_intro_autocapture=" + true_false(capture && diagnostics && display_locked)
        // F10 capture to out\\game\\captures: with the xenos60 renderer a burst
        // of 60 consecutive frames (any resolution); otherwise the older
        // two-frame final-swap copy (1280x720 only, needs the candidate audit).
        // xenos60 only: occlusion queries answered from the latest completed
        // GPU result instead of a full CPU/GPU sync per query.
        + (display_locked ? std::wstring(L" --d3d12_doax2_async_occlusion_query=") +
                                true_false(settings.async_occlusion)
                          : std::wstring())
        // Exact render target extents for screen-space draws (vertex shader
        // run on the CPU): small offscreen targets (swimsuit thumbnails,
        // reflections) no longer claim the whole EDRAM, which made every bind
        // copy the full 1280x720 frame in and out (2x the GPU time at 1440p).
        + std::wstring(L" --execute_unclipped_draw_vs_on_cpu=true"
                       L" --execute_unclipped_draw_vs_on_cpu_with_scissor=true")
        // Command-processor sampling profiler (xenos60 diagnostics only).
        + (display_locked && diagnostics ? std::wstring(L" --d3d12_doax2_cp_sampler=true") : std::wstring())
        // xenos60 only: shared memory uploads batched at earlier anchors
        // instead of a whole-buffer COPY_DEST transition pair per draw.
        // (Mid-frame submission splitting stays off: it raised per-draw cost.)
        + (display_locked ? std::wstring(L" --d3d12_doax2_submit_draw_interval=0"
                                         L" --d3d12_doax2_batch_uploads=") +
                                true_false(settings.batch_uploads)
                          : std::wstring())
        + (display_locked
               ? std::wstring(L" --d3d12_doax2_frame_burst_capture=") + true_false(capture)
                     + L" --d3d12_doax2_candidate_audit=false"
                     + L" --d3d12_doax2_owned_final_swap_copy=false"
                     + L" --d3d12_doax2_owned_final_swap_capture_hotkey=false"
               : std::wstring(L" --d3d12_doax2_candidate_audit=") + true_false(capture)
                     + L" --d3d12_doax2_owned_final_swap_copy=" + true_false(capture)
                     + L" --d3d12_doax2_owned_final_swap_capture_hotkey=" + true_false(capture))
        + L" --d3d12_doax2_owned_final_swap_capture_after=0";
  }
  if (!experimental) {
    // Stable builds (30 FPS and 60 FPS stable) share rexgpu-xenos.dll;
    // renderer performance fixes from the 1440p work:
    // exact screen-space draw extents and batched shared memory uploads.
    command += std::wstring(L" --execute_unclipped_draw_vs_on_cpu=true"
                            L" --execute_unclipped_draw_vs_on_cpu_with_scissor=true"
                            L" --d3d12_doax2_submit_draw_interval=0 --d3d12_doax2_batch_uploads=true"
                            L" --d3d12_doax2_async_occlusion_query=true")
               // 30 FPS keeps the original free-running vblank emitter; 60 FPS
               // locks guest vblanks to the display (even frame delivery).
               + L" --gpu_display_locked_vblank=" + true_false(stable60);
    if (stable60) {
      command += std::wstring(L" --doax2_native60_tick_scale=0.5")
                 + L" --doax2_native60_phase_half=false --doax2_native60_keyframe_half=false"
                 + L" --doax2_native60_scene_timeline_half=false --doax2_native60_scene_counters_half=false"
                 + L" --doax2_native60_mode3_gate_half=false --doax2_native60_preview_motion_half=false"
                 + L" --doax2_native60_eval_probe=false --doax2_time_source_probe=false"
                 + L" --doax2_native60_counter_scan=" + true_false(diagnostics)
                 + L" --doax2_native60_anim_sweep=false"
                 + L" --doax2_native60_intro_bisect=0 --doax2_native60_intro_autocapture=false"
                 // Menus: transitions / held repeat at the 30 FPS
                 // timing (defaults), window pop in/out slower by request.
                 + L" --doax2_native60_menu_transition_scale=2 --doax2_native60_menu_window_scale=4"
                 + L" --doax2_native60_menu_repeat_scale=2"
                 + L" --xmp_keep_song_across_playlists=" + true_false(settings.keep_song)
                 // Renderer timing profiler with diagnostics (tester logs).
                 + (diagnostics ? L" --d3d12_doax2_cp_sampler=true" : L"");
    }
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr, g_root.c_str(), &startup, &process)) {
    const DWORD error = GetLastError();
    MessageBoxW(window, (L"Unable to start DOAX2 (Windows error " + std::to_wstring(error) + L").").c_str(),
                L"DOAX2 Launcher", MB_ICONERROR | MB_OK);
    return;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  SetWindowTextW(GetDlgItem(window, kStatus), L"DOAX2 is running. Runtime logs are saved in out\\game\\logs.");
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      if (LOWORD(wparam) == kLaunch && HIWORD(wparam) == BN_CLICKED) Launch(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

void BuildControls(HWND window, const Settings& settings) {
  HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  auto set_font = [font](HWND control) { SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); };
  set_font(AddLabel(window, L"DOAX2 Recomp - stable 30 FPS / native 60 FPS experiment", 24, 18, 480));
  set_font(AddLabel(window, L"Display resolution", 24, 58));
  HWND resolutions = AddCombo(window, kResolution, 250, 54, 230);
  PopulateModes(resolutions, settings); set_font(resolutions);
  set_font(AddLabel(window, L"Resolution scale", 24, 98));
  HWND scale = AddCombo(window, kScale, 250, 94, 230);
  AddComboValue(scale, L"1x (native)", 1, settings.scale == 1);
  AddComboValue(scale, L"2x", 2, settings.scale == 2);
  AddComboValue(scale, L"3x", 3, settings.scale == 3);
  AddComboValue(scale, L"4x", 4, settings.scale == 4); set_font(scale);
  set_font(AddLabel(window, L"MSAA mode", 24, 138));
  HWND aa = AddCombo(window, kAntialiasing, 250, 134, 230);
  AddComboValue(aa, L"Native game 2x MSAA", 1, settings.native_2x_msaa);
  AddComboValue(aa, L"Emulated 2x MSAA (compatibility)", 0, !settings.native_2x_msaa); set_font(aa);
  set_font(AddLabel(window, L"Post-process AA", 24, 178));
  HWND post_process_aa = AddCombo(window, kPostProcessAA, 250, 174, 230);
  AddComboValue(post_process_aa, L"Off", 0, settings.post_process_aa == 0);
  AddComboValue(post_process_aa, L"FXAA", 1, settings.post_process_aa == 1);
  AddComboValue(post_process_aa, L"FXAA Extreme", 2, settings.post_process_aa == 2); set_font(post_process_aa);
  set_font(AddLabel(window, L"Anisotropic filtering", 24, 218));
  HWND anisotropic = AddCombo(window, kAnisotropic, 250, 214, 230);
  AddComboValue(anisotropic, L"Game default", -1, settings.anisotropic == -1);
  AddComboValue(anisotropic, L"Off", 0, settings.anisotropic == 0);
  AddComboValue(anisotropic, L"1x", 1, settings.anisotropic == 1);
  AddComboValue(anisotropic, L"2x", 2, settings.anisotropic == 2);
  AddComboValue(anisotropic, L"4x", 3, settings.anisotropic == 3);
  AddComboValue(anisotropic, L"8x", 4, settings.anisotropic == 4);
  AddComboValue(anisotropic, L"16x", 5, settings.anisotropic == 5); set_font(anisotropic);
  set_font(AddCheck(window, kVsync, L"Guest V-sync (recommended)", 24, 258, 210, settings.vsync));
  set_font(AddCheck(window, kFullscreen, L"Fullscreen", 250, 258, 180, settings.fullscreen));
  set_font(AddLabel(window, L"Frame rate", 24, 302));
  HWND frame_rate = AddCombo(window, kFrameRate, 250, 298, 230);
  if (kTester) {
    AddComboValue(frame_rate, L"60 FPS stable v1.0.0 (with logging)", kStable60, true);
  } else {
  AddComboValue(frame_rate, L"30 FPS (stable)", kStable30, settings.frame_rate_mode == kStable30);
  AddComboValue(frame_rate, L"60 FPS stable v1.0.0", kStable60, settings.frame_rate_mode == kStable60);
  AddComboValue(frame_rate, L"60 FPS (experimental)", kNative60, settings.frame_rate_mode == kNative60);
  AddComboValue(frame_rate, L"30 FPS reference (experimental build)", kReference30,
                settings.frame_rate_mode == kReference30);
  AddComboValue(frame_rate, L"60 FPS + animation memory sweep (diagnostics)", kNative60Sweep,
                settings.frame_rate_mode == kNative60Sweep);
  AddComboValue(frame_rate, L"30 FPS reference + animation memory sweep (diagnostics)", kReference30Sweep,
                settings.frame_rate_mode == kReference30Sweep);
  static const wchar_t* kBisectNames[7] = {
      L"60 FPS intro test A (jet update half rate)", L"60 FPS intro test B (rider half rate)",
      L"60 FPS intro test C (girl placement half rate)", L"60 FPS intro test D (race management half rate)",
      L"60 FPS intro test E (girls' character update half rate)",
      L"60 FPS intro test F (scaled step for non-camera tracks)",
      L"60 FPS intro test G (intro script waits half rate)"};
  for (int i = 0; i < 7; ++i)
    AddComboValue(frame_rate, kBisectNames[i], kBisectFirst + i,
                  settings.frame_rate_mode == kBisectFirst + i);
  }
  set_font(frame_rate);
  if (kTester) {
    set_font(AddLabel(window, L"Logs are written to out\\game\\logs.", 24, 340, 400));
    set_font(AddCheck(window, kDiagnostics, L"Diagnostic logging (detailed, lowers FPS)",
                      24, 362, 420, settings.diagnostics));
    set_font(AddCheck(window, kFpsOverlay, L"Show FPS overlay", 24, 388, 200, settings.fps_overlay));
    set_font(AddCheck(window, kKeepSong, L"Keep song playing across scenes and minigames",
                      24, 414, 420, settings.keep_song));
  } else {
  set_font(AddLabel(window, L"Experimental modes only:", 24, 340, 300));
  set_font(AddCheck(window, kDiagnostics, L"Diagnostic logging (counters, probes, frame pacing)",
                    24, 362, 420, settings.diagnostics));
  set_font(AddCheck(window, kCapture, L"F10 frame capture (60-frame burst with xenos60)",
                    24, 388, 420, settings.capture));
  set_font(AddCheck(window, kFpsOverlay, L"Show FPS overlay", 24, 414, 200, settings.fps_overlay));
  set_font(AddCheck(window, kDisplayLocked, L"Display-locked frame pacing (xenos60 renderer)",
                    24, 440, 420, settings.display_locked));
  set_font(AddCheck(window, kAsyncOcclusion, L"Async occlusion queries (performance, xenos60)",
                    24, 466, 420, settings.async_occlusion));
  set_font(AddCheck(window, kBatchUploads, L"Batch memory uploads (performance, xenos60)",
                    24, 492, 420, settings.batch_uploads));
  set_font(AddCheck(window, kKeepSong, L"Keep song playing across scenes (60 FPS modes)",
                    24, 518, 420, settings.keep_song));
  }
  const int launch_y = kTester ? 526 : 552;
  HWND launch = CreateWindowW(L"BUTTON", L"Launch DOAX2", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              24, launch_y, 456, 42, window, reinterpret_cast<HMENU>(INT_PTR(kLaunch)), nullptr, nullptr);
  set_font(launch);
  HWND status = AddLabel(window, L"Settings are saved automatically in config\\launcher.ini.", 24, launch_y + 54, 500);
  SetWindowLongPtrW(status, GWL_STYLE, GetWindowLongPtrW(status, GWL_STYLE) | SS_PATHELLIPSIS); set_font(status);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  g_root = ExecutableDirectory();
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);
  const wchar_t* const class_name = L"DOAX2RecompLauncher";
  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = class_name;
  RegisterClassExW(&window_class);
  HWND window = CreateWindowExW(0, class_name, kTester ? L"DOAX2 Recomp 60 FPS Test" : L"DOAX2 Recomp Launcher", WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 530, kTester ? 664 : 690, nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  BuildControls(window, LoadSettings());
  ShowWindow(window, show);
  UpdateWindow(window);
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  return int(message.wParam);
}
