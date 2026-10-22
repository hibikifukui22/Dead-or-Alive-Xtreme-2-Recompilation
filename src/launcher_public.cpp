// DOAX2 Recomp launcher (public release).
//
// A single custom-drawn window the size of the background art (scaled down
// when the screen is smaller). Three pages: the main page (logo, frame rate,
// Play, Settings / Graphics Settings), Settings and Graphics Settings, each
// with a Back button. Everything is laid out in a 1024 x 1024 design space and
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
#include <vector>

#include "../resources/launcher/launcher_resources.h"

namespace {

using namespace Gdiplus;

constexpr const wchar_t* kVersion = L"v1.0.0";
constexpr const wchar_t* kGameFolder = L"extracted game files here";
constexpr float kDesign = 1024.0f;  // design space and background size

// Frame rate modes (values kept compatible with earlier launcher.ini files).
constexpr int kMode30 = 30;
constexpr int kMode60 = 65;

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
};

enum class Page { kMain, kSettings, kGraphics };

// Clickable areas, rebuilt on every paint.
struct HitArea {
  RectF rect;
  std::function<void()> action;
  int id;
};

std::filesystem::path g_root;
Settings g_settings;
Page g_page = Page::kMain;
std::vector<std::pair<int, int>> g_modes;
std::vector<HitArea> g_hits;
int g_hover = -1;
std::wstring g_status;
bool g_status_error = false;
float g_scale = 1.0f;
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
         << "keep_song=" << (s.keep_song ? "true" : "false") << '\n';
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
    SetStatus(L"60 FPS needs Guest V-sync: turn it on in Graphics Settings.", true);
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
      + L" --xmp_keep_song_across_playlists=" + true_false(s.keep_song);
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
  CloseHandle(process.hProcess);
  SetStatus(L"DOAX2 is running. Logs are saved in out\\game\\logs.", false);
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
  Pen border(WithAlpha(kWhite, 200), 2.0f);
  g.DrawPath(&border, &path);
  const float knob = r.Height - 8;
  const float x = *value ? r.X + r.Width - knob - 4 : r.X + 4;
  SolidBrush knob_brush(kWhite);
  g.FillEllipse(&knob_brush, x, r.Y + 4, knob, knob);
  RectF text = *value ? RectF(r.X + 10, r.Y, r.Width - knob - 18, r.Height)
                      : RectF(r.X + knob + 10, r.Y, r.Width - knob - 18, r.Height);
  DrawText(g, *value ? L"ON" : L"OFF", text, 17, true, *value ? kNavy : kWhite, StringAlignmentCenter,
           StringAlignmentCenter, !*value);
}

// "<  value  >" selector; the arrows (or the value) step through the options.
void DrawSelector(Graphics& g, const RectF& r, const std::wstring& value, std::function<void(int)> step) {
  GraphicsPath path;
  RoundedRect(path, r, r.Height / 2);
  LinearGradientBrush fill(PointF(0, r.Y), PointF(0, r.Y + r.Height), kOcean, kDeepBlue);
  fill.SetWrapMode(WrapModeTileFlipXY);
  g.FillPath(&fill, &path);
  Pen border(WithAlpha(kWhite, 190), 2.0f);
  g.DrawPath(&border, &path);
  const float arrow = r.Height;
  RectF left(r.X, r.Y, arrow, r.Height), right(r.X + r.Width - arrow, r.Y, arrow, r.Height);
  RectF middle(r.X + arrow, r.Y, r.Width - 2 * arrow, r.Height);
  const int left_id = AddHit(left, [step] { step(-1); SaveSettings(); });
  const int middle_id = AddHit(middle, [step] { step(+1); SaveSettings(); });
  const int right_id = AddHit(right, [step] { step(+1); SaveSettings(); });
  for (auto [rect, id] : {std::pair{left, left_id}, std::pair{right, right_id}}) {
    if (id == g_hover) {
      SolidBrush glow(Color(70, 255, 255, 255));
      g.FillEllipse(&glow, rect.X + 6, rect.Y + 6, rect.Width - 12, rect.Height - 12);
    }
  }
  DrawText(g, L"‹", left, 30, true, kWhite);
  DrawText(g, L"›", right, 30, true, kWhite);
  DrawText(g, value, middle, 18, true, middle_id == g_hover ? Lighter(kLime, 0.3f) : kWhite);
}

