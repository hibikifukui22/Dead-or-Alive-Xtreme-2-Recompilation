// DOAX2 Recomp launcher (public release).
//
// A single custom-drawn, resizable window (opens at 1600 x 900). Three pages:
// the main page (logo, frame rate, Play, Settings / Graphics Settings),
// Settings and Graphics Settings, each with a Back button. Everything is laid
// out in a 1600 x 900 design space, scaled uniformly with the window, and
// drawn with GDI+; the background and logo are embedded as RCDATA resources.
// Settings are stored in config\launcher.ini next to the launcher, the game is
// read from "extracted game files here" and saves go to userdata.
#define UNICODE
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
// gdiplus.h expects min/max, which NOMINMAX removes.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../resources/launcher/launcher_resources.h"

namespace {

using namespace Gdiplus;

constexpr const wchar_t* kVersion = L"v1.1.2";
constexpr const wchar_t* kGameFolder = L"extracted game files here";
constexpr float kDesignW = 1600.0f;  // design space (window opens at this client size)
constexpr float kDesignH = 900.0f;

// Frame rate modes (values kept compatible with earlier launcher.ini files).
constexpr int kMode30 = 30;
constexpr int kMode60 = 65;

// Posted to the window when a game started from it exits.
constexpr UINT kGameExited = WM_APP + 1;
constexpr const wchar_t* kRunningStatus = L"DOAX2 is running. Logs are saved in out\\game\\logs.";

struct Settings {
  int width = 1280;
  int height = 720;
  int scale = 1;
  int anisotropic = 3;
  bool native_2x_msaa = true;
  bool vsync = true;
  bool fullscreen = false;
  int frame_rate_mode = kMode60;
  int post_process_aa = 0;
  bool fps_overlay = false;
  bool diagnostics = false;
  bool keep_song = false;
  int money_multiplier = 1;  // winnings x1 (original), x10, x15, x20, x100
};

enum class Page { kMain, kSettings, kGraphics };

// Clickable areas, rebuilt on every paint.
struct HitArea {
  RectF rect;
  std::function<void()> action;
  int id;
};

std::filesystem::path g_root;
HWND g_window = nullptr;  // the launcher window
Settings g_settings;
Page g_page = Page::kMain;
std::vector<std::pair<int, int>> g_modes;
std::vector<HitArea> g_hits;
int g_hover = -1;
std::wstring g_status;
bool g_status_error = false;
std::unique_ptr<Bitmap> g_background;
std::unique_ptr<Bitmap> g_logo;

// Colours taken from the background art and logo.
const Color kOcean(255, 30, 120, 214);
const Color kDeepBlue(255, 13, 79, 168);
const Color kAqua(255, 61, 181, 240);
const Color kLime(255, 184, 224, 42);
const Color kGreen(255, 104, 186, 32);
const Color kNavy(255, 8, 36, 84);
const Color kWhite(255, 255, 255, 255);
const Color kPink(255, 242, 123, 176);

// ---------------------------------------------------------------- settings

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
  settings.frame_rate_mode = ReadInt(values, "frame_rate_mode", settings.frame_rate_mode) == kMode30 ? kMode30 : kMode60;
  settings.post_process_aa = std::clamp(ReadInt(values, "post_process_aa", settings.post_process_aa), 0, 2);
  settings.fps_overlay = ReadBool(values, "fps_overlay", settings.fps_overlay);
  settings.diagnostics = ReadBool(values, "diagnostics", settings.diagnostics);
  settings.keep_song = ReadBool(values, "keep_song", settings.keep_song);
  settings.money_multiplier = ReadInt(values, "money_multiplier", settings.money_multiplier);
  if (settings.money_multiplier != 10 && settings.money_multiplier != 15 && settings.money_multiplier != 20 &&
      settings.money_multiplier != 100)
    settings.money_multiplier = 1;
  return settings;
}

void SaveSettings() {
  const Settings& s = g_settings;
  std::error_code error;
  std::filesystem::create_directories(g_root / "config", error);
  std::ofstream output(g_root / "config" / "launcher.ini", std::ios::trunc);
  output << "# DOAX2 Recomp launcher settings\n"
         << "width=" << s.width << '\n'
         << "height=" << s.height << '\n'
         << "resolution_scale=" << s.scale << '\n'
         << "anisotropic_override=" << s.anisotropic << '\n'
         << "native_2x_msaa=" << (s.native_2x_msaa ? "true" : "false") << '\n'
         << "vsync=" << (s.vsync ? "true" : "false") << '\n'
         << "fullscreen=" << (s.fullscreen ? "true" : "false") << '\n'
         << "frame_rate_mode=" << s.frame_rate_mode << '\n'
         << "post_process_aa=" << s.post_process_aa << '\n'
         << "fps_overlay=" << (s.fps_overlay ? "true" : "false") << '\n'
         << "diagnostics=" << (s.diagnostics ? "true" : "false") << '\n'
         << "keep_song=" << (s.keep_song ? "true" : "false") << '\n'
         << "money_multiplier=" << s.money_multiplier << '\n';
}

