// DOAX2 Recomp launcher (public release).
//
// A single custom-drawn, resizable window (opens at 1600 x 900). Five pages:
// the main page (logo, frame rate, Play, Settings / Graphics Settings /
// Update / Input), Settings, Graphics Settings, Update (downloads and installs
// the latest release) and Input (keyboard and controller), each with a Back
// button. Everything is laid
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
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
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
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "../resources/launcher/launcher_resources.h"

namespace {

using namespace Gdiplus;

constexpr const wchar_t* kVersion = L"v1.1.4";
constexpr const wchar_t* kGameFolder = L"extracted game files here";
constexpr float kDesignW = 1600.0f;  // design space (window opens at this client size)
constexpr float kDesignH = 900.0f;

// Frame rate modes (values kept compatible with earlier launcher.ini files).
constexpr int kMode30 = 30;
constexpr int kMode60 = 65;

// Posted to the window when a game started from it exits.
constexpr UINT kGameExited = WM_APP + 1;
constexpr const wchar_t* kRunningStatus = L"DOAX2 is running. Logs are saved in out\\game\\logs.";

// Keyboard controls: each controller input can have two keys. The names are
// the runtime's key names (passed as --keybind_<cvar>=Key1,Key2).
struct KeyAction {
  const char* id;     // launcher.ini key_<id> and the runtime's keybind_<id>
  const wchar_t* label;
  const char* primary;
  const char* secondary;
};

constexpr KeyAction kKeyActions[] = {
    {"a", L"A", "Space", ""},
    {"b", L"B", "Backspace", ""},
    {"x", L"X", "F", ""},
    {"y", L"Y", "R", ""},
    {"left_shoulder", L"LB", "Q", ""},
    {"right_shoulder", L"RB", "E", ""},
    {"left_trigger", L"LT", "Z", ""},
    {"right_trigger", L"RT", "C", ""},
    {"back", L"Back", "Tab", ""},
    {"start", L"Start", "Return", ""},
    {"lstick_press", L"LS click", "V", ""},
    {"rstick_press", L"RS click", "H", ""},
    {"lstick_up", L"LS up", "W", ""},
    {"lstick_down", L"LS down", "S", ""},
    {"lstick_left", L"LS left", "A", ""},
    {"lstick_right", L"LS right", "D", ""},
    {"rstick_up", L"RS up", "I", ""},
    {"rstick_down", L"RS down", "K", ""},
    {"rstick_left", L"RS left", "J", ""},
    {"rstick_right", L"RS right", "L", ""},
    {"dpad_up", L"D-pad up", "Up", ""},
    {"dpad_down", L"D-pad down", "Down", ""},
    {"dpad_left", L"D-pad left", "Left", ""},
    {"dpad_right", L"D-pad right", "Right", ""},
};
constexpr size_t kKeyActionCount = std::size(kKeyActions);

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
  bool keyboard = true;
  std::array<std::array<std::string, 2>, kKeyActionCount> keys = [] {
    std::array<std::array<std::string, 2>, kKeyActionCount> defaults;
    for (size_t i = 0; i < kKeyActionCount; ++i) defaults[i] = {kKeyActions[i].primary, kKeyActions[i].secondary};
    return defaults;
  }();
  // Player 1 controller: the controller inputs (KeyAction ids) that drive each
  // game input; by default every input is its own button.
  std::array<std::array<std::string, 2>, kKeyActionCount> pad = [] {
    std::array<std::array<std::string, 2>, kKeyActionCount> defaults;
    for (size_t i = 0; i < kKeyActionCount; ++i) defaults[i] = {kKeyActions[i].id, ""};
    return defaults;
  }();
};

enum class Page { kMain, kSettings, kGraphics, kInput, kUpdate };

// Clickable areas, rebuilt on every paint; 'right' runs on a right click.
struct HitArea {
  RectF rect;
  std::function<void()> action;
  int id;
  std::function<void()> right;
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
std::unique_ptr<Bitmap> g_controller;  // controller diagram on the Input page
// Key box waiting for a key press (Input page), or -1.
int g_capture_action = -1;
int g_capture_slot = 0;
bool g_capture_pad = false;  // waiting for a controller button instead of a key
uint32_t g_pad_held = 0;     // controller inputs held when the wait began
bool g_input_controller_tab = false;
std::wstring g_input_note;  // result of the last key change
constexpr UINT_PTR kPadTimer = 1;     // polls the controller while waiting for a button
constexpr UINT_PTR kStatusTimer = 2;  // refreshes "Connected" on the controller tab

// Keys that can be bound: Windows virtual key, runtime name, label. F3, F4,
// F7 and ` open the runtime's overlays, Esc cancels a key change, and
// Shift / Ctrl / Alt only work as modifiers, so none of them are listed.
struct KeyName {
  UINT vk;
  std::string name;
  std::wstring label;
};

const std::vector<KeyName>& KeyNames() {
  static const std::vector<KeyName> names = [] {
    std::vector<KeyName> list;
    for (char c = 'A'; c <= 'Z'; ++c) list.push_back({UINT(c), std::string(1, c), std::wstring(1, wchar_t(c))});
    for (char c = '0'; c <= '9'; ++c) list.push_back({UINT(c), std::string(1, c), std::wstring(1, wchar_t(c))});
    for (int f = 1; f <= 12; ++f)
      if (f != 3 && f != 4 && f != 7)
        list.push_back({UINT(VK_F1 + f - 1), "F" + std::to_string(f), L"F" + std::to_wstring(f)});
    for (int n = 0; n <= 9; ++n)
      list.push_back({UINT(VK_NUMPAD0 + n), "Numpad" + std::to_string(n), L"Num " + std::to_wstring(n)});
    const KeyName fixed[] = {
        {VK_SPACE, "Space", L"Space"},         {VK_RETURN, "Return", L"Enter"},
        {VK_TAB, "Tab", L"Tab"},               {VK_BACK, "Backspace", L"Backspace"},
        {VK_DELETE, "Delete", L"Delete"},      {VK_INSERT, "Insert", L"Insert"},
        {VK_HOME, "Home", L"Home"},            {VK_END, "End", L"End"},
        {VK_PRIOR, "PageUp", L"Page Up"},      {VK_NEXT, "PageDown", L"Page Down"},
        {VK_UP, "Up", L"Up"},                  {VK_DOWN, "Down", L"Down"},
        {VK_LEFT, "Left", L"Left"},            {VK_RIGHT, "Right", L"Right"},
        {VK_OEM_1, "Semicolon", L";"},         {VK_OEM_7, "Quote", L"'"},
        {VK_OEM_COMMA, "Comma", L","},         {VK_OEM_PERIOD, "Period", L"."},
        {VK_OEM_2, "Slash", L"/"},             {VK_OEM_5, "Backslash", L"\\"},
        {VK_OEM_4, "LBracket", L"["},          {VK_OEM_6, "RBracket", L"]"},
        {VK_OEM_MINUS, "Minus", L"-"},         {VK_OEM_PLUS, "Plus", L"="},
        {VK_ADD, "NumpadPlus", L"Num +"},      {VK_SUBTRACT, "NumpadMinus", L"Num -"},
        {VK_MULTIPLY, "NumpadStar", L"Num *"}, {VK_DIVIDE, "NumpadSlash", L"Num /"},
    };
    list.insert(list.end(), std::begin(fixed), std::end(fixed));
    return list;
  }();
  return names;
}

const KeyName* FindKey(UINT vk) {
  for (const auto& key : KeyNames())
    if (key.vk == vk) return &key;
  return nullptr;
}

std::wstring KeyLabel(const std::string& name) {
  if (name.empty()) return L"—";
  for (const auto& key : KeyNames())
    if (key.name == name) return key.label;
  return std::wstring(name.begin(), name.end());  // hand-edited launcher.ini value
}

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

std::wstring ExecutablePath() {
  std::vector<wchar_t> buffer(32768);
  DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
  return std::wstring(buffer.data(), length);
}

std::filesystem::path ExecutableDirectory() { return std::filesystem::path(ExecutablePath()).parent_path(); }

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
  settings.keyboard = ReadBool(values, "keyboard", settings.keyboard);
  for (size_t i = 0; i < kKeyActionCount; ++i) {
    auto found = values.find(std::string("key_") + kKeyActions[i].id);
    if (found == values.end()) continue;
    const std::string& keys = found->second;  // "Primary,Secondary"; either may be empty
    const size_t comma = keys.find(',');
    settings.keys[i][0] = keys.substr(0, comma);
    settings.keys[i][1] = comma == std::string::npos ? "" : keys.substr(comma + 1);
  }
  for (size_t i = 0; i < kKeyActionCount; ++i) {
    auto found = values.find(std::string("pad_") + kKeyActions[i].id);
    if (found == values.end()) continue;
    const std::string& inputs = found->second;
    const size_t comma = inputs.find(',');
    settings.pad[i][0] = inputs.substr(0, comma);
    settings.pad[i][1] = comma == std::string::npos ? "" : inputs.substr(comma + 1);
  }
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
         << "money_multiplier=" << s.money_multiplier << '\n'
         << "keyboard=" << (s.keyboard ? "true" : "false") << '\n';
  for (size_t i = 0; i < kKeyActionCount; ++i)
    output << "key_" << kKeyActions[i].id << '=' << s.keys[i][0] << ',' << s.keys[i][1] << '\n';
  for (size_t i = 0; i < kKeyActionCount; ++i)
    output << "pad_" << kKeyActions[i].id << '=' << s.pad[i][0] << ',' << s.pad[i][1] << '\n';
}