void DrawPanel(Graphics& g, const RectF& r) {
  GraphicsPath path;
  RoundedRect(path, r, 26);
  SolidBrush fill(Color(205, 8, 36, 84));
  g.FillPath(&fill, &path);
  Pen border(Color(150, 120, 200, 255), 2.0f);
  g.DrawPath(&border, &path);
}

void DrawLogo(Graphics& g, float width, float y) {
  if (!g_logo) return;
  const float height = width * float(g_logo->GetHeight()) / float(g_logo->GetWidth());
  g.DrawImage(g_logo.get(), RectF((kDesign - width) / 2, y, width, height));
}

void DrawStatus(Graphics& g, float y) {
  if (g_status.empty()) return;
  RectF r(152, y, 720, 44);
  GraphicsPath path;
  RoundedRect(path, r, 22);
  SolidBrush fill(g_status_error ? Color(215, 150, 20, 60) : Color(190, 8, 36, 84));
  g.FillPath(&fill, &path);
  DrawText(g, g_status, RectF(r.X + 16, r.Y, r.Width - 32, r.Height), 16, true, kWhite);
}

// One settings row: title and explanation on the left, control on the right.
struct Row {
  std::wstring title;
  std::wstring explanation;
  std::function<void(Graphics&, const RectF&)> control;
};

void DrawRows(Graphics& g, const std::vector<Row>& rows, float top, float row_height) {
  for (size_t i = 0; i < rows.size(); ++i) {
    const float y = top + row_height * float(i);
    if (i) {
      Pen line(Color(70, 160, 210, 255), 1.0f);
      g.DrawLine(&line, 104.0f, y, 920.0f, y);
    }
    DrawText(g, rows[i].title, RectF(108, y + 10, 540, 28), 21, true, kWhite, StringAlignmentNear,
             StringAlignmentNear);
    DrawText(g, rows[i].explanation, RectF(108, y + 38, 540, row_height - 44), 14.5f, false,
             Color(255, 205, 228, 250), StringAlignmentNear, StringAlignmentNear, false);
    rows[i].control(g, RectF(668, y + (row_height - 48) / 2, 252, 48));
  }
}

template <typename T>
void StepOption(T& value, const std::vector<T>& options, int direction) {
  auto it = std::find(options.begin(), options.end(), value);
  int index = it == options.end() ? 0 : int(it - options.begin());
  index = (index + direction + int(options.size())) % int(options.size());
  value = options[size_t(index)];
}

void PaintMain(Graphics& g) {
  DrawLogo(g, 600, 22);
  DrawText(g, L"FRAME RATE", RectF(312, 318, 400, 26), 17, true, kWhite);
  const auto option = [](bool selected) { return selected ? ButtonStyle::kSelected : ButtonStyle::kUnselected; };
  DrawButton(g, RectF(322, 348, 182, 56), L"30 FPS", 24, [] {
    g_settings.frame_rate_mode = kMode30; SaveSettings();
  }, option(g_settings.frame_rate_mode == kMode30));
  DrawButton(g, RectF(520, 348, 182, 56), L"60 FPS", 24, [] {
    g_settings.frame_rate_mode = kMode60; SaveSettings();
  }, option(g_settings.frame_rate_mode == kMode60));
  DrawButton(g, RectF(332, 432, 360, 104), L"PLAY", 46, [] { Launch(nullptr); }, ButtonStyle::kAccent);
  DrawStatus(g, 556);
  // Darken the bottom of the art behind the page buttons.
  LinearGradientBrush fade(PointF(0, 810), PointF(0, kDesign + 2), Color(0, 8, 36, 84), Color(170, 8, 36, 84));
  fade.SetWrapMode(WrapModeTileFlipXY);
  g.FillRectangle(&fade, 0.0f, 820.0f, kDesign, kDesign - 820.0f);
  DrawButton(g, RectF(152, 902, 340, 66), L"SETTINGS", 24, [] { g_page = Page::kSettings; });
  DrawButton(g, RectF(532, 902, 340, 66), L"GRAPHICS SETTINGS", 24, [] { g_page = Page::kGraphics; });
  DrawText(g, kVersion, RectF(884, 984, 124, 30), 15, true, kWhite, StringAlignmentFar);
}