void CollectDisplayModes() {
  std::set<std::pair<int, int>> unique;
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  for (DWORD index = 0; EnumDisplaySettingsW(nullptr, index, &mode); ++index) {
    if (mode.dmPelsWidth >= 640 && mode.dmPelsWidth <= 4095 && mode.dmPelsHeight >= 480 && mode.dmPelsHeight <= 4095)
      unique.emplace(int(mode.dmPelsWidth), int(mode.dmPelsHeight));
  }
  unique.emplace(g_settings.width, g_settings.height);
  g_modes.assign(unique.begin(), unique.end());
  std::sort(g_modes.begin(), g_modes.end(), [](auto a, auto b) {
    return a.first * a.second < b.first * b.second || (a.first * a.second == b.first * b.second && a.first < b.first);
  });
}

bool GameFilesPresent() { return std::filesystem::exists(g_root / kGameFolder / "default.xex"); }

void SetStatus(std::wstring text, bool error) {
  g_status = std::move(text);
  g_status_error = error;
}

// ------------------------------------------------------------------ launch

std::wstring Quote(const std::filesystem::path& path) { return L"\"" + path.wstring() + L"\""; }

const wchar_t* PostProcessAAName(int setting) {
  switch (setting) {
    case 1: return L"fxaa";
    case 2: return L"fxaa_extreme";
    default: return L"none";
  }
}

void Launch(HWND window) {
  const Settings& s = g_settings;
  const bool native60 = s.frame_rate_mode == kMode60;
  if (native60 && !s.vsync) {
    SetStatus(L"60 FPS needs Guest V-Sync: turn it on in Graphics Settings.", true);
    return;
  }
  const auto game_exe = g_root / "out" / "game" / "doax2.exe";
  if (!std::filesystem::exists(game_exe)) {
    SetStatus(L"out\\game\\doax2.exe is missing: extract the whole release zip again.", true);
    return;
  }
  if (!GameFilesPresent()) {
    SetStatus(L"Game files not found: extract your disc into \"extracted game files here\".", true);
    return;
  }
  SaveSettings();
  const auto true_false = [](bool value) { return value ? L"true" : L"false"; };
  std::wstring command = Quote(game_exe)
      + L" --game_data_root=" + Quote(g_root / kGameFolder)
      + L" --user_data_root=" + Quote(g_root / "userdata")
      // D3D12 render-target path with the Intel stencil/viewport corrections.
      + L" --gpu_plugin=xenos --render_target_path_d3d12=rtv"
      + L" --native_stencil_value_output_d3d12_intel=true"
      + L" --window_width=" + std::to_wstring(s.width)
      + L" --window_height=" + std::to_wstring(s.height)
      + L" --video_mode_width=" + std::to_wstring(s.width)
      + L" --video_mode_height=" + std::to_wstring(s.height)
      + L" --resolution_scale=" + std::to_wstring(s.scale)
      + L" --anisotropic_override=" + std::to_wstring(s.anisotropic)
      + L" --native_2x_msaa=" + true_false(s.native_2x_msaa)
      + L" --vsync=" + true_false(s.vsync)
      // Guest VBlank timing stays with the game; host presentation is not
      // serialized behind DXGI Present(1).
      + L" --d3d12_vsync=false --d3d12_allow_variable_refresh_rate_and_tearing=false"
      + L" --d3d12_vsync_interval=1"
      // 60 FPS runs the guest at 120 Hz vblank (one 60 Hz frame per two
      // vblanks); 30 FPS keeps the original 60 Hz.
      + L" --video_mode_refresh_rate=" + (native60 ? L"120" : L"60")
      + L" --vblank_counter_rate_multiplier=1"
      + L" --doax2_engine_60fps=" + true_false(native60)
      + L" --doax2_music_volume_bridge=true"
      + L" --gpu_allow_invalid_fetch_constants=true"
      + L" --fullscreen=" + true_false(s.fullscreen)
      + L" --doax2_fps_overlay=" + true_false(s.fps_overlay)
      + L" --swap_post_effect=" + PostProcessAAName(s.post_process_aa)
      // Renderer performance: exact screen-space draw extents, batched uploads.
      + L" --execute_unclipped_draw_vs_on_cpu=true --execute_unclipped_draw_vs_on_cpu_with_scissor=true"
      + L" --d3d12_doax2_submit_draw_interval=0 --d3d12_doax2_batch_uploads=true"
      + L" --d3d12_doax2_async_occlusion_query=true"
      // 30 FPS keeps the original free-running vblank; 60 FPS locks guest
      // vblanks to the display (even frame delivery).
      + L" --gpu_display_locked_vblank=" + true_false(native60)
      + L" --doax2_native60_counter_scan=" + true_false(s.diagnostics)
      + L" --xmp_keep_song_across_playlists=" + true_false(s.keep_song)
      + L" --doax2_money_multiplier=" + std::to_wstring(s.money_multiplier);
  if (native60) {
    command += std::wstring(L" --doax2_native60_tick_scale=0.5")
               // Menus at the original speed: transitions and held-direction
               // repeat at the 30 FPS timing, window pop in/out a little slower.
               + L" --doax2_native60_menu_transition_scale=2 --doax2_native60_menu_window_scale=4"
               + L" --doax2_native60_menu_repeat_scale=2"
               + (s.diagnostics ? L" --d3d12_doax2_cp_sampler=true" : L"");
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr, g_root.c_str(),
                      &startup, &process)) {
    SetStatus(L"Unable to start DOAX2 (Windows error " + std::to_wstring(GetLastError()) + L").", true);
    return;
  }
  CloseHandle(process.hThread);
  SetStatus(kRunningStatus, false);
  // Clear the running message when the game closes.
  std::thread([game = process.hProcess] {
    WaitForSingleObject(game, INFINITE);
    CloseHandle(game);
    PostMessageW(g_window, kGameExited, 0, 0);
  }).detach();
}