// Controller family of the connected controller, for button names.
enum class PadFamily { kNone, kXbox, kPlayStation, kNintendo, kOther };
PadFamily g_pad_family = PadFamily::kNone;

// Label of a controller input id in the connected controller's own names
// ("a" is Cross on a PlayStation controller, B on a Nintendo one: SDL and the
// game read buttons by position, so "a" is always the bottom face button).
std::wstring PadLabel(const std::string& id) {
  if (id.empty()) return L"—";
  struct Names { const char* id; const wchar_t* playstation; const wchar_t* nintendo; };
  static const Names kNames[] = {
      {"a", L"Cross", L"B"},           {"b", L"Circle", L"A"},
      {"x", L"Square", L"Y"},          {"y", L"Triangle", L"X"},
      {"left_shoulder", L"L1", L"L"},  {"right_shoulder", L"R1", L"R"},
      {"left_trigger", L"L2", L"ZL"},  {"right_trigger", L"R2", L"ZR"},
      {"back", L"Share", L"Minus"},    {"start", L"Options", L"Plus"},
      {"lstick_press", L"L3", nullptr}, {"rstick_press", L"R3", nullptr},
  };
  if (g_pad_family == PadFamily::kPlayStation || g_pad_family == PadFamily::kNintendo)
    for (const auto& names : kNames)
      if (id == names.id) {
        const wchar_t* name = g_pad_family == PadFamily::kPlayStation ? names.playstation : names.nintendo;
        if (name) return name;
      }
  for (const auto& action : kKeyActions)
    if (id == action.id) return action.label;
  return std::wstring(id.begin(), id.end());
}

// Puts controller input 'id' in one box. If another box already had it, that
// box gets this box's old input, so the two swap.
void AssignPad(size_t action, int slot, const std::string& id) {
  std::string& box = g_settings.pad[action][size_t(slot)];
  const std::string previous = box;
  g_input_note = PadLabel(id) + L" now does " + kKeyActions[action].label + L".";
  for (size_t i = 0; i < kKeyActionCount; ++i)
    for (auto& other : g_settings.pad[i])
      if (other == id && &other != &box) {
        other = previous;
        if (i != action)
          g_input_note += previous.empty()
                              ? L" It was removed from " + std::wstring(kKeyActions[i].label) + L"."
                              : L" " + PadLabel(previous) + L" now does " + kKeyActions[i].label + L".";
      }
  box = id;
  SaveSettings();
}

// Controller inputs held on any connected controller (Xbox, PlayStation,
// Switch and others, through SDL like the game), as a bit mask in
// kKeyActions order. 'connected' is set when a controller is connected. SDL
// starts on the first call, so the launcher only touches controllers on the
// controller tab.
uint32_t HeldPadInputs(bool* connected = nullptr) {
  static bool started = false;
  if (!started) {
    started = true;
    // Device detection on SDL's own thread: the launcher pumps no SDL window.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_Init(SDL_INIT_GAMEPAD);
  }
  static std::map<SDL_JoystickID, SDL_Gamepad*> open;
  SDL_PumpEvents();
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  for (auto it = open.begin(); it != open.end();) {
    if (SDL_GamepadConnected(it->second)) { ++it; continue; }
    SDL_CloseGamepad(it->second);
    it = open.erase(it);
  }
  int count = 0;
  if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
    for (int i = 0; i < count; ++i)
      if (!open.count(ids[i]))
        if (SDL_Gamepad* gamepad = SDL_OpenGamepad(ids[i])) open[ids[i]] = gamepad;
    SDL_free(ids);
  }

  static const SDL_GamepadButton kButtons[] = {
      SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
      SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
      SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
      SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_INVALID,
      SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID,
      SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID,
      SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
      SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
  static_assert(std::size(kButtons) == kKeyActionCount);
  uint32_t held = 0;
  g_pad_family = PadFamily::kNone;
  for (const auto& [id, gamepad] : open) {
    if (g_pad_family == PadFamily::kNone) {
      switch (SDL_GetGamepadType(gamepad)) {
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE: g_pad_family = PadFamily::kXbox; break;
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5: g_pad_family = PadFamily::kPlayStation; break;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: g_pad_family = PadFamily::kNintendo; break;
        default: g_pad_family = PadFamily::kOther; break;
      }
    }
    for (size_t i = 0; i < kKeyActionCount; ++i)
      if (kButtons[i] != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(gamepad, kButtons[i])) held |= 1u << i;
    if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 12000) held |= 1u << 6;
    if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 12000) held |= 1u << 7;
    // Stick directions (lstick_up .. rstick_right) once pushed halfway; SDL's
    // Y axes point down.
    const int lx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    const int ly = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    const int rx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    const int ry = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    const int sticks[] = {-ly, ly, -lx, lx, -ry, ry, -rx, rx};
    for (int d = 0; d < 8; ++d)
      if (sticks[d] > 16384) held |= 1u << (12 + d);
  }
  if (connected) *connected = !open.empty();
  return held;
}