void PaintSubPage(Graphics& g, const wchar_t* title, const std::vector<Row>& rows, float row_height,
                  float extra_bottom = 0) {
  DrawLogo(g, 330, 14);
  DrawText(g, title, RectF(0, 182, kDesign, 44), 30, true, kWhite);
  const float top = 240;
  DrawPanel(g, RectF(72, top, 880, row_height * float(rows.size()) + 24 + extra_bottom));
  DrawRows(g, rows, top + 12, row_height);
  DrawButton(g, RectF(72, 904, 220, 62), L"‹  BACK", 24, [] { g_page = Page::kMain; });
  DrawText(g, L"Changes are saved automatically.", RectF(312, 904, 640, 62), 15, false, kWhite,
           StringAlignmentFar);
}

void PaintSettings(Graphics& g) {
  Settings& s = g_settings;
  std::vector<Row> rows = {
      {L"Keep song playing across scenes",
       L"The current song plays to its end instead of restarting on every scene change, minigame or event. "
       L"Pre-rendered movies and the title screen still stop the music.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, RectF(r.X + r.Width - 132, r.Y, 132, r.Height), &s.keep_song); }},
      {L"Show FPS overlay",
       L"Shows the current frame rate in the corner of the game window.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, RectF(r.X + r.Width - 132, r.Y, 132, r.Height), &s.fps_overlay); }},
      {L"Diagnostic logging",
       L"Writes detailed logs for bug reports to out\\game\\logs. Lowers performance, so leave it off for "
       L"normal play.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, RectF(r.X + r.Width - 132, r.Y, 132, r.Height), &s.diagnostics); }},
      {L"Log files",
       L"Opens the folder with the game's log files. Attach the newest doax2_<number>.log when reporting a "
       L"problem.",
       [](Graphics& g, const RectF& r) { DrawButton(g, r, L"OPEN LOGS FOLDER", 17, OpenLogsFolder); }},
  };
  PaintSubPage(g, L"SETTINGS", rows, 128);
}

void PaintGraphics(Graphics& g) {
  Settings& s = g_settings;
  auto resolution = [&s] { return std::to_wstring(s.width) + L" x " + std::to_wstring(s.height); };
  std::vector<Row> rows = {
      {L"Resolution",
       L"Size of the game window, or the screen resolution when fullscreen.",
       [&s, resolution](Graphics& g, const RectF& r) {
         DrawSelector(g, r, resolution(), [&s](int direction) {
           auto current = std::pair{s.width, s.height};
           StepOption(current, g_modes, direction);
           s.width = current.first;
           s.height = current.second;
         });
       }},
      {L"Fullscreen",
       L"Runs the game fullscreen instead of in a window.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, RectF(r.X + r.Width - 132, r.Y, 132, r.Height), &s.fullscreen); }},
      {L"Resolution scale",
       L"Renders the game at a multiple of the console's resolution: sharper image, but each step needs "
       L"a much faster GPU.",
       [&s](Graphics& g, const RectF& r) {
         DrawSelector(g, r, s.scale == 1 ? L"1x (native)" : std::to_wstring(s.scale) + L"x",
                      [&s](int direction) { StepOption(s.scale, std::vector<int>{1, 2, 3, 4}, direction); });
       }},
      {L"MSAA mode",
       L"How the game's own 2x anti-aliasing is reproduced. Native is faster and correct on most GPUs; "
       L"use Emulated if you see rendering problems.",
       [&s](Graphics& g, const RectF& r) {
         DrawSelector(g, r, s.native_2x_msaa ? L"Native 2x" : L"Emulated 2x",
                      [&s](int) { s.native_2x_msaa = !s.native_2x_msaa; });
       }},
      {L"Post-process anti-aliasing",
       L"Extra edge smoothing on the final image (FXAA). Slightly softer picture, small performance cost.",
       [&s](Graphics& g, const RectF& r) {
         static const wchar_t* kNames[] = {L"Off", L"FXAA", L"FXAA Extreme"};
         DrawSelector(g, r, kNames[s.post_process_aa],
                      [&s](int direction) { StepOption(s.post_process_aa, std::vector<int>{0, 1, 2}, direction); });
       }},
      {L"Anisotropic filtering",
       L"Keeps textures sharp at steep angles, such as sand, water and floors. Very small performance cost.",
       [&s](Graphics& g, const RectF& r) {
         static const wchar_t* kNames[] = {L"Game default", L"Off", L"1x", L"2x", L"4x", L"8x", L"16x"};
         DrawSelector(g, r, kNames[s.anisotropic + 1], [&s](int direction) {
           StepOption(s.anisotropic, std::vector<int>{-1, 0, 1, 2, 3, 4, 5}, direction);
         });
       }},
      {L"Guest V-sync",
       L"Paces the game from the console's refresh signal, as on the Xbox 360. Required for 60 FPS; "
       L"without it the game can run too fast.",
       [&s](Graphics& g, const RectF& r) { DrawToggle(g, RectF(r.X + r.Width - 132, r.Y, 132, r.Height), &s.vsync); }},
  };
  PaintSubPage(g, L"GRAPHICS SETTINGS", rows, 88);
}

void Paint(HWND window) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(window, &ps);
  RECT client;
  GetClientRect(window, &client);
  const int width = client.right - client.left, height = client.bottom - client.top;
  Bitmap buffer(width, height, PixelFormat32bppPARGB);
  {
    Graphics g(&buffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.ScaleTransform(g_scale, g_scale);
    if (g_background) {
      g.DrawImage(g_background.get(), RectF(0, 0, kDesign, kDesign));
    } else {
      SolidBrush fill(kDeepBlue);
      g.FillRectangle(&fill, 0.0f, 0.0f, kDesign, kDesign);
    }
    g_hits.clear();
    switch (g_page) {
      case Page::kMain: PaintMain(g); break;
      case Page::kSettings: PaintSettings(g); break;
      case Page::kGraphics: PaintGraphics(g); break;
    }
  }
  Graphics screen(hdc);
  screen.DrawImage(&buffer, 0, 0, width, height);
  EndPaint(window, &ps);
}

int HitTest(LPARAM lparam) {
  const PointF point(float(short(LOWORD(lparam))) / g_scale, float(short(HIWORD(lparam))) / g_scale);
  for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
    if (it->rect.Contains(point)) return it->id;
  return -1;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
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
        if (g_page != before) SetStatus(L"", false);
        g_hover = -1;
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE && g_page != Page::kMain) {
        g_page = Page::kMain;
        InvalidateRect(window, nullptr, FALSE);
      } else if (wparam == VK_RETURN && g_page == Page::kMain) {
        Launch(window);
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

  // The window is the size of the background art (1024 x 1024 pixels),
  // scaled down only when the screen's work area is smaller.
  const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT frame{0, 0, 100, 100};
  AdjustWindowRectEx(&frame, style, FALSE, 0);
  const int frame_w = (frame.right - frame.left) - 100, frame_h = (frame.bottom - frame.top) - 100;
  const float fit = std::min((float(work.right - work.left) - float(frame_w)) / kDesign,
                             (float(work.bottom - work.top) - float(frame_h)) / kDesign);
  g_scale = std::min(1.0f, fit);
  const int client = int(std::lround(kDesign * g_scale));
  const int window_w = client + frame_w, window_h = client + frame_h;
  const int x = work.left + std::max(0, int(work.right - work.left - window_w) / 2);
  const int y = work.top + std::max(0, int(work.bottom - work.top - window_h) / 2);
  HWND window = CreateWindowExW(0, class_name, L"DOAX2 Recomp Launcher", style, x, y, window_w, window_h, nullptr,
                                nullptr, instance, nullptr);
  if (!window) return 1;
  ShowWindow(window, show);
  UpdateWindow(window);
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  g_background.reset();
  g_logo.reset();
  GdiplusShutdown(gdiplus_token);
  return int(message.wParam);
}