void OpenLogsFolder() {
  const auto logs = g_root / "out" / "game" / "logs";
  std::error_code error;
  std::filesystem::create_directories(logs, error);
  ShellExecuteW(nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ----------------------------------------------------------------- drawing

std::unique_ptr<Bitmap> LoadImageResource(int id) {
  HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!resource) return nullptr;
  HGLOBAL handle = LoadResource(nullptr, resource);
  const DWORD size = SizeofResource(nullptr, resource);
  const void* data = LockResource(handle);
  if (!data || !size) return nullptr;
  IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), size);
  if (!stream) return nullptr;
  std::unique_ptr<Bitmap> bitmap(Bitmap::FromStream(stream));
  stream->Release();
  if (!bitmap || bitmap->GetLastStatus() != Ok) return nullptr;
  return bitmap;
}

void RoundedRect(GraphicsPath& path, const RectF& r, float radius) {
  const float d = std::min(radius * 2, std::min(r.Width, r.Height));
  path.Reset();
  path.AddArc(r.X, r.Y, d, d, 180, 90);
  path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  path.CloseFigure();
}

Color WithAlpha(const Color& c, BYTE alpha) { return Color(alpha, c.GetR(), c.GetG(), c.GetB()); }

Color Lighter(const Color& c, float amount) {
  auto mix = [amount](BYTE v) { return BYTE(v + (255 - v) * amount); };
  return Color(c.GetA(), mix(c.GetR()), mix(c.GetG()), mix(c.GetB()));
}