void StartCapture(size_t action, int slot, bool pad) {
  g_capture_action = int(action);
  g_capture_slot = slot;
  g_capture_pad = pad;
  g_input_note.clear();
  if (pad) {
    bool connected = false;
    g_pad_held = HeldPadInputs(&connected);
    if (!connected) g_input_note = L"No controller found. Connect your controller, then press a button.";
    SetTimer(g_window, kPadTimer, 16, nullptr);
  }
}

void EndCapture() {
  if (g_capture_action >= 0 && g_capture_pad) KillTimer(g_window, kPadTimer);
  g_capture_action = -1;
}

// pad_remap value: every game input whose controller inputs differ from the
// default, as "input=source,source;..."; empty when nothing is changed.
std::wstring PadRemapValue(const Settings& s) {
  std::string value;
  for (size_t i = 0; i < kKeyActionCount; ++i) {
    if (s.pad[i][0] == kKeyActions[i].id && s.pad[i][1].empty()) continue;
    value += std::string(kKeyActions[i].id) + "=";
    bool first = true;
    for (const auto& source : s.pad[i])
      if (!source.empty()) { value += (first ? "" : ",") + source; first = false; }
    value += ';';
  }
  return std::wstring(value.begin(), value.end());
}

// Binds 'name' to one key box, taking it off any other box first so a key
// never drives two inputs.
void AssignKey(size_t action, int slot, const std::string& name) {
  std::wstring moved_from;
  for (size_t i = 0; i < kKeyActionCount; ++i)
    for (auto& key : g_settings.keys[i])
      if (key == name && !(i == action && &key == &g_settings.keys[i][size_t(slot)])) {
        key.clear();
        if (i != action) moved_from = kKeyActions[i].label;
      }
  g_settings.keys[action][size_t(slot)] = name;
  g_input_note = KeyLabel(name) + L" is now " + kKeyActions[action].label + L".";
  if (!moved_from.empty()) g_input_note += L" It was removed from " + moved_from + L".";
  SaveSettings();
}

// Runtime keybind value: the keys joined by commas; "None" binds nothing.
std::wstring KeybindValue(const std::array<std::string, 2>& keys) {
  std::string value;
  for (const auto& key : keys)
    if (!key.empty()) value += (value.empty() ? "" : ",") + key;
  if (value.empty()) value = "None";
  return std::wstring(value.begin(), value.end());
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
      + L" --doax2_money_multiplier=" + std::to_wstring(s.money_multiplier)
      // Keyboard as a controller (merged with any connected pad); no mouse look.
      + L" --mnk_mode=" + true_false(s.keyboard) + L" --mnk_mouse=false";
  for (size_t i = 0; i < kKeyActionCount; ++i)
    command += std::wstring(L" --keybind_") + std::wstring(kKeyActions[i].id, kKeyActions[i].id + strlen(kKeyActions[i].id))
               + L"=" + KeybindValue(s.keys[i]);
  // Player 1 controller layout (only when changed from the default).
  if (const std::wstring remap = PadRemapValue(s); !remap.empty()) command += L" --pad_remap=" + remap;
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

// ------------------------------------------------------------------ update
//
// The UPDATE page is the only part of DOAX2 Recomp that goes online, and only
// while it is open: it asks GitHub for the latest release. INSTALL UPDATE
// downloads the release's player zip, checks it against SHA256SUMS.txt,
// extracts it with Windows' own tar.exe and copies the program files over
// this folder. userdata, config and the game files are never touched. The
// download, the unpacked files and backups of the replaced files (restored if
// anything fails) go to %TEMP%\DOAX2-Recomp-update, which keeps paths short
// for installs in deep folders. The
// running launcher cannot be overwritten, so it is renamed to *.update-old
// and deleted on the next start.

constexpr const wchar_t* kUpdateUrl =
    L"https://api.github.com/repos/hibikifukui22/Dead-or-Alive-Xtreme-2-Recompilation/releases/latest";
constexpr const wchar_t* kOldSuffix = L".update-old";
constexpr UINT kUpdateChanged = WM_APP + 2;  // posted by the update thread: repaint
constexpr UINT_PTR kUpdateTimer = 3;         // animates steps of unknown length

enum UpdateStep { kStepCheck, kStepDownload, kStepVerify, kStepExtract, kStepInstall, kStepCount };
enum class StepState { kWaiting, kRunning, kDone, kFailed };

struct UpdateProgress {
  StepState state[kStepCount] = {};
  float fraction[kStepCount] = {};  // below 0 while the length is unknown
  std::wstring detail[kStepCount];
  std::wstring message;
  bool error = false;
  bool busy = false;
  bool checked = false;    // a check has run on this visit of the page
  bool available = false;  // a newer release was found
  bool installed = false;
  std::wstring latest;     // "v1.1.4"
  std::wstring zip_url, sums_url, zip_name;
};

std::mutex g_update_mutex;
UpdateProgress g_update;

// Changes the update state from any thread and repaints the window.
void UpdateState(const std::function<void(UpdateProgress&)>& change) {
  {
    std::lock_guard lock(g_update_mutex);
    change(g_update);
  }
  PostMessageW(g_window, kUpdateChanged, 0, 0);
}

UpdateProgress UpdateSnapshot() {
  std::lock_guard lock(g_update_mutex);
  return g_update;
}

void StepRunning(int step, std::wstring detail, float fraction = -1) {
  UpdateState([&](UpdateProgress& u) {
    u.state[step] = StepState::kRunning;
    u.fraction[step] = fraction;
    u.detail[step] = std::move(detail);
  });
}

void StepDone(int step, std::wstring detail) {
  UpdateState([&](UpdateProgress& u) {
    u.state[step] = StepState::kDone;
    u.fraction[step] = 1;
    u.detail[step] = std::move(detail);
  });
}

void StepFailed(int step, std::wstring detail, std::wstring message) {
  UpdateState([&](UpdateProgress& u) {
    u.state[step] = StepState::kFailed;
    u.detail[step] = std::move(detail);
    u.message = std::move(message);
    u.error = true;
    u.busy = false;
  });
}

std::wstring Widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

// Only GitHub over HTTPS. DOAX2_UPDATE_URL may point the check at a local
// test server (http://127.0.0.1 or http://localhost) instead.
bool AllowedUpdateUrl(const std::wstring& url) {
  for (const wchar_t* prefix : {L"https://api.github.com/", L"https://github.com/"})
    if (url.rfind(prefix, 0) == 0) return true;
  if (_wgetenv(L"DOAX2_UPDATE_URL"))
    for (const wchar_t* prefix : {L"http://127.0.0.1:", L"http://localhost:"})
      if (url.rfind(prefix, 0) == 0) return true;
  return false;
}

struct HttpHandle {
  HINTERNET handle = nullptr;
  ~HttpHandle() {
    if (handle) WinHttpCloseHandle(handle);
  }
};

// GET 'url' into 'body' or into 'file'; 'progress' gets (received, total),
// total 0 when unknown. Redirects (GitHub download links) are followed.
bool HttpGet(const std::wstring& url, std::string* body, const std::filesystem::path* file,
             const std::function<void(uint64_t, uint64_t)>& progress, std::wstring& error) {
  if (!AllowedUpdateUrl(url)) {
    error = L"Unexpected download address.";
    return false;
  }
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = DWORD(-1);
  parts.dwUrlPathLength = DWORD(-1);
  parts.dwExtraInfoLength = DWORD(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
    error = L"Unexpected download address.";
    return false;
  }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  const std::wstring agent = std::wstring(L"DOAX2-Recomp-Launcher/") + (kVersion + 1);
  HttpHandle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.handle) {
    error = L"Windows could not start the download.";
    return false;
  }
  WinHttpSetTimeouts(session.handle, 15000, 15000, 30000, 60000);
  HttpHandle connection{WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0)};
  HttpHandle request{connection.handle
                         ? WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                         : nullptr};
  const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
  if (!request.handle || !WinHttpSendRequest(request.handle, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.handle, nullptr)) {
    error = L"Could not reach GitHub. Check your internet connection.";
    return false;
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) {
    error = status == 404   ? L"No release was found on GitHub (the project page may be private or offline)."
            : status == 403 ? L"GitHub is limiting requests right now. Try again in an hour."
                            : L"GitHub answered with error " + std::to_wstring(status) + L".";
    return false;
  }
  uint64_t total = 0;
  wchar_t length_text[32] = {};
  size = sizeof(length_text);
  if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length_text,
                          &size, WINHTTP_NO_HEADER_INDEX))
    total = _wcstoui64(length_text, nullptr, 10);
  std::ofstream output;
  if (file) {
    output.open(*file, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = L"Could not save the download in the Windows temp folder.";
      return false;
    }
  }
  std::vector<char> buffer(1 << 16);
  uint64_t received = 0;
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.handle, buffer.data(), DWORD(buffer.size()), &read)) {
      error = L"The download was interrupted. Check your internet connection.";
      return false;
    }
    if (read == 0) break;
    if (file)
      output.write(buffer.data(), read);
    else
      body->append(buffer.data(), read);
    received += read;
    if (progress) progress(received, total);
  }
  if (file && !output.good()) {
    error = L"Could not save the download in the Windows temp folder.";
    return false;
  }
  return true;
}

// The string value of "key" at or after 'from' in a JSON text.
std::string JsonString(const std::string& json, const std::string& key, size_t from, size_t* end = nullptr) {
  size_t at = json.find("\"" + key + "\"", from);
  if (at == std::string::npos) return {};
  at = json.find(':', at);
  if (at == std::string::npos) return {};
  at = json.find_first_not_of(" \t\r\n", at + 1);
  if (at == std::string::npos || json[at] != '"') return {};
  std::string value;
  for (++at; at < json.size() && json[at] != '"'; ++at) {
    if (json[at] == '\\' && at + 1 < json.size()) ++at;
    value += json[at];
  }
  if (end) *end = at;
  return value;
}

// "v1.1.4" -> {1, 1, 4}
std::vector<int> VersionNumbers(const std::wstring& tag) {
  std::vector<int> numbers;
  int value = -1;
  for (wchar_t c : tag) {
    if (c >= L'0' && c <= L'9') {
      value = (value < 0 ? 0 : value * 10) + (c - L'0');
    } else if (value >= 0) {
      numbers.push_back(value);
      value = -1;
    }
  }
  if (value >= 0) numbers.push_back(value);
  return numbers;
}

bool NewerVersion(const std::wstring& latest, const std::wstring& installed) {
  auto a = VersionNumbers(latest), b = VersionNumbers(installed);
  a.resize(std::max(a.size(), b.size()));
  b.resize(a.size());
  return a > b;
}

std::string Sha256File(const std::filesystem::path& path, const std::function<void(uint64_t, uint64_t)>& progress) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string hex;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
      BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    std::ifstream input(path, std::ios::binary);
    std::error_code error;
    const uint64_t total = std::filesystem::file_size(path, error);
    std::vector<char> buffer(1 << 20);
    uint64_t done = 0;
    while (input) {
      input.read(buffer.data(), std::streamsize(buffer.size()));
      const auto count = input.gcount();
      if (count <= 0) break;
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(count), 0);
      done += uint64_t(count);
      if (progress) progress(done, total);
    }
    unsigned char digest[32];
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
      static const char* kHex = "0123456789abcdef";
      for (unsigned char byte : digest) {
        hex += kHex[byte >> 4];
        hex += kHex[byte & 15];
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  return hex;
}

std::wstring Megabytes(uint64_t bytes) {
  wchar_t text[32];
  swprintf(text, 32, L"%.1f MB", double(bytes) / (1024.0 * 1024.0));
  return text;
}