void DrawText(Graphics& g, const std::wstring& text, const RectF& rect, float size, bool bold, const Color& color,
              StringAlignment align = StringAlignmentCenter, StringAlignment line = StringAlignmentCenter,
              bool shadow = true) {
  FontFamily family(L"Segoe UI");
  Font font(&family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
  StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(line);
  format.SetTrimming(StringTrimmingEllipsisWord);
  if (shadow) {
    SolidBrush shade(Color(110, 0, 20, 50));
    RectF offset = rect;
    offset.Offset(1.5f, 2.0f);
    g.DrawString(text.c_str(), -1, &font, offset, &format, &shade);
  }
  SolidBrush brush(color);
  g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

int AddHit(const RectF& rect, std::function<void()> action) {
  const int id = int(g_hits.size());
  g_hits.push_back({rect, std::move(action), id});
  return id;
}

enum class ButtonStyle { kNormal, kAccent, kSelected, kUnselected };

// A rounded button in the background's colours: kAccent uses the logo's
// lime/green for the main action; kSelected / kUnselected mark the chosen
// option of a group.
void DrawButton(Graphics& g, const RectF& r, const std::wstring& label, float text_size, std::function<void()> action,
                ButtonStyle style = ButtonStyle::kNormal) {
  const int id = AddHit(r, std::move(action));
  const bool hover = id == g_hover;
  const bool accent = style == ButtonStyle::kAccent, selected = style == ButtonStyle::kSelected;
  Color top = accent ? kLime : selected ? kAqua : kOcean;
  Color bottom = accent ? kGreen : selected ? kOcean : kDeepBlue;
  if (style == ButtonStyle::kUnselected) {
    top = Color(215, 24, 58, 112);
    bottom = Color(215, 8, 36, 84);
  }
  if (hover) { top = Lighter(top, 0.25f); bottom = Lighter(bottom, 0.18f); }
  GraphicsPath path;
  RectF shadow = r;
  shadow.Offset(0, 4);
  RoundedRect(path, shadow, r.Height / 2);
  SolidBrush shadow_brush(Color(70, 0, 20, 60));
  g.FillPath(&shadow_brush, &path);
  RoundedRect(path, r, r.Height / 2);
  LinearGradientBrush fill(PointF(0, r.Y), PointF(0, r.Y + r.Height), top, bottom);
  fill.SetWrapMode(WrapModeTileFlipXY);
  g.FillPath(&fill, &path);
  // Glossy highlight on the upper half.
  RectF gloss(r.X + r.Height * 0.25f, r.Y + 3, r.Width - r.Height * 0.5f, r.Height * 0.42f);
  GraphicsPath gloss_path;
  RoundedRect(gloss_path, gloss, gloss.Height / 2);
  LinearGradientBrush gloss_brush(PointF(0, gloss.Y), PointF(0, gloss.Y + gloss.Height), Color(110, 255, 255, 255),
                                  Color(0, 255, 255, 255));
  gloss_brush.SetWrapMode(WrapModeTileFlipXY);
  g.FillPath(&gloss_brush, &gloss_path);
  Pen border(WithAlpha(kWhite, BYTE(selected || hover ? 255 : style == ButtonStyle::kUnselected ? 110 : 170)),
             selected ? 3.5f : 2.0f);
  g.DrawPath(&border, &path);
  DrawText(g, label, r, text_size, true,
           accent ? kNavy : style == ButtonStyle::kUnselected ? Color(255, 190, 210, 235) : kWhite,
           StringAlignmentCenter, StringAlignmentCenter, !accent);
}

// ON/OFF switch.
void DrawToggle(Graphics& g, const RectF& r, bool* value) {
  const int id = AddHit(r, [value] { *value = !*value; SaveSettings(); });
  const bool hover = id == g_hover;
  GraphicsPath path;
  RoundedRect(path, r, r.Height / 2);
  Color top = *value ? kLime : Color(255, 70, 92, 128);
  Color bottom = *value ? kGreen : Color(255, 40, 58, 92);
  if (hover) { top = Lighter(top, 0.2f); bottom = Lighter(bottom, 0.15f); }
  LinearGradientBrush fill(PointF(0, r.Y), PointF(0, r.Y + r.Height), top, bottom);
  fill.SetWrapMode(WrapModeTileFlipXY);
  g.FillPath(&fill, &path);
  Pen border(WithAlpha(kWhite, 200), 2.5f);
  g.DrawPath(&border, &path);
  const float knob = r.Height - 10;
  const float x = *value ? r.X + r.Width - knob - 5 : r.X + 5;
  SolidBrush knob_brush(kWhite);
  g.FillEllipse(&knob_brush, x, r.Y + 5, knob, knob);
  RectF text = *value ? RectF(r.X + 12, r.Y, r.Width - knob - 22, r.Height)
                      : RectF(r.X + knob + 12, r.Y, r.Width - knob - 22, r.Height);
  DrawText(g, *value ? L"ON" : L"OFF", text, 24, true, *value ? kNavy : kWhite, StringAlignmentCenter,
           StringAlignmentCenter, !*value);
}

// "<  value  >" selector; the arrows (or the value) step through the options.
void DrawSelector(Graphics& g, const RectF& r, const std::wstring& value, std::function<void(int)> step) {
  GraphicsPath path;
  RoundedRect(path, r, r.Height / 2);
  LinearGradientBrush fill(PointF(0, r.Y), PointF(0, r.Y + r.Height), kOcean, kDeepBlue);
  fill.SetWrapMode(WrapModeTileFlipXY);
  g.FillPath(&fill, &path);
  Pen border(WithAlpha(kWhite, 190), 2.5f);
  g.DrawPath(&border, &path);
  const float arrow = r.Height * 1.1f;
  RectF left(r.X, r.Y, arrow, r.Height), right(r.X + r.Width - arrow, r.Y, arrow, r.Height);
  RectF middle(r.X + arrow, r.Y, r.Width - 2 * arrow, r.Height);
  const int left_id = AddHit(left, [step] { step(-1); SaveSettings(); });
  const int middle_id = AddHit(middle, [step] { step(+1); SaveSettings(); });
  const int right_id = AddHit(right, [step] { step(+1); SaveSettings(); });
  for (auto [rect, id] : {std::pair{left, left_id}, std::pair{right, right_id}}) {
    if (id == g_hover) {
      SolidBrush glow(Color(70, 255, 255, 255));
      g.FillEllipse(&glow, rect.X + 7, rect.Y + 7, rect.Width - 14, rect.Height - 14);
    }
  }
  DrawText(g, L"‹", left, 42, true, kWhite);
  DrawText(g, L"›", right, 42, true, kWhite);
  DrawText(g, value, middle, 24, true, middle_id == g_hover ? Lighter(kLime, 0.3f) : kWhite);
}

void DrawPanel(Graphics& g, const RectF& r) {
  GraphicsPath path;
  RoundedRect(path, r, 32);
  SolidBrush fill(Color(205, 8, 36, 84));
  g.FillPath(&fill, &path);
  Pen border(Color(150, 120, 200, 255), 2.5f);
  g.DrawPath(&border, &path);
}

// The project logo, top left on every page.
void DrawLogo(Graphics& g, float width) {
  if (!g_logo) return;
  const float height = width * float(g_logo->GetHeight()) / float(g_logo->GetWidth());
  g.DrawImage(g_logo.get(), RectF(22, 14, width, height));
}

void DrawStatus(Graphics& g, float y) {
  if (g_status.empty()) return;
  RectF r(280, y, 1040, 58);
  GraphicsPath path;
  RoundedRect(path, r, 29);
  SolidBrush fill(g_status_error ? Color(215, 150, 20, 60) : Color(190, 8, 36, 84));
  g.FillPath(&fill, &path);
  DrawText(g, g_status, RectF(r.X + 20, r.Y, r.Width - 40, r.Height), 22, true, kWhite);
}

// One settings row: title and explanation on the left, control on the right.
struct Row {
  std::wstring title;
  std::wstring explanation;
  std::function<void(Graphics&, const RectF&)> control;
};

struct RowStyle {
  float height;
  float title_size;
  float explanation_size;
  // Explanations in a popup behind a "?" button instead of under the title.
  bool info_popup = false;
};

// Explanation popup (graphics settings): title and text of the open popup.
struct InfoPopup {
  std::wstring title;
  std::wstring text;
};
std::unique_ptr<InfoPopup> g_info;

// Round "?" button that opens the explanation popup.
void DrawInfoButton(Graphics& g, const RectF& r, const std::wstring& title, const std::wstring& text) {
  const int id = AddHit(r, [title, text] { g_info = std::make_unique<InfoPopup>(InfoPopup{title, text}); });
  const bool hover = id == g_hover;
  RectF shadow = r;
  shadow.Offset(0, 3);
  SolidBrush shadow_brush(Color(70, 0, 20, 60));
  g.FillEllipse(&shadow_brush, shadow);
  LinearGradientBrush fill(PointF(0, r.Y), PointF(0, r.Y + r.Height), hover ? Lighter(kAqua, 0.25f) : kAqua,
                           hover ? Lighter(kOcean, 0.2f) : kOcean);
  fill.SetWrapMode(WrapModeTileFlipXY);
  g.FillEllipse(&fill, r);
  Pen border(WithAlpha(kWhite, BYTE(hover ? 255 : 210)), 2.5f);
  g.DrawEllipse(&border, r);
  DrawText(g, L"?", r, r.Height * 0.62f, true, kWhite);
}

// The open explanation popup: a large card over the dimmed page. Clicking
// outside the card, CLOSE or Esc closes it; the page underneath stays.
void DrawInfoPopupOverlay(Graphics& g) {
  if (!g_info) return;
  g_hits.clear();
  const RectF everywhere(-4000, -4000, 9600, 8900);
  AddHit(everywhere, [] { g_info.reset(); });
  SolidBrush dim(Color(150, 4, 18, 44));
  g.FillRectangle(&dim, everywhere);
  const RectF card(260, 170, 1080, 560);
  AddHit(card, [] {});
  GraphicsPath path;
  RoundedRect(path, card, 36);
  SolidBrush fill(Color(242, 8, 36, 84));
  g.FillPath(&fill, &path);
  Pen border(Color(220, 140, 210, 255), 3.0f);
  g.DrawPath(&border, &path);
  DrawText(g, g_info->title, RectF(card.X + 50, card.Y + 34, card.Width - 100, 60), 44, true, kWhite,
           StringAlignmentNear, StringAlignmentNear);
  Pen line(Color(110, 160, 210, 255), 2.0f);
  g.DrawLine(&line, card.X + 50, card.Y + 112, card.X + card.Width - 50, card.Y + 112);
  DrawText(g, g_info->text, RectF(card.X + 50, card.Y + 136, card.Width - 100, 300), 29, false,
           Color(255, 225, 238, 255), StringAlignmentNear, StringAlignmentNear, false);
  DrawButton(g, RectF(card.X + (card.Width - 300) / 2, card.Y + card.Height - 104, 300, 74), L"CLOSE", 30,
             [] { g_info.reset(); });
}

void DrawRows(Graphics& g, const std::vector<Row>& rows, float top, const RowStyle& style) {
  for (size_t i = 0; i < rows.size(); ++i) {
    const float y = top + style.height * float(i);
    if (i) {
      Pen line(Color(70, 160, 210, 255), 1.5f);
      g.DrawLine(&line, 186.0f, y, 1414.0f, y);
    }
    if (style.info_popup) {
      DrawText(g, rows[i].title, RectF(192, y, 740, style.height), style.title_size, true, kWhite,
               StringAlignmentNear, StringAlignmentCenter);
      const float button = 58;
      DrawInfoButton(g, RectF(958, y + (style.height - button) / 2, button, button), rows[i].title,
                     rows[i].explanation);
      const float control_height = std::min(62.0f, style.height - 22);
      rows[i].control(g, RectF(1046, y + (style.height - control_height) / 2, 372, control_height));
      continue;
    }
    const float title_height = style.title_size + 10;
    DrawText(g, rows[i].title, RectF(192, y + 8, 830, title_height), style.title_size, true, kWhite,
             StringAlignmentNear, StringAlignmentNear);
    DrawText(g, rows[i].explanation, RectF(192, y + 8 + title_height, 830, style.height - title_height - 12),
             style.explanation_size, false, Color(255, 205, 228, 250), StringAlignmentNear, StringAlignmentNear,
             false);
    const float control_height = std::min(62.0f, style.height - 22);
    rows[i].control(g, RectF(1046, y + (style.height - control_height) / 2, 372, control_height));
  }
}

RectF ToggleRect(const RectF& r) { return RectF(r.X + r.Width - 172, r.Y, 172, r.Height); }

template <typename T>
void StepOption(T& value, const std::vector<T>& options, int direction) {
  auto it = std::find(options.begin(), options.end(), value);
  int index = it == options.end() ? 0 : int(it - options.begin());
  index = (index + direction + int(options.size())) % int(options.size());
  value = options[size_t(index)];
}

// Main page: logo top left, PLAY at the bottom middle with the frame rate
// choice on its left and the settings pages on its right.
void PaintMain(Graphics& g) {
  DrawLogo(g, 560);
  DrawText(g, kVersion, RectF(1380, 14, 204, 38), 21, true, kWhite, StringAlignmentFar);
  DrawStatus(g, 650);
  // Left of PLAY: frame rate.
  DrawText(g, L"FRAME RATE", RectF(60, 728, 470, 34), 24, true, kWhite);
  const auto option = [](bool selected) { return selected ? ButtonStyle::kSelected : ButtonStyle::kUnselected; };
  DrawButton(g, RectF(60, 772, 226, 88), L"30 FPS", 33, [] {
    g_settings.frame_rate_mode = kMode30; SaveSettings();
  }, option(g_settings.frame_rate_mode == kMode30));
  DrawButton(g, RectF(304, 772, 226, 88), L"60 FPS", 33, [] {
    g_settings.frame_rate_mode = kMode60; SaveSettings();
  }, option(g_settings.frame_rate_mode == kMode60));
  // Bottom middle: PLAY.
  DrawButton(g, RectF(566, 728, 468, 140), L"PLAY", 66, [] { Launch(nullptr); }, ButtonStyle::kAccent);
  // Right of PLAY: settings pages.
  DrawButton(g, RectF(1070, 728, 470, 64), L"SETTINGS", 28, [] { g_page = Page::kSettings; });
  DrawButton(g, RectF(1070, 804, 470, 64), L"GRAPHICS SETTINGS", 28, [] { g_page = Page::kGraphics; });
}

void PaintSubPage(Graphics& g, const wchar_t* title, const std::vector<Row>& rows, const RowStyle& style) {
  DrawLogo(g, 250);
  DrawText(g, title, RectF(300, 46, 1000, 56), 42, true, kWhite);
  const float top = 150;
  DrawPanel(g, RectF(150, top, 1300, style.height * float(rows.size()) + 24));
  DrawRows(g, rows, top + 12, style);
  DrawButton(g, RectF(150, 812, 290, 76), L"‹  BACK", 33, [] { g_page = Page::kMain; });
  DrawText(g, L"Changes are saved automatically.", RectF(460, 812, 990, 76), 21, false, kWhite,
           StringAlignmentFar);
}

void PaintSettings(Graphics& g) {
  Settings& s = g_settings;
  std::vector<Row> rows = {
      {L"Continuous Music Playback",
       L"The current song keeps playing to its end instead of restarting whenever you change scenes, start "
       L"a minigame or an event begins. When it ends, the soundtrack carries on with the next song.\n\n"
       L"Pre-rendered movies and returning to the title screen still stop the music. Off is the game's "
       L"original behaviour.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, ToggleRect(r), &s.keep_song); }},
      {L"Winnings Multiplier",
       L"Multiplies the money you win in the casino (blackjack, poker, roulette, slots) and the prize "
       L"money from minigames.\n\n"
       L"Only winnings are multiplied: bets, losses and shop prices stay the same. Normal is the "
       L"game's original amounts.",
       [&s](Graphics& g, const RectF& r) {
         DrawSelector(g, r, s.money_multiplier == 1 ? L"Normal" : std::to_wstring(s.money_multiplier) + L"x",
                      [&s](int direction) {
                        StepOption(s.money_multiplier, std::vector<int>{1, 10, 15, 20, 100}, direction);
                      });
       }},
      {L"Show FPS Overlay",
       L"Shows the current frame rate in the corner of the game window.\n\n"
       L"Useful for checking that the game holds 30 or 60 FPS on your PC.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, ToggleRect(r), &s.fps_overlay); }},
      {L"Diagnostic Logging",
       L"Writes detailed logs for bug reports to the out\\game\\logs folder.\n\n"
       L"It lowers performance, so leave it off for normal play and turn it on only when you are "
       L"reporting a problem or are asked for logs.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, ToggleRect(r), &s.diagnostics); }},
      {L"Log Files",
       L"Opens the folder with the game's log files.\n\n"
       L"When reporting a problem, attach the newest doax2_<number>.log (and doax2_<number>.1.log if there "
       L"is one). Never share game files.",
       [](Graphics& g, const RectF& r) { DrawButton(g, r, L"OPEN LOGS FOLDER", 24, OpenLogsFolder); }},
  };
  PaintSubPage(g, L"SETTINGS", rows, RowStyle{112, 36, 0, true});
}

void PaintGraphics(Graphics& g) {
  Settings& s = g_settings;
  auto resolution = [&s] { return std::to_wstring(s.width) + L" x " + std::to_wstring(s.height); };
  std::vector<Row> rows = {
      {L"Resolution",
       L"Sets the size of the game window, or the screen resolution when Fullscreen is on.\n\n"
       L"The game draws its picture at its own internal resolution and scales it to this size. For a "
       L"sharper picture, raise Resolution Scale as well.",
       [&s, resolution](Graphics& g, const RectF& r) {
         DrawSelector(g, r, resolution(), [&s](int direction) {
           auto current = std::pair{s.width, s.height};
           StepOption(current, g_modes, direction);
           s.width = current.first;
           s.height = current.second;
         });
       }},
      {L"Fullscreen",
       L"Runs the game fullscreen at the chosen resolution instead of in a window.\n\n"
       L"Turn it off to play in a window you can move next to other programs.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, ToggleRect(r), &s.fullscreen); }},
      {L"Resolution Scale",
       L"Renders the game at a multiple of the Xbox 360's 1280 x 720 resolution: 2x is 2560 x 1440, "
       L"3x is 3840 x 2160.\n\n"
       L"Higher values give a much sharper image but need a much faster GPU. 1x is the original look.",
       [&s](Graphics& g, const RectF& r) {
         DrawSelector(g, r, s.scale == 1 ? L"1x (native)" : std::to_wstring(s.scale) + L"x",
                      [&s](int direction) { StepOption(s.scale, std::vector<int>{1, 2, 3, 4}, direction); });
       }},
      {L"MSAA Mode",
       L"On the Xbox 360 the game smooths edges with 2x multisample anti-aliasing (MSAA).\n\n"
       L"Native uses your graphics card's own 2x MSAA: fastest, and correct on most GPUs. Emulated "
       L"uses a slower compatibility path; switch to it only if you see rendering problems.",
       [&s](Graphics& g, const RectF& r) {
         DrawSelector(g, r, s.native_2x_msaa ? L"Native 2x" : L"Emulated 2x",
                      [&s](int) { s.native_2x_msaa = !s.native_2x_msaa; });
       }},
      {L"Post-Process Anti-Aliasing",
       L"Adds FXAA, an extra edge-smoothing filter, to the final image. FXAA Extreme smooths more.\n\n"
       L"Both make the picture slightly softer and cost a little performance. Off keeps the original "
       L"image.",
       [&s](Graphics& g, const RectF& r) {
         static const wchar_t* kNames[] = {L"Off", L"FXAA", L"FXAA Extreme"};
         DrawSelector(g, r, kNames[s.post_process_aa],
                      [&s](int direction) { StepOption(s.post_process_aa, std::vector<int>{0, 1, 2}, direction); });
       }},
      {L"Anisotropic Filtering",
       L"Keeps textures sharp when they are seen at a steep angle, such as the sand, the water and "
       L"floors.\n\n"
       L"Game default keeps the game's own setting; 16x is the sharpest. The performance cost is very "
       L"small on modern graphics cards.",
       [&s](Graphics& g, const RectF& r) {
         static const wchar_t* kNames[] = {L"Game default", L"Off", L"1x", L"2x", L"4x", L"8x", L"16x"};
         DrawSelector(g, r, kNames[s.anisotropic + 1], [&s](int direction) {
           StepOption(s.anisotropic, std::vector<int>{-1, 0, 1, 2, 3, 4, 5}, direction);
         });
       }},
      {L"Guest V-Sync",
       L"Paces the game from the Xbox 360's refresh signal, the way the console does.\n\n"
       L"It must stay on for 60 FPS: without it the game can run too fast. This is not your monitor's "
       L"V-sync.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, ToggleRect(r), &s.vsync); }},
  };
  PaintSubPage(g, L"GRAPHICS SETTINGS", rows, RowStyle{89, 34, 0, true});
}

// The design space (1600 x 900) is scaled uniformly to fit the window and
// centred; the background covers the whole window (cropped, not stretched).
struct Layout {
  float scale = 1;
  float x = 0;
  float y = 0;
};
Layout g_layout;
std::unique_ptr<Bitmap> g_background_cache;  // background drawn at the client size

void UpdateLayout(HWND window) {
  RECT client;
  GetClientRect(window, &client);
  const float width = float(client.right - client.left), height = float(client.bottom - client.top);
  g_layout.scale = std::max(0.05f, std::min(width / kDesignW, height / kDesignH));
  g_layout.x = (width - kDesignW * g_layout.scale) / 2;
  g_layout.y = (height - kDesignH * g_layout.scale) / 2;
  g_background_cache.reset();
}

void Paint(HWND window) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(window, &ps);
  RECT client;
  GetClientRect(window, &client);
  const int width = std::max(1, int(client.right - client.left));
  const int height = std::max(1, int(client.bottom - client.top));
  if (!g_background_cache || int(g_background_cache->GetWidth()) != width ||
      int(g_background_cache->GetHeight()) != height) {
    g_background_cache = std::make_unique<Bitmap>(width, height, PixelFormat32bppPARGB);
    Graphics bg(g_background_cache.get());
    bg.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    bg.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    if (g_background) {
      const float iw = float(g_background->GetWidth()), ih = float(g_background->GetHeight());
      const float cover = std::max(float(width) / iw, float(height) / ih);
      const float dw = iw * cover, dh = ih * cover;
      bg.DrawImage(g_background.get(), RectF((float(width) - dw) / 2, (float(height) - dh) / 2, dw, dh));
    } else {
      SolidBrush fill(kDeepBlue);
      bg.FillRectangle(&fill, 0, 0, width, height);
    }
  }
  Bitmap buffer(width, height, PixelFormat32bppPARGB);
  {
    Graphics g(&buffer);
    g.DrawImage(g_background_cache.get(), 0, 0, width, height);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    if (g_page == Page::kMain) {
      // Darken the bottom of the art behind the page buttons (full width).
      const float top = g_layout.y + 620 * g_layout.scale;
      LinearGradientBrush fade(PointF(0, top - 2), PointF(0, float(height) + 2), Color(0, 8, 36, 84),
                               Color(170, 8, 36, 84));
      fade.SetWrapMode(WrapModeTileFlipXY);
      g.FillRectangle(&fade, 0.0f, top, float(width), float(height) - top);
    }
    g.TranslateTransform(g_layout.x, g_layout.y);
    g.ScaleTransform(g_layout.scale, g_layout.scale);
    g_hits.clear();
    switch (g_page) {
      case Page::kMain: PaintMain(g); break;
      case Page::kSettings: PaintSettings(g); break;
      case Page::kGraphics: PaintGraphics(g); break;
    }
    DrawInfoPopupOverlay(g);
  }
  Graphics screen(hdc);
  screen.DrawImage(&buffer, 0, 0, width, height);
  EndPaint(window, &ps);
}

int HitTest(LPARAM lparam) {
  const PointF point((float(short(LOWORD(lparam))) - g_layout.x) / g_layout.scale,
                     (float(short(HIWORD(lparam))) - g_layout.y) / g_layout.scale);
  for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
    if (it->rect.Contains(point)) return it->id;
  return -1;
}

// Window size for a given client size.
SIZE FrameFor(int client_width, int client_height) {
  RECT rect{0, 0, client_width, client_height};
  AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
  return SIZE{rect.right - rect.left, rect.bottom - rect.top};
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      UpdateLayout(window);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_GETMINMAXINFO: {
      // Smallest window: half of the design size.
      const SIZE minimum = FrameFor(int(kDesignW / 2), int(kDesignH / 2));
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = minimum.cx;
      info->ptMinTrackSize.y = minimum.cy;
      return 0;
    }
    case WM_PAINT:
      Paint(window);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEMOVE: {
      const int hover = HitTest(lparam);
      if (hover != g_hover) {
        g_hover = hover;
        SetCursor(LoadCursorW(nullptr, hover >= 0 ? IDC_HAND : IDC_ARROW));
        InvalidateRect(window, nullptr, FALSE);
      }
      TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
      TrackMouseEvent(&track);
      return 0;
    }
    case WM_MOUSELEAVE:
      if (g_hover != -1) { g_hover = -1; InvalidateRect(window, nullptr, FALSE); }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, g_hover >= 0 ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_LBUTTONUP: {
      const int hit = HitTest(lparam);
      if (hit >= 0 && hit < int(g_hits.size())) {
        const Page before = g_page;
        auto action = g_hits[size_t(hit)].action;
        action();
        if (g_page != before) {
          SetStatus(L"", false);
          g_info.reset();
        }
        g_hover = -1;
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE && g_info) {
        g_info.reset();
        InvalidateRect(window, nullptr, FALSE);
      } else if (wparam == VK_ESCAPE && g_page != Page::kMain) {
        g_page = Page::kMain;
        InvalidateRect(window, nullptr, FALSE);
      } else if (wparam == VK_RETURN && g_page == Page::kMain) {
        Launch(window);
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case kGameExited:
      // Only the running message; an error shown since stays.
      if (g_status == kRunningStatus) {
        SetStatus(L"", false);
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_DESTROY:
      SaveSettings();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void EnableDpiAwareness() {
  using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    if (auto set = reinterpret_cast<SetContext>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
      if (set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
  }
  SetProcessDPIAware();
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  EnableDpiAwareness();
  g_root = ExecutableDirectory();
  GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus_token = 0;
  GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
  g_background = LoadImageResource(IDR_LAUNCHER_BACKGROUND);
  g_logo = LoadImageResource(IDR_LAUNCHER_LOGO);
  g_settings = LoadSettings();
  CollectDisplayModes();
  if (!GameFilesPresent())
    SetStatus(L"Extract your game disc into \"extracted game files here\" to play.", true);

  const wchar_t* const class_name = L"DOAX2RecompPublicLauncher";
  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_LAUNCHER_ICON));
  window_class.hIconSm = window_class.hIcon;
  window_class.lpszClassName = class_name;
  RegisterClassExW(&window_class);

  // Opens at 1600 x 900 (client), scaled down only when the screen's work
  // area is smaller; resizable, the contents scale with the window.
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const SIZE frame = FrameFor(100, 100);
  const int extra_w = frame.cx - 100, extra_h = frame.cy - 100;
  const float fit = std::min((float(work.right - work.left) - float(extra_w)) / kDesignW,
                             (float(work.bottom - work.top) - float(extra_h)) / kDesignH);
  const float initial = std::min(1.0f, fit);
  const int window_w = int(std::lround(kDesignW * initial)) + extra_w;
  const int window_h = int(std::lround(kDesignH * initial)) + extra_h;
  const int x = work.left + std::max(0, int(work.right - work.left - window_w) / 2);
  const int y = work.top + std::max(0, int(work.bottom - work.top - window_h) / 2);
  HWND window = CreateWindowExW(0, class_name, L"DOAX2 Recomp Launcher", WS_OVERLAPPEDWINDOW, x, y, window_w,
                                window_h, nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  g_window = window;
  ShowWindow(window, show);
  UpdateWindow(window);
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  g_background_cache.reset();
  g_background.reset();
  g_logo.reset();
  GdiplusShutdown(gdiplus_token);
  return int(message.wParam);
}