// True when DOAX2 from this folder is running (its exe is then locked).
bool GameRunning() {
  const auto exe = g_root / "out" / "game" / "doax2.exe";
  HANDLE file = CreateFileW(exe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
  CloseHandle(file);
  return false;
}

// Download, extraction and backup folder of an update.
std::filesystem::path UpdateTempFolder() {
  wchar_t temp[MAX_PATH + 1];
  const DWORD length = GetTempPathW(MAX_PATH + 1, temp);
  return std::filesystem::path(std::wstring(temp, length)) / L"DOAX2-Recomp-update";
}

// Folders of the install that an update never touches.
bool KeptByUpdate(const std::filesystem::path& relative) {
  if (relative.empty()) return true;
  const std::wstring first = relative.begin()->wstring();
  for (const wchar_t* kept : {L"userdata", L"config", kGameFolder})
    if (_wcsicmp(first.c_str(), kept) == 0) return true;
  return false;
}

void CheckForUpdate() {
  UpdateState([](UpdateProgress& u) {
    u = UpdateProgress{};
    u.busy = true;
    u.checked = true;
    u.message = L"Looking for a new version...";
  });
  StepRunning(kStepCheck, L"Contacting github.com");
  std::wstring url = kUpdateUrl;
  if (const wchar_t* test = _wgetenv(L"DOAX2_UPDATE_URL")) url = test;
  std::string json;
  std::wstring error;
  if (!HttpGet(url, &json, nullptr, nullptr, error)) {
    StepFailed(kStepCheck, L"Check failed", error);
    return;
  }
  const std::wstring latest = Widen(JsonString(json, "tag_name", 0));
  std::wstring zip_url, sums_url, zip_name;
  for (size_t at = 0;;) {
    size_t end = 0;
    const std::string asset = JsonString(json, "browser_download_url", at, &end);
    if (asset.empty()) break;
    at = end;
    const std::string name = asset.substr(asset.find_last_of('/') + 1);
    if (name.size() > 10 && name.compare(name.size() - 10, 10, "-win64.zip") == 0) {
      zip_url = Widen(asset);
      zip_name = Widen(name);
    } else if (name == "SHA256SUMS.txt") {
      sums_url = Widen(asset);
    }
  }
  if (latest.empty() || zip_url.empty() || sums_url.empty()) {
    StepFailed(kStepCheck, L"Check failed", L"The latest release on GitHub has no Windows download.");
    return;
  }
  StepDone(kStepCheck, L"Latest version: " + latest);
  const bool newer = NewerVersion(latest, kVersion);
  UpdateState([&](UpdateProgress& u) {
    u.busy = false;
    u.latest = latest;
    u.available = newer;
    u.zip_url = zip_url;
    u.sums_url = sums_url;
    u.zip_name = zip_name;
    u.message = newer ? latest + L" is available. Press INSTALL UPDATE to download and install it."
                      : std::wstring(L"You have the latest version.");
  });
}

void InstallUpdate() {
  const UpdateProgress start = UpdateSnapshot();
  UpdateState([](UpdateProgress& u) {
    u.busy = true;
    u.error = false;
    u.message = L"Installing " + u.latest + L". Please keep the launcher open.";
    for (int step = kStepDownload; step < kStepCount; ++step) {
      u.state[step] = StepState::kWaiting;
      u.detail[step].clear();
    }
  });
  const auto temp = UpdateTempFolder();
  std::error_code fs_error;
  std::filesystem::remove_all(temp, fs_error);
  std::filesystem::create_directories(temp / "extracted", fs_error);
  const auto cleanup = [&] { std::filesystem::remove_all(temp, fs_error); };

  // Download.
  StepRunning(kStepDownload, L"Starting", 0);
  std::wstring error;
  std::string sums;
  if (!HttpGet(start.sums_url, &sums, nullptr, nullptr, error)) {
    StepFailed(kStepDownload, L"Download failed", error);
    return cleanup();
  }
  const auto zip = temp / start.zip_name;
  uint64_t last_percent = 101;
  if (!HttpGet(
          start.zip_url, nullptr, &zip,
          [&](uint64_t received, uint64_t total) {
            const uint64_t percent = total ? received * 100 / total : received >> 20;
            if (percent == last_percent) return;
            last_percent = percent;
            StepRunning(kStepDownload, Megabytes(received) + (total ? L" of " + Megabytes(total) : std::wstring()),
                        total ? float(received) / float(total) : -1.0f);
          },
          error)) {
    StepFailed(kStepDownload, L"Download failed", error);
    return cleanup();
  }
  StepDone(kStepDownload, Megabytes(std::filesystem::file_size(zip, fs_error)) + L" downloaded");

  // Verify against SHA256SUMS.txt.
  StepRunning(kStepVerify, L"Checking the download", 0);
  std::string expected;
  {
    std::istringstream lines(sums);
    std::string line;
    const std::string name(start.zip_name.begin(), start.zip_name.end());
    while (std::getline(lines, line)) {
      std::istringstream fields(line);
      std::string hash, file;
      fields >> hash >> file;
      if (file == name) expected = hash;
    }
  }
  std::transform(expected.begin(), expected.end(), expected.begin(), [](char c) { return char(tolower(c)); });
  uint64_t last_verify = 101;
  const std::string actual = Sha256File(zip, [&](uint64_t done, uint64_t total) {
    const uint64_t percent = total ? done * 100 / total : 0;
    if (percent == last_verify) return;
    last_verify = percent;
    StepRunning(kStepVerify, L"Checking the download", total ? float(done) / float(total) : -1.0f);
  });
  if (expected.empty() || actual != expected) {
    StepFailed(kStepVerify, L"Checksum does not match",
               L"The download is damaged or incomplete, so nothing was changed. Try again.");
    return cleanup();
  }
  StepDone(kStepVerify, L"SHA-256 checksum matches");

  // Extract with Windows' tar.exe.
  StepRunning(kStepExtract, L"Unpacking files");
  wchar_t system[MAX_PATH];
  GetSystemDirectoryW(system, MAX_PATH);
  std::wstring command =
      L"\"" + std::wstring(system) + L"\\tar.exe\" -xf " + Quote(zip) + L" -C " + Quote(temp / "extracted");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  DWORD exit_code = 1;
  if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                     &startup, &process)) {
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
  }
  // The zip holds one top folder (DOAX2-Recomp-v<version>).
  std::filesystem::path package = temp / "extracted";
  std::vector<std::filesystem::path> top;
  for (const auto& entry : std::filesystem::directory_iterator(package, fs_error)) top.push_back(entry.path());
  if (top.size() == 1 && std::filesystem::is_directory(top[0])) package = top[0];
  if (exit_code != 0 || !std::filesystem::exists(package / "out" / "game" / "doax2.exe")) {
    StepFailed(kStepExtract, L"Unpacking failed", L"The update could not be unpacked, so nothing was changed.");
    return cleanup();
  }
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(package, fs_error)) {
    if (!entry.is_regular_file()) continue;
    const auto relative = entry.path().lexically_relative(package);
    if (!KeptByUpdate(relative)) files.push_back(relative);
  }
  StepDone(kStepExtract, std::to_wstring(files.size()) + L" files unpacked");

  // Install: back up every file that is replaced, then copy the new ones.
  StepRunning(kStepInstall, L"Replacing files", 0);
  if (GameRunning()) {
    StepFailed(kStepInstall, L"DOAX2 is running", L"Close DOAX2, then press INSTALL UPDATE again.");
    return cleanup();
  }
  const auto self = std::filesystem::path(ExecutablePath());
  const auto backup = temp / "backup";
  std::vector<std::filesystem::path> replaced, added;
  bool failed = false;
  std::wstring failed_file;
  for (size_t i = 0; i < files.size() && !failed; ++i) {
    const auto target = g_root / files[i];
    const auto source = package / files[i];
    std::filesystem::create_directories(target.parent_path(), fs_error);
    if (std::filesystem::exists(target)) {
      std::filesystem::create_directories((backup / files[i]).parent_path(), fs_error);
      if (!CopyFileW(target.c_str(), (backup / files[i]).c_str(), FALSE)) {
        failed = true;
      } else {
        replaced.push_back(files[i]);
        // The running launcher (or any other locked file) is moved aside first.
        const bool is_self = std::filesystem::equivalent(target, self, fs_error);
        if (is_self || !CopyFileW(source.c_str(), target.c_str(), FALSE)) {
          const std::wstring aside = target.wstring() + kOldSuffix;
          failed = !MoveFileExW(target.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING) ||
                   !CopyFileW(source.c_str(), target.c_str(), FALSE);
        }
      }
    } else {
      failed = !CopyFileW(source.c_str(), target.c_str(), FALSE);
      if (!failed) added.push_back(files[i]);
    }
    if (failed) failed_file = files[i].wstring();
    StepRunning(kStepInstall, std::to_wstring(i + 1) + L" of " + std::to_wstring(files.size()) + L" files",
                float(i + 1) / float(std::max<size_t>(files.size(), 1)));
  }
  if (failed) {
    for (const auto& file : replaced) {
      const auto target = g_root / file;
      if (!CopyFileW((backup / file).c_str(), target.c_str(), FALSE)) {
        // A file moved aside (the running launcher) goes back under its name.
        const std::wstring aside = target.wstring() + kOldSuffix;
        MoveFileExW(aside.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
      }
    }
    for (const auto& file : added) DeleteFileW((g_root / file).c_str());
    StepFailed(kStepInstall, L"Could not replace " + failed_file,
               L"The update could not replace " + failed_file + L". Your previous version was restored.");
    return cleanup();
  }
  cleanup();
  StepDone(kStepInstall, std::to_wstring(files.size()) + L" files updated");
  UpdateState([](UpdateProgress& u) {
    u.busy = false;
    u.installed = true;
    u.available = false;
    u.message = L"Updated to " + u.latest + L". Press RESTART LAUNCHER to finish.";
  });
}

void StartUpdateThread(void (*work)()) {
  {
    std::lock_guard lock(g_update_mutex);
    if (g_update.busy) return;
    g_update.busy = true;
  }
  SetTimer(g_window, kUpdateTimer, 40, nullptr);
  std::thread([work] {
    try {
      work();
    } catch (const std::exception& exception) {
      const std::string what = exception.what();
      UpdateState([&](UpdateProgress& u) {
        for (auto& state : u.state)
          if (state == StepState::kRunning) state = StepState::kFailed;
        u.message = L"The update stopped unexpectedly (" + Widen(what) + L").";
        u.error = true;
        u.busy = false;
      });
    }
  }).detach();
}

// Starts the updated launcher and closes this one.
void RestartLauncher() {
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::wstring command = Quote(ExecutablePath());
  if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, g_root.c_str(), &startup,
                     &process)) {
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    DestroyWindow(g_window);
  }
}

// Removes what a finished update left behind (the old launcher and any file
// that was locked) and the temp folder of an interrupted one. Right after an
// update the old launcher may still be closing, so this retries for a while.
void CleanUpAfterUpdate() {
  std::thread([root = g_root] {
    std::error_code error;
    std::filesystem::remove_all(UpdateTempFolder(), error);
    for (int attempt = 0; attempt < 20; ++attempt) {
      bool left = false;
      for (const auto& folder : {root, root / "out" / "game"})
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
          if (entry.path().wstring().ends_with(kOldSuffix) && !std::filesystem::remove(entry.path(), error))
            left = true;
      if (!left) return;
      Sleep(500);
    }
  }).detach();
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

int AddHit(const RectF& rect, std::function<void()> action, std::function<void()> right = {}) {
  const int id = int(g_hits.size());
  g_hits.push_back({rect, std::move(action), id, std::move(right)});
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
  DrawStatus(g, 630);
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
  DrawButton(g, RectF(1070, 698, 470, 38), L"SETTINGS", 23, [] { g_page = Page::kSettings; });
  DrawButton(g, RectF(1070, 742, 470, 38), L"GRAPHICS SETTINGS", 23, [] { g_page = Page::kGraphics; });
  DrawButton(g, RectF(1070, 786, 470, 38), L"INPUT", 23, [] { g_page = Page::kInput; });
  DrawButton(g, RectF(1070, 830, 470, 38), L"UPDATE", 23, [] {
    g_page = Page::kUpdate;
    // Opening the page is what starts the check (the only time the launcher goes online).
    const UpdateProgress u = UpdateSnapshot();
    if (!u.busy && !u.installed) StartUpdateThread(CheckForUpdate);
  });
}

// One box on the Input page: click, then press a key (keyboard tab) or a
// controller button (controller tab); right click clears it.
void DrawBindBox(Graphics& g, const RectF& r, size_t action, int slot, bool pad) {
  const int id = AddHit(
      r, [action, slot, pad] { StartCapture(action, slot, pad); },
      [action, slot, pad] {
        (pad ? g_settings.pad : g_settings.keys)[action][size_t(slot)].clear();
        g_input_note = std::wstring(kKeyActions[action].label) + (pad ? L" button cleared." : L" key cleared.");
        SaveSettings();
      });
  const bool capturing = g_capture_action == int(action) && g_capture_slot == slot && g_capture_pad == pad;
  const bool hover = id == g_hover;
  const std::string& bind = (pad ? g_settings.pad : g_settings.keys)[action][size_t(slot)];
  GraphicsPath path;
  RoundedRect(path, r, r.Height / 2);
  Color top = capturing ? kLime : bind.empty() ? Color(215, 24, 58, 112) : kOcean;
  Color bottom = capturing ? kGreen : bind.empty() ? Color(215, 8, 36, 84) : kDeepBlue;
  if (hover && !capturing) { top = Lighter(top, 0.25f); bottom = Lighter(bottom, 0.18f); }
  LinearGradientBrush fill(PointF(0, r.Y), PointF(0, r.Y + r.Height), top, bottom);
  fill.SetWrapMode(WrapModeTileFlipXY);
  g.FillPath(&fill, &path);
  Pen border(WithAlpha(kWhite, BYTE(capturing || hover ? 255 : 150)), capturing ? 3.0f : 2.0f);
  g.DrawPath(&border, &path);
  DrawText(g, capturing ? (pad ? L"Press a button" : L"Press a key") : pad ? PadLabel(bind) : KeyLabel(bind), r,
           pad && capturing ? 17 : 19, true,
           capturing ? kNavy : bind.empty() ? Color(255, 150, 175, 210) : kWhite, StringAlignmentCenter,
           StringAlignmentCenter, !capturing);
}

// Controller type ("PlayStation", "Switch", ...) or "Not found" for the
// controller tab. Drawn before the boxes, which use the type's button names.
void DrawControllerStatus(Graphics& g, const RectF& r) {
  bool connected = false;
  HeldPadInputs(&connected);
  const wchar_t* name = g_pad_family == PadFamily::kXbox          ? L"Xbox"
                        : g_pad_family == PadFamily::kPlayStation ? L"PlayStation"
                        : g_pad_family == PadFamily::kNintendo    ? L"Switch"
                                                                  : L"Connected";
  GraphicsPath path;
  RoundedRect(path, r, r.Height / 2);
  SolidBrush fill(connected ? kGreen : Color(215, 150, 20, 60));
  g.FillPath(&fill, &path);
  Pen border(WithAlpha(kWhite, 200), 2.5f);
  g.DrawPath(&border, &path);
  DrawText(g, connected ? name : L"Not found", r, 21, true, connected ? kNavy : kWhite,
           StringAlignmentCenter, StringAlignmentCenter, !connected);
}

// Input page, with a Keyboard and a Controller tab in the same layout: the
// controller diagram, a switch or status and help on the left, two boxes for
// every controller input on the right (two columns).
void PaintInput(Graphics& g) {
  const bool pad = g_input_controller_tab;
  DrawLogo(g, 250);
  DrawText(g, L"INPUT", RectF(300, 46, 1000, 56), 42, true, kWhite);
  const auto tab = [](bool selected) { return selected ? ButtonStyle::kSelected : ButtonStyle::kUnselected; };
  DrawButton(g, RectF(1100, 44, 220, 62), L"KEYBOARD", 24, [] {
    EndCapture();
    g_input_controller_tab = false;
    g_input_note.clear();
  }, tab(!pad));
  DrawButton(g, RectF(1340, 44, 220, 62), L"CONTROLLER", 24, [] {
    EndCapture();
    g_input_controller_tab = true;
    g_input_note.clear();
  }, tab(pad));
  // Left: diagram, switch or status, help.
  DrawPanel(g, RectF(40, 130, 670, 660));
  const RectF card(60, 150, 630, 630.0f * 925 / 2000 + 20);
  GraphicsPath card_path;
  RoundedRect(card_path, card, 22);
  SolidBrush card_fill(Color(240, 255, 255, 255));
  g.FillPath(&card_fill, &card_path);
  if (g_controller) g.DrawImage(g_controller.get(), RectF(card.X + 10, card.Y + 10, card.Width - 20, card.Height - 20));
  const float y = card.Y + card.Height + 24;
  if (pad) {
    DrawText(g, L"Player 1 Controller", RectF(70, y, 420, 62), 32, true, kWhite, StringAlignmentNear);
    DrawControllerStatus(g, RectF(510, y, 172, 62));
  } else {
    DrawText(g, L"Keyboard Controls", RectF(70, y, 420, 62), 32, true, kWhite, StringAlignmentNear);
    DrawToggle(g, RectF(510, y, 172, 62), &g_settings.keyboard);
  }
  DrawText(g,
           pad ? L"Choose which controller button does each action for player 1.\n\n"
                 L"Click a box, then press a button, trigger or stick direction (Esc cancels). Right-click "
                 L"a box to clear it. Picking a button that is already used swaps the two."
               : L"Play with the keyboard. A connected controller keeps working at the same time.\n\n"
                 L"Click a key box, then press the key you want (Esc cancels). Right-click a box to clear it. "
                 L"F3, F4, F7 and ` open the game's overlays and can't be used.",
           RectF(70, y + 76, 610, 200), 21, false, Color(255, 205, 228, 250), StringAlignmentNear,
           StringAlignmentNear, false);
  // Right: binds.
  DrawPanel(g, RectF(730, 130, 830, 660));
  const size_t per_column = (kKeyActionCount + 1) / 2;
  for (int column = 0; column < 2; ++column) {
    const float x = 750.0f + 405.0f * float(column);
    DrawText(g, pad ? L"BUTTON 1" : L"KEY 1", RectF(x + 125, 140, 125, 34), 18, true, Color(255, 160, 210, 255));
    DrawText(g, pad ? L"BUTTON 2" : L"KEY 2", RectF(x + 260, 140, 125, 34), 18, true, Color(255, 160, 210, 255));
    for (size_t row = 0; row < per_column; ++row) {
      const size_t action = size_t(column) * per_column + row;
      if (action >= kKeyActionCount) break;
      const float ry = 178.0f + 50.0f * float(row);
      DrawText(g, kKeyActions[action].label, RectF(x, ry, 128, 42), 19, true, kWhite, StringAlignmentNear);
      DrawBindBox(g, RectF(x + 125, ry + 2, 125, 40), action, 0, pad);
      DrawBindBox(g, RectF(x + 260, ry + 2, 125, 40), action, 1, pad);
    }
  }
  DrawButton(g, RectF(40, 812, 290, 76), L"‹  BACK", 33, [] { g_page = Page::kMain; });
  DrawButton(g, RectF(350, 812, 360, 76), L"RESET TO DEFAULTS", 25, [pad] {
    for (size_t i = 0; i < kKeyActionCount; ++i) {
      if (pad)
        g_settings.pad[i] = {kKeyActions[i].id, ""};
      else
        g_settings.keys[i] = {kKeyActions[i].primary, kKeyActions[i].secondary};
    }
    g_input_note = pad ? L"All controller buttons are back to the defaults." : L"All keys are back to the defaults.";
    SaveSettings();
  });
  DrawText(g, g_input_note.empty() ? std::wstring(L"Changes are saved automatically.") : g_input_note,
           RectF(730, 812, 830, 76), 21, !g_input_note.empty(), kWhite, StringAlignmentFar);
}

// Progress bar; a fraction below 0 draws a moving stripe (length unknown).
void DrawProgressBar(Graphics& g, const RectF& r, float fraction, StepState state) {
  GraphicsPath path;
  RoundedRect(path, r, r.Height / 2);
  SolidBrush track(Color(200, 4, 22, 54));
  g.FillPath(&track, &path);
  const Color top = state == StepState::kFailed ? Color(255, 240, 90, 120) : kLime;
  const Color bottom = state == StepState::kFailed ? Color(255, 170, 30, 70) : kGreen;
  RectF fill_rect = r;
  if (state == StepState::kRunning && fraction < 0) {
    const float phase = float(GetTickCount64() % 1400) / 1400.0f;
    fill_rect.Width = r.Width * 0.25f;
    fill_rect.X = r.X + (r.Width - fill_rect.Width) * (0.5f - 0.5f * std::cos(phase * 6.2831853f));
  } else {
    const float shown = state == StepState::kWaiting ? 0.0f : state == StepState::kFailed ? 1.0f : fraction;
    fill_rect.Width = r.Width * std::clamp(shown, 0.0f, 1.0f);
  }
  if (fill_rect.Width >= 1) {
    GraphicsPath fill_path;
    RoundedRect(fill_path, fill_rect, r.Height / 2);
    LinearGradientBrush fill(PointF(0, r.Y), PointF(0, r.Y + r.Height), top, bottom);
    fill.SetWrapMode(WrapModeTileFlipXY);
    g.FillPath(&fill, &fill_path);
  }
  Pen border(Color(150, 160, 210, 255), 1.5f);
  g.DrawPath(&border, &path);
}

// Update page: installed and latest version, the five steps with progress
// bars, and the button for the next step. Opening the page starts the check.
void PaintUpdate(Graphics& g) {
  const UpdateProgress u = UpdateSnapshot();
  DrawLogo(g, 250);
  DrawText(g, L"UPDATE", RectF(300, 46, 1000, 56), 42, true, kWhite);
  DrawPanel(g, RectF(150, 130, 1300, 660));
  DrawText(g, std::wstring(L"Installed version:  ") + kVersion, RectF(190, 146, 560, 46), 26, true, kWhite,
           StringAlignmentNear);
  DrawText(g, L"Latest version:  " + (u.latest.empty() ? std::wstring(u.busy ? L"checking..." : L"—") : u.latest),
           RectF(850, 146, 560, 46), 26, true, kWhite, StringAlignmentFar);
  const Color headline = u.error ? Color(255, 255, 160, 180) : u.available || u.installed ? Lighter(kLime, 0.2f)
                                                                                            : kWhite;
  DrawText(g, u.message, RectF(190, 198, 1220, 52), 25, true, headline, StringAlignmentNear);
  static const wchar_t* kStepNames[kStepCount] = {L"Check for updates", L"Download", L"Verify download",
                                                  L"Extract", L"Install"};
  for (int step = 0; step < kStepCount; ++step) {
    const float y = 268.0f + 78.0f * float(step);
    if (step) {
      Pen line(Color(70, 160, 210, 255), 1.5f);
      g.DrawLine(&line, 186.0f, y - 6, 1414.0f, y - 6);
    }
    const StepState state = u.state[step];
    // Step number, or a tick / cross once finished.
    const RectF badge(192, y + 8, 48, 48);
    SolidBrush badge_fill(state == StepState::kDone      ? kGreen
                          : state == StepState::kFailed  ? Color(255, 170, 30, 70)
                          : state == StepState::kRunning ? kAqua
                                                         : Color(215, 24, 58, 112));
    g.FillEllipse(&badge_fill, badge);
    Pen badge_border(WithAlpha(kWhite, 200), 2.0f);
    g.DrawEllipse(&badge_border, badge);
    DrawText(g, state == StepState::kDone ? L"✓" : state == StepState::kFailed ? L"✕" : std::to_wstring(step + 1),
             badge, 24, true, state == StepState::kDone ? kNavy : kWhite, StringAlignmentCenter,
             StringAlignmentCenter, false);
    DrawText(g, kStepNames[step], RectF(258, y, 300, 64), 25, true,
             state == StepState::kWaiting ? Color(255, 160, 185, 215) : kWhite, StringAlignmentNear);
    DrawProgressBar(g, RectF(570, y + 10, 600, 24), u.fraction[step], state);
    DrawText(g, u.detail[step], RectF(570, y + 38, 600, 28), 18, false, Color(255, 205, 228, 250),
             StringAlignmentNear, StringAlignmentCenter, false);
    if (state == StepState::kRunning && u.fraction[step] >= 0)
      DrawText(g, std::to_wstring(int(u.fraction[step] * 100 + 0.5f)) + L"%", RectF(1184, y + 4, 226, 36), 22, true,
               kWhite, StringAlignmentFar);
  }
  DrawText(g,
           L"Opening this page contacts github.com to look for a new version; nothing else is sent. Updating "
           L"keeps your saves (userdata), settings (config) and game files.",
           RectF(190, 662, 1220, 110), 20, false, Color(255, 205, 228, 250), StringAlignmentNear,
           StringAlignmentCenter, false);
  if (u.busy) {
    DrawText(g, L"Please wait...", RectF(460, 812, 990, 76), 24, true, kWhite, StringAlignmentFar);
    return;  // no Back while an update runs
  }
  DrawButton(g, RectF(150, 812, 290, 76), L"‹  BACK", 33, [] { g_page = Page::kMain; });
  if (u.installed) {
    DrawButton(g, RectF(1010, 812, 440, 76), L"RESTART LAUNCHER", 30, RestartLauncher, ButtonStyle::kAccent);
  } else if (u.available) {
    DrawButton(g, RectF(1010, 812, 440, 76), L"INSTALL UPDATE", 30, [] {
      if (GameRunning()) {
        UpdateState([](UpdateProgress& state) {
          state.message = L"Close DOAX2, then press INSTALL UPDATE again.";
          state.error = true;
        });
        return;
      }
      StartUpdateThread(InstallUpdate);
    }, ButtonStyle::kAccent);
  } else if (u.checked) {
    DrawButton(g, RectF(1010, 812, 440, 76), L"CHECK AGAIN", 30, [] { StartUpdateThread(CheckForUpdate); });
  }
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
      case Page::kInput: PaintInput(g); break;
      case Page::kUpdate: PaintUpdate(g); break;
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
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
      // Any click ends a key change; a key box click starts a new one.
      const bool was_capturing = g_capture_action >= 0;
      EndCapture();
      const int hit = HitTest(lparam);
      if (hit >= 0 && hit < int(g_hits.size())) {
        const Page before = g_page;
        auto action = message == WM_LBUTTONUP ? g_hits[size_t(hit)].action : g_hits[size_t(hit)].right;
        if (action) action();
        if (g_page != before) {
          SetStatus(L"", false);
          g_info.reset();
          g_input_note.clear();
        }
        g_hover = -1;
        InvalidateRect(window, nullptr, FALSE);
      } else if (was_capturing) {
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    }
    case WM_SYSKEYDOWN:
      // F10 arrives as a system key; everything else (Alt+F4) stays with Windows.
      if (g_capture_action < 0 || wparam != VK_F10) break;
      [[fallthrough]];
    case WM_KEYDOWN:
      if (g_capture_action >= 0) {
        const size_t action = size_t(g_capture_action);
        if (wparam == VK_ESCAPE) {
          EndCapture();
          g_input_note.clear();
        } else if (g_capture_pad) {
          g_input_note = L"Press a button on your controller, or Esc to cancel.";
        } else if (wparam == VK_SHIFT || wparam == VK_CONTROL || wparam == VK_MENU) {
          g_input_note = L"Shift, Ctrl and Alt can't be used on their own. Pick another key.";
        } else if (const KeyName* key = FindKey(UINT(wparam))) {
          g_capture_action = -1;
          AssignKey(action, g_capture_slot, key->name);
        } else {
          g_input_note = L"That key can't be used. Pick another key.";
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (wparam == VK_ESCAPE && g_info) {
        g_info.reset();
        InvalidateRect(window, nullptr, FALSE);
      } else if (wparam == VK_ESCAPE && g_page != Page::kMain && !(g_page == Page::kUpdate && UpdateSnapshot().busy)) {
        g_page = Page::kMain;
        InvalidateRect(window, nullptr, FALSE);
      } else if (wparam == VK_RETURN && g_page == Page::kMain) {
        Launch(window);
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_TIMER:
      if (wparam == kPadTimer && g_capture_action >= 0 && g_capture_pad) {
        // Take the first input pressed after the wait began; inputs already
        // held then count only once released and pressed again.
        bool connected = false;
        const uint32_t held = HeldPadInputs(&connected);
        g_pad_held &= held;
        const uint32_t pressed = held & ~g_pad_held;
        if (pressed) {
          const size_t action = size_t(g_capture_action);
          size_t input = 0;
          while (!(pressed & (1u << input))) ++input;
          EndCapture();
          AssignPad(action, g_capture_slot, kKeyActions[input].id);
          InvalidateRect(window, nullptr, FALSE);
        } else if (connected && g_input_note.rfind(L"No controller", 0) == 0) {
          g_input_note.clear();
          InvalidateRect(window, nullptr, FALSE);
        }
      } else if (wparam == kUpdateTimer) {
        if (g_page == Page::kUpdate) InvalidateRect(window, nullptr, FALSE);
      } else if (wparam == kStatusTimer && g_page == Page::kInput && g_input_controller_tab) {
        static bool last_connected = false;
        bool connected = false;
        HeldPadInputs(&connected);
        if (connected != last_connected) {
          last_connected = connected;
          InvalidateRect(window, nullptr, FALSE);
        }
      }
      return 0;
    case kUpdateChanged:
      if (!UpdateSnapshot().busy) KillTimer(window, kUpdateTimer);
      if (g_page == Page::kUpdate) InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_CLOSE:
      if (UpdateSnapshot().busy) {
        MessageBoxW(window, L"An update is in progress. Please wait until it has finished.", L"DOAX2 Recomp",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
      }
      break;
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
  CleanUpAfterUpdate();
  GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus_token = 0;
  GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
  g_background = LoadImageResource(IDR_LAUNCHER_BACKGROUND);
  g_logo = LoadImageResource(IDR_LAUNCHER_LOGO);
  g_controller = LoadImageResource(IDR_LAUNCHER_CONTROLLER);
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
  SetTimer(window, kStatusTimer, 1000, nullptr);
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
  g_controller.reset();
  SDL_Quit();
  GdiplusShutdown(gdiplus_token);
  return int(message.wParam);
}
