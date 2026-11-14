// CubeEvil4 Texture Packer, the window version (CubeEvil4-TexturePacker.exe); the command line version is texpack.cpp
// (CubeEvil4-TexturePacker-Console.exe). Both use the same core (packer.h) and make the same .c4tex file.
//
// Plain Win32 with GDI and GDI+ (both part of Windows): one .exe, nothing to install. One window with four pages:
//   1. the pack: a drop area for a ZIP file or a folder, "Choose ZIP file…" / "Choose folder…";
//   2. the options: the pack's choices (variants, add-ons) and the size, each with the file's size and the time;
//   3. the conversion (after Windows' Save dialog and the space check): steps, progress bar, counts, the image in work,
//      elapsed and remaining time, Cancel (deletes the unfinished file);
//   4. the end: done or failed, a summary (images, the file's size, refused images and why), Open folder, Show report.
// Reading the pack and converting run on threads of their own; the window stays responsive. Texts in English only
// (gui_text.cpp; decision of the project owner, 14.11.2026). It follows light and dark mode and the screen's DPI.
// Plan: port/docs/textur-packer-windows.md. Our own code (CC0).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "fileio.h"
#include "gui_text.h"
#include "packer.h"
#include "pc.h"
#include "zip.h"

namespace {

namespace fs = std::filesystem;
namespace gt = guitext;

constexpr unsigned kSizes[3] = {256, 512, 1024};
constexpr std::uint64_t kReserve = 100ull << 20;  // free space kept besides the pack file (as the Android app)
constexpr UINT kMsgReadProgress = WM_APP + 1, kMsgReadDone = WM_APP + 2, kMsgConvProgress = WM_APP + 3,
               kMsgConvDone = WM_APP + 4, kMsgRebuild = WM_APP + 5, kMsgLoad = WM_APP + 6;
constexpr UINT_PTR kTimerAnim = 1, kTimerTestDrop = 2;
const wchar_t kTitle[] = L"CubeEvil4 Texture Packer";
const wchar_t kMainClass[] = L"CubeEvil4TexturePacker", kContentClass[] = L"CubeEvil4TexturePackerContent";

// glyphs of Segoe Fluent Icons / Segoe MDL2 Assets
constexpr wchar_t kGlyphDrop = 0xE896, kGlyphFolder = 0xE8B7, kGlyphZip = 0xF012, kGlyphCheck = 0xE73E,
                  kGlyphCross = 0xE711;

enum class Page { kStart, kOptions, kConverting, kDone };

enum FontId {
  kFBody, kFBodyBold, kFSmall, kFHeading, kFTitle, kFBig, kFButton, kFButtonBold, kFIcon, kFIconMid, kFIconBig, kFontCount
};

enum ButtonId {
  kIdZip = 1001, kIdFolder, kIdOther, kIdStart, kIdCancel, kIdClose, kIdBack, kIdOpen, kIdReport,
  kIdSize = 1100,    // + 0..2
  kIdChoice = 2000,  // + group * 64 + variant + 1
  kIdAddOn = 9000    // + group
};

enum class Style { kPrimary, kSecondary, kRadio, kChip, kCheck };

struct Button {
  HWND h = nullptr;
  int id = 0;
  Style style = Style::kSecondary;
  bool bar = false;  // in the bottom bar (a child of the main window), else in the scrolling content
  bool selected = false, hover = false;
  std::wstring text, sub;
  COLORREF behind = 0;
  RECT rc{};  // content: before scrolling
};

struct Colors {
  COLORREF bg, bar, card, cardBorder, text, sub, accent, accentHover, accentPressed, accentText, selBg, btn, btnHover,
      btnPressed, btnBorder, ok, bad, warn, dropBg, dropBorder, dropActive, track, focus,
      ink;  // the accent as text and icons on the background (lighter in dark mode, for contrast)
};

// one thing to draw
struct Op {
  enum Kind { kText, kFill, kBorder, kDash, kCircle, kRing, kTrack, kMarquee, kSpin, kIcon } kind = kText;
  RECT r{};
  COLORREF c = 0, c2 = 0;
  int font = kFBody;
  int align = 0;  // 0 start, 1 centre, 2 end
  bool wrap = true, path = false;
  float radius = 0, value = 0;
  std::wstring text;
};

// a pack read by the reading thread
struct PackData {
  std::wstring path;
  bool zip = false;
  std::unique_ptr<pack::Pack> pack;
  std::map<const pack::Source *, std::array<std::uint64_t, 3>> each;  // each image's share of the estimate
};

struct ReadOutcome {
  std::unique_ptr<PackData> data;
  std::string error;  // a window text
};

struct ConvOutcome {
  pack::Outcome result;
  bool ok = false, cancelled = false;
  std::string error;  // a window text
  std::wstring out, report;
};

struct App {
  HINSTANCE inst = nullptr;
  HWND hwnd = nullptr, content = nullptr;
  int forceTheme = -1;  // -1 as Windows, 0 light, 1 dark
  bool dark = false, highContrast = false;
  UINT dpi = 96;
  HFONT font[kFontCount] = {};
  std::wstring uiFace = L"Segoe UI", iconFace = L"Segoe MDL2 Assets";
  HDC measure = nullptr;
  HICON icon = nullptr;
  int iconSize = 0;
  Colors c{};
  Page page = Page::kStart;
  bool reading = false, dragOver = false, modal = false, closeAfter = false;
  std::string message;  // a note on the start or options page (UTF-8)
  std::unique_ptr<PackData> pack;
  unsigned size = 512;
  // threads
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::mutex mu;
  bool posted = false;
  std::size_t listed = 0, sized = 0, toSize = 0;  // reading (under mu)
  pack::Progress progress;                       // converting (under mu)
  // the conversion as shown
  pack::Progress shown;
  bool cancelling = false;
  std::chrono::steady_clock::time_point convStart;
  double convEstimate = 0;
  std::wstring outPath;
  std::uint64_t outFree = 0, outNeed = 0;
  std::unique_ptr<ConvOutcome> done;
  // layout
  std::vector<Op> ops, barOps;
  std::vector<std::unique_ptr<Button>> buttons;
  int scroll = 0, maxScroll = 0, contentWidth = 0;
  bool inLayout = false;
  float anim = 0;
  int ticks = 0, percent = -1;
  RECT animRect{};  // the turning parts (content, before scrolling)
  ITaskbarList3 *taskbar = nullptr;
  IDropTarget *drop = nullptr;       // the window's drop target (registered with Windows)
  IDataObject *testData = nullptr;   // -testdrop: what Explorer hands over when the pack is dragged in
  std::wstring testSave;             // -testsave: the file the Save dialog would give (the tests run without it)
  UINT taskbarCreated = 0;
};

App g;
WNDPROC gButtonProc = nullptr;

// ---------------- text ----------------

std::wstring W(const std::string &s) {
  if (s.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string U8(const std::wstring &w) {
  if (w.empty()) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string T8(gt::Id id) { return gt::Text(id); }
std::wstring T(gt::Id id) { return W(gt::Text(id)); }
std::wstring TF(gt::Id id, std::initializer_list<std::string> args) { return W(gt::Format(gt::Text(id), args)); }
std::string Count(std::uint64_t v) { return gt::Count(v); }
std::string Size(std::uint64_t b) { return gt::Bytes(b); }

// Windows' text for an error code
std::string SystemError(DWORD code) {
  wchar_t *buf = nullptr;
  const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, code, 0, reinterpret_cast<wchar_t *>(&buf), 0, nullptr);
  std::wstring w = n && buf ? std::wstring(buf, n) : L"error " + std::to_wstring(code);
  if (buf) LocalFree(buf);
  while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ' || w.back() == L'.')) w.pop_back();
  return U8(w);
}

// ---------------- DPI, fonts, colours ----------------

int S(double dip) { return static_cast<int>(std::lround(dip * g.dpi / 96.0)); }

UINT WindowDpi(HWND h) {
  using Fn = UINT(WINAPI *)(HWND);
  static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (fn && h) return fn(h);
  HDC dc = GetDC(nullptr);
  const UINT d = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
  ReleaseDC(nullptr, dc);
  return d ? d : 96;
}

int CALLBACK FoundFont(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM found) {
  *reinterpret_cast<bool *>(found) = true;
  return 0;
}

bool FontExists(const wchar_t *name) {
  LOGFONTW lf{};
  lf.lfCharSet = DEFAULT_CHARSET;
  wcsncpy_s(lf.lfFaceName, name, _TRUNCATE);
  bool found = false;
  HDC dc = GetDC(nullptr);
  EnumFontFamiliesExW(dc, &lf, FoundFont, reinterpret_cast<LPARAM>(&found), 0);
  ReleaseDC(nullptr, dc);
  return found;
}

void ChooseFaces() {
  // Segoe UI Variable (Windows 11), else Segoe UI
  g.uiFace = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
  g.iconFace = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
}

void MakeFonts() {
  struct Spec {
    double px;
    int weight;
    bool icon;
  };
  static const Spec specs[kFontCount] = {{16, 400, false}, {16, 600, false}, {13.5, 400, false}, {19, 600, false},
                                         {25, 600, false}, {32, 700, false}, {15.5, 400, false}, {15.5, 600, false},
                                         {17, 400, true},  {28, 400, true}, {48, 400, true}};
  for (int i = 0; i < kFontCount; i++) {
    if (g.font[i]) DeleteObject(g.font[i]);
    LOGFONTW lf{};
    lf.lfHeight = -S(specs[i].px);
    lf.lfWeight = specs[i].weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, specs[i].icon ? g.iconFace.c_str() : g.uiFace.c_str(), _TRUNCATE);
    g.font[i] = CreateFontIndirectW(&lf);
  }
  const int want = S(40);
  if (!g.icon || g.iconSize != want) {
    if (g.icon) DestroyIcon(g.icon);
    g.icon = static_cast<HICON>(LoadImageW(g.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, want, want, 0));
    g.iconSize = want;
  }
}

COLORREF Mix(COLORREF a, COLORREF b, double t) {  // t of b
  auto m = [t](int x, int y) { return static_cast<BYTE>(std::lround(x + (y - x) * t)); };
  return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}

bool SystemDark() {
  DWORD v = 1, size = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS)
    return v == 0;
  return false;
}

void ChooseColors() {
  HIGHCONTRASTW hc{sizeof(hc)};
  g.highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
  g.dark = g.forceTheme >= 0 ? g.forceTheme == 1 : SystemDark();
  Colors &c = g.c;
  if (g.highContrast) {
    const COLORREF win = GetSysColor(COLOR_WINDOW), text = GetSysColor(COLOR_WINDOWTEXT),
                   hi = GetSysColor(COLOR_HIGHLIGHT), hiText = GetSysColor(COLOR_HIGHLIGHTTEXT),
                   face = GetSysColor(COLOR_BTNFACE), gray = GetSysColor(COLOR_GRAYTEXT);
    c = {win, win, win, text, text, text, hi, hi, hi, hiText, win, face, face, face, text, text, text, text, win,
         text, win, gray, text, hi};
    return;
  }
  if (g.dark) {
    c.bg = RGB(0x20, 0x20, 0x20);
    c.bar = RGB(0x1A, 0x1A, 0x1A);
    c.card = RGB(0x2B, 0x2B, 0x2B);
    c.cardBorder = RGB(0x3D, 0x3D, 0x3D);
    c.text = RGB(0xF2, 0xF2, 0xF2);
    c.sub = RGB(0xB8, 0xB8, 0xB8);
    c.accent = RGB(0xC4, 0x37, 0x2D);
    c.accentHover = RGB(0xD6, 0x48, 0x3D);
    c.accentPressed = RGB(0xA8, 0x2D, 0x25);
    c.accentText = RGB(0xFF, 0xFF, 0xFF);
    c.selBg = RGB(0x3B, 0x26, 0x24);
    c.btn = RGB(0x2F, 0x2F, 0x2F);
    c.btnHover = RGB(0x3A, 0x3A, 0x3A);
    c.btnPressed = RGB(0x27, 0x27, 0x27);
    c.btnBorder = RGB(0x4A, 0x4A, 0x4A);
    c.ok = RGB(0x5F, 0xC1, 0x52);
    c.bad = RGB(0xFF, 0x8A, 0x80);
    c.warn = RGB(0xF2, 0xC1, 0x4E);
    c.dropBg = RGB(0x26, 0x26, 0x26);
    c.dropBorder = RGB(0x60, 0x60, 0x60);
    c.dropActive = RGB(0x3B, 0x26, 0x24);
    c.track = RGB(0x3D, 0x3D, 0x3D);
    c.focus = RGB(0xFF, 0xFF, 0xFF);
    c.ink = RGB(0xFF, 0x9A, 0x8F);
  } else {
    c.bg = RGB(0xF3, 0xF3, 0xF3);
    c.bar = RGB(0xEA, 0xEA, 0xEA);
    c.card = RGB(0xFF, 0xFF, 0xFF);
    c.cardBorder = RGB(0xDC, 0xDC, 0xDC);
    c.text = RGB(0x1A, 0x1A, 0x1A);
    c.sub = RGB(0x5A, 0x5A, 0x5A);
    c.accent = RGB(0xB3, 0x26, 0x1E);
    c.accentHover = RGB(0x9E, 0x20, 0x19);
    c.accentPressed = RGB(0x86, 0x1A, 0x14);
    c.accentText = RGB(0xFF, 0xFF, 0xFF);
    c.selBg = RGB(0xFB, 0xEC, 0xEA);
    c.btn = RGB(0xFF, 0xFF, 0xFF);
    c.btnHover = RGB(0xF2, 0xF2, 0xF2);
    c.btnPressed = RGB(0xE6, 0xE6, 0xE6);
    c.btnBorder = RGB(0xC8, 0xC8, 0xC8);
    c.ok = RGB(0x10, 0x7C, 0x10);
    c.bad = RGB(0xC4, 0x2B, 0x1C);
    c.warn = RGB(0x8A, 0x52, 0x00);
    c.dropBg = RGB(0xFF, 0xFF, 0xFF);
    c.dropBorder = RGB(0xA8, 0xA8, 0xA8);
    c.dropActive = RGB(0xFB, 0xEC, 0xEA);
    c.track = RGB(0xE0, 0xE0, 0xE0);
    c.focus = RGB(0x1A, 0x1A, 0x1A);
    c.ink = RGB(0xB3, 0x26, 0x1E);
  }
}

// the title bar and the scroll bar in the theme's colours (Windows 10 1809 and later)
void ApplyThemeToWindows() {
  if (!g.hwnd) return;
  BOOL dark = g.dark && !g.highContrast;
  if (FAILED(DwmSetWindowAttribute(g.hwnd, 20, &dark, sizeof(dark)))) DwmSetWindowAttribute(g.hwnd, 19, &dark, sizeof(dark));
  static auto allow = [] {
    using AllowApp = int(WINAPI *)(int);
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (ux) {
      auto fn = reinterpret_cast<AllowApp>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
      OSVERSIONINFOEXW v{sizeof(v)};
      using Rtl = LONG(WINAPI *)(OSVERSIONINFOEXW *);
      auto rtl = reinterpret_cast<Rtl>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
      if (fn && rtl && rtl(&v) == 0 && v.dwMajorVersion >= 10 && v.dwBuildNumber >= 17763) {
        fn(1);  // AllowDarkModeForApp / SetPreferredAppMode(AllowDark)
        return reinterpret_cast<void *>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
      }
    }
    return static_cast<void *>(nullptr);
  }();
  if (allow && g.content) {
    using AllowWindow = BOOL(WINAPI *)(HWND, BOOL);
    reinterpret_cast<AllowWindow>(allow)(g.content, dark);
  }
  if (g.content) SetWindowTheme(g.content, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}

// ---------------- measuring ----------------

UINT TextFlags(int align, bool wrap, bool path) {
  UINT f = DT_NOPREFIX;
  f |= wrap ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER | (path ? DT_PATH_ELLIPSIS : DT_END_ELLIPSIS);
  return f | (align == 1 ? DT_CENTER : align == 2 ? DT_RIGHT : DT_LEFT);
}

int TextHeight(int font, const std::wstring &t, int width) {
  SelectObject(g.measure, g.font[font]);
  RECT r{0, 0, std::max(1, width), 0};
  DrawTextW(g.measure, t.c_str(), -1, &r, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
  return r.bottom;
}

int TextWidth(int font, const std::wstring &t) {
  SelectObject(g.measure, g.font[font]);
  RECT r{0, 0, 0, 0};
  DrawTextW(g.measure, t.c_str(), -1, &r, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
  return r.right;
}

int LineHeight(int font) {
  SelectObject(g.measure, g.font[font]);
  TEXTMETRICW tm{};
  GetTextMetricsW(g.measure, &tm);
  return tm.tmHeight;
}

// ---------------- the pack and the estimates ----------------

int SizeIndex() { return g.size == 256 ? 0 : g.size == 1024 ? 2 : 1; }

// the pack file's size at most for each size with the choices as they are, and the bytes of the images read
std::array<std::uint64_t, 3> Estimates(std::uint64_t *source = nullptr) {
  std::array<std::uint64_t, 3> e{pack::kEstimateBase, pack::kEstimateBase, pack::kEstimateBase};
  if (!g.pack) return e;
  pack::Settings s;
  std::string choices;
  std::uint64_t bytes = 0;
  for (const pack::Source *src : pack::Todo(*g.pack->pack, s, choices)) {
    auto it = g.pack->each.find(src);
    if (it != g.pack->each.end())
      for (int k = 0; k < 3; k++) e[k] += it->second[k];
    bytes += src->file.bytes;
  }
  if (source) *source = bytes;
  return e;
}

// about how long converting takes here: the times the Android app starts from (Ryzen 7 5700X, 16 threads, the owner's
// HD Project; Estimates.java), scaled by this PC's threads
double EstimateSeconds(std::uint64_t source, std::uint64_t estimate) {
  const double reference = 1.14 * static_cast<double>(source) / 1e9 + 330.0 * static_cast<double>(estimate) / 1e9;
  return reference * 16.0 / std::max(1u, std::thread::hardware_concurrency());
}

// ---------------- buttons ----------------

Button *Find(int id) {
  for (auto &b : g.buttons)
    if (b->id == id) return b.get();
  return nullptr;
}

void SetButtonText(Button *b, const std::wstring &text, const std::wstring &sub) {
  if (b->text == text && b->sub == sub) return;
  b->text = text;
  b->sub = sub;
  SetWindowTextW(b->h, (sub.empty() ? text : text + L", " + sub).c_str());
  InvalidateRect(b->h, nullptr, FALSE);
}

void EnsureVisible(Button *b);

LRESULT CALLBACK ButtonProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  Button *b = reinterpret_cast<Button *>(GetWindowLongPtrW(h, GWLP_USERDATA));
  switch (m) {
    case WM_MOUSEMOVE:
      if (b && !b->hover) {
        b->hover = true;
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        InvalidateRect(h, nullptr, FALSE);
      }
      break;
    case WM_MOUSELEAVE:
      if (b) {
        b->hover = false;
        InvalidateRect(h, nullptr, FALSE);
      }
      break;
    case WM_SETCURSOR:
      SetCursor(LoadCursorW(nullptr, IDC_HAND));
      return TRUE;
    case WM_ERASEBKGND:
      return 1;
    case WM_SETFOCUS:
      if (b && !b->bar) EnsureVisible(b);
      break;
    case WM_MOUSEWHEEL:
      return SendMessageW(g.content, m, w, l);
    default:
      break;
  }
  return CallWindowProcW(gButtonProc, h, m, w, l);
}

Button *AddButton(int id, Style style, const std::wstring &text, bool bar = false) {
  auto b = std::make_unique<Button>();
  b->id = id;
  b->style = style;
  b->bar = bar;
  b->text = text;
  b->h = CreateWindowExW(0, L"BUTTON", text.c_str(), WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                         bar ? g.hwnd : g.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
  SetWindowLongPtrW(b->h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(b.get()));
  WNDPROC old = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(b->h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ButtonProc)));
  if (!gButtonProc) gButtonProc = old;
  g.buttons.push_back(std::move(b));
  return g.buttons.back().get();
}

// ---------------- drawing ----------------

Gdiplus::Color GC(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

Gdiplus::RectF RF(const RECT &r, float inset = 0) {
  return Gdiplus::RectF(r.left + inset, r.top + inset, r.right - r.left - 2 * inset, r.bottom - r.top - 2 * inset);
}

void RoundPath(Gdiplus::GraphicsPath &p, Gdiplus::RectF r, float rad) {
  rad = std::min(rad, std::min(r.Width, r.Height) / 2);
  if (rad < 0.5f) {
    p.AddRectangle(r);
    return;
  }
  const float d = rad * 2;
  p.AddArc(r.X, r.Y, d, d, 180, 90);
  p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  p.CloseFigure();
}

void FillRound(Gdiplus::Graphics &gr, const RECT &r, float rad, COLORREF c) {
  Gdiplus::GraphicsPath p;
  RoundPath(p, RF(r), rad);
  Gdiplus::SolidBrush b(GC(c));
  gr.FillPath(&b, &p);
}

void StrokeRound(Gdiplus::Graphics &gr, const RECT &r, float rad, COLORREF c, float width, bool dashed = false) {
  Gdiplus::GraphicsPath p;
  RoundPath(p, RF(r, width / 2), std::max(0.0f, rad - width / 2));
  Gdiplus::Pen pen(GC(c), width);
  if (dashed) {
    const Gdiplus::REAL dash[] = {3.0f, 2.2f};
    pen.SetDashPattern(dash, 2);
    pen.SetDashCap(Gdiplus::DashCapRound);
  }
  gr.DrawPath(&pen, &p);
}

void DrawText(HDC dc, const RECT &r, const std::wstring &t, int font, COLORREF c, int align, bool wrap, bool path) {
  SelectObject(dc, g.font[font]);
  SetTextColor(dc, c);
  SetBkMode(dc, TRANSPARENT);
  RECT rr = r;
  DrawTextW(dc, t.c_str(), -1, &rr, TextFlags(align, wrap, path));
}

// ops onto a window's DC, double-buffered; dy: the content's scrolling
void Paint(HDC target, const RECT &area, const std::vector<Op> &ops, COLORREF bg, int dy) {
  const int w = area.right - area.left, h = area.bottom - area.top;
  if (w <= 0 || h <= 0) return;
  HDC mem = CreateCompatibleDC(target);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void *bits = nullptr;
  HBITMAP bmp = CreateDIBSection(target, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ oldBmp = SelectObject(mem, bmp);
  SetViewportOrgEx(mem, -area.left, -area.top, nullptr);
  HBRUSH brush = CreateSolidBrush(bg);
  FillRect(mem, &area, brush);
  DeleteObject(brush);
  auto at = [dy](const RECT &r) { return RECT{r.left, r.top - dy, r.right, r.bottom - dy}; };
  auto visible = [&](const RECT &r) { return r.bottom >= area.top && r.top <= area.bottom; };
  {
    Gdiplus::Graphics gr(mem);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gr.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    for (const Op &o : ops) {
      const RECT r = at(o.r);
      if (!visible(r)) continue;
      switch (o.kind) {
        case Op::kFill: FillRound(gr, r, o.radius, o.c); break;
        case Op::kBorder: StrokeRound(gr, r, o.radius, o.c, o.value); break;
        case Op::kDash: StrokeRound(gr, r, o.radius, o.c, o.value, true); break;
        case Op::kCircle: {
          Gdiplus::SolidBrush b(GC(o.c));
          gr.FillEllipse(&b, RF(r));
          break;
        }
        case Op::kRing: {
          Gdiplus::Pen pen(GC(o.c), o.value);
          gr.DrawEllipse(&pen, RF(r, o.value / 2));
          break;
        }
        case Op::kTrack: {
          FillRound(gr, r, (r.bottom - r.top) / 2.0f, o.c);
          const int fill = static_cast<int>(std::lround((r.right - r.left) * std::clamp(o.value, 0.0f, 1.0f)));
          if (fill > 0) {
            RECT f = r;
            f.right = f.left + std::max<LONG>(fill, r.bottom - r.top);
            FillRound(gr, f, (r.bottom - r.top) / 2.0f, o.c2);
          }
          break;
        }
        case Op::kMarquee: {
          FillRound(gr, r, (r.bottom - r.top) / 2.0f, o.c);
          const int wbar = r.right - r.left, seg = wbar / 3;
          const float t = std::fmod(g.anim * 0.6f, 1.0f);
          const int x0 = r.left - seg + static_cast<int>((wbar + seg) * t);
          RECT s{std::max<LONG>(r.left, x0), r.top, std::min<LONG>(r.right, x0 + seg), r.bottom};
          if (s.right > s.left) FillRound(gr, s, (r.bottom - r.top) / 2.0f, o.c2);
          break;
        }
        case Op::kSpin: {
          Gdiplus::Pen track(GC(o.c), o.value);
          gr.DrawEllipse(&track, RF(r, o.value / 2));
          Gdiplus::Pen pen(GC(o.c2), o.value);
          pen.SetStartCap(Gdiplus::LineCapRound);
          pen.SetEndCap(Gdiplus::LineCapRound);
          const float start = std::fmod(g.anim * 360.0f, 360.0f);
          gr.DrawArc(&pen, RF(r, o.value / 2), start, 100);
          break;
        }
        default: break;
      }
    }
  }
  for (const Op &o : ops) {
    const RECT r = at(o.r);
    if (!visible(r)) continue;
    if (o.kind == Op::kText) DrawText(mem, r, o.text, o.font, o.c, o.align, o.wrap, o.path);
    if (o.kind == Op::kIcon && g.icon) DrawIconEx(mem, r.left, r.top, g.icon, r.right - r.left, r.bottom - r.top, 0, nullptr, DI_NORMAL);
  }
  SetViewportOrgEx(mem, 0, 0, nullptr);
  BitBlt(target, area.left, area.top, w, h, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldBmp);
  DeleteObject(bmp);
  DeleteDC(mem);
}

void DrawButton(const DRAWITEMSTRUCT *d) {
  Button *b = reinterpret_cast<Button *>(GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA));
  if (!b) return;
  const RECT rc = d->rcItem;
  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  const bool pressed = (d->itemState & ODS_SELECTED) != 0, disabled = (d->itemState & ODS_DISABLED) != 0,
             focus = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT), hover = b->hover && !disabled;
  const Colors &c = g.c;
  std::vector<Op> ops;
  auto text = [&](RECT r, const std::wstring &t, int font, COLORREF col, int align, bool wrap) {
    Op o;
    o.kind = Op::kText;
    o.r = r;
    o.text = t;
    o.font = font;
    o.c = col;
    o.align = align;
    o.wrap = wrap;
    ops.push_back(o);
  };
  auto shape = [&](Op::Kind kind, RECT r, COLORREF col, float radius, float width = 0) {
    Op o;
    o.kind = kind;
    o.r = r;
    o.c = col;
    o.radius = radius;
    o.value = width;
    ops.push_back(o);
  };
  const RECT all{0, 0, w, h};
  const float rad = static_cast<float>(S(6));
  switch (b->style) {
    case Style::kPrimary: {
      const COLORREF fill = disabled ? Mix(c.accent, b->behind, 0.55) : pressed ? c.accentPressed : hover ? c.accentHover : c.accent;
      shape(Op::kFill, all, fill, rad);
      text(RECT{S(10), 0, w - S(10), h}, b->text, kFButtonBold, disabled ? Mix(c.accentText, fill, 0.35) : c.accentText, 1, false);
      break;
    }
    case Style::kSecondary: {
      shape(Op::kFill, all, pressed ? c.btnPressed : hover ? c.btnHover : c.btn, rad);
      shape(Op::kBorder, all, c.btnBorder, rad, 1);
      text(RECT{S(10), 0, w - S(10), h}, b->text, kFButton, disabled ? Mix(c.text, c.btn, 0.55) : c.text, 1, false);
      break;
    }
    case Style::kRadio: {
      shape(Op::kFill, all, b->selected ? c.selBg : hover ? c.btnHover : c.card, static_cast<float>(S(8)));
      shape(Op::kBorder, all, b->selected ? c.accent : hover ? c.sub : c.cardBorder, static_cast<float>(S(8)),
            static_cast<float>(b->selected ? S(2) : 1));
      const int cx = S(24), cy = h / 2, r = S(10);
      shape(Op::kRing, RECT{cx - r, cy - r, cx + r, cy + r}, b->selected ? c.accent : c.sub, 0, static_cast<float>(S(1.8)));
      if (b->selected) shape(Op::kCircle, RECT{cx - S(5), cy - S(5), cx + S(5), cy + S(5)}, c.accent, 0);
      const int tx = S(48), tw = w - tx - S(16);
      const int h1 = TextHeight(kFBodyBold, b->text, tw), h2 = b->sub.empty() ? 0 : TextHeight(kFSmall, b->sub, tw);
      const int top = (h - h1 - (h2 ? h2 + S(3) : 0)) / 2;
      text(RECT{tx, top, tx + tw, top + h1}, b->text, kFBodyBold, disabled ? c.sub : c.text, 0, true);
      if (h2) text(RECT{tx, top + h1 + S(3), tx + tw, top + h1 + S(3) + h2}, b->sub, kFSmall, c.sub, 0, true);
      break;
    }
    case Style::kChip: {
      const float pill = h / 2.0f;
      if (b->selected) {
        shape(Op::kFill, all, pressed ? c.accentPressed : hover ? c.accentHover : c.accent, pill);
      } else {
        shape(Op::kFill, all, pressed ? c.btnPressed : hover ? c.btnHover : c.btn, pill);
        shape(Op::kBorder, all, c.btnBorder, pill, 1);
      }
      text(RECT{S(14), 0, w - S(14), h}, b->text, b->selected ? kFButtonBold : kFButton,
           b->selected ? c.accentText : disabled ? c.sub : c.text, 1, false);
      break;
    }
    case Style::kCheck: {
      if (hover) shape(Op::kFill, all, c.btnHover, rad);
      const int box = S(20), bx = S(8), by = (h - box) / 2;
      const RECT br{bx, by, bx + box, by + box};
      if (b->selected) {
        shape(Op::kFill, br, c.accent, static_cast<float>(S(4)));
      } else {
        shape(Op::kFill, br, c.card, static_cast<float>(S(4)));
        shape(Op::kBorder, br, c.sub, static_cast<float>(S(4)), static_cast<float>(S(1.5)));
      }
      if (b->selected) {
        Op o;
        o.kind = Op::kText;
        o.r = br;
        o.text = std::wstring(1, kGlyphCheck);
        o.font = kFIcon;
        o.c = c.accentText;
        o.align = 1;
        o.wrap = false;
        ops.push_back(o);
      }
      text(RECT{bx + box + S(12), 0, w - S(8), h}, b->text, kFBody, disabled ? c.sub : c.text, 0, false);
      break;
    }
  }
  if (focus) shape(Op::kBorder, RECT{1, 1, w - 1, h - 1}, c.focus, b->style == Style::kChip ? (h - 2) / 2.0f : rad, static_cast<float>(S(2)));
  RECT area{0, 0, w, h};
  HDC dc = d->hDC;
  Paint(dc, area, ops, b->behind, 0);
}

// ---------------- layout ----------------

struct Builder {
  std::vector<Op> &ops;
  int Text(int x, int y, int w, int font, COLORREF c, const std::wstring &t, int align = 0, bool wrap = true,
           bool path = false) {
    const int h = wrap ? TextHeight(font, t, w) : LineHeight(font);
    Op o;
    o.kind = Op::kText;
    o.r = {x, y, x + w, y + h};
    o.font = font;
    o.c = c;
    o.text = t;
    o.align = align;
    o.wrap = wrap;
    o.path = path;
    ops.push_back(o);
    return h;
  }
  void Shape(Op::Kind kind, RECT r, COLORREF c, float radius = 0, float value = 0, COLORREF c2 = 0) {
    Op o;
    o.kind = kind;
    o.r = r;
    o.c = c;
    o.c2 = c2;
    o.radius = radius;
    o.value = value;
    ops.push_back(o);
  }
  void Glyph(RECT r, wchar_t glyph, int font, COLORREF c) {
    Op o;
    o.kind = Op::kText;
    o.r = r;
    o.text = std::wstring(1, glyph);
    o.font = font;
    o.c = c;
    o.align = 1;
    o.wrap = false;
    ops.push_back(o);
  }
};

void Place(Button *b, int x, int y, int w, int h, COLORREF behind) {
  b->rc = {x, y, x + w, y + h};
  b->behind = behind;
}

int ButtonWidth(const std::wstring &t, bool bold) { return std::max(S(112), TextWidth(bold ? kFButtonBold : kFButton, t) + S(40)); }

int Header(Builder &bd, int x, int w, int y) {
  const int icon = S(40);
  Op o;
  o.kind = Op::kIcon;
  o.r = {x, y, x + icon, y + icon};
  bd.ops.push_back(o);
  const int tx = x + icon + S(14), tw = w - icon - S(14);
  const int th = LineHeight(kFTitle), vh = LineHeight(kFSmall);
  const int top = y + (icon - th - vh) / 2;
  bd.Text(tx, top, tw, kFTitle, g.c.text, kTitle, 0, false);
  bd.Text(tx, top + th, tw, kFSmall, g.c.sub, TF(gt::kVersion, {pack::kToolVersion}), 0, false);
  return y + icon + S(18);
}

int LayoutStart(Builder &bd, int x, int w, int y) {
  const Colors &c = g.c;
  y += bd.Text(x, y, w, kFBody, c.text, T(gt::kIntro)) + S(8);
  y += bd.Text(x, y, w, kFSmall, c.sub, T(gt::kLegal)) + S(20);
  // the drop area
  const int pad = S(24), iw = w - 2 * pad;
  const int glyph = LineHeight(kFIconBig);
  const int titleH = TextHeight(kFHeading, T(gt::kDropTitle), iw), subH = TextHeight(kFBody, T(gt::kDropSub), iw);
  const int btnH = S(40);
  int inner = glyph + S(10) + titleH + S(2) + subH + S(22) + btnH;
  if (g.reading) inner = std::max(inner, glyph + S(14) + LineHeight(kFHeading) + S(16) + S(6) + S(12) + LineHeight(kFBody));
  const int zoneH = std::max(S(250), inner + 2 * pad);
  const RECT zone{x, y, x + w, y + zoneH};
  const bool active = g.dragOver && !g.reading;
  bd.Shape(Op::kFill, zone, active ? c.dropActive : c.dropBg, static_cast<float>(S(12)));
  bd.Shape(active ? Op::kBorder : Op::kDash, zone, active ? c.accent : c.dropBorder, static_cast<float>(S(12)),
           static_cast<float>(S(2)));
  int iy = y + (zoneH - inner) / 2;
  Button *zip = Find(kIdZip), *folder = Find(kIdFolder);
  if (g.reading) {
    bd.Glyph(RECT{x, iy, x + w, iy + glyph}, kGlyphDrop, kFIconBig, c.sub);
    iy += glyph + S(14);
    iy += bd.Text(x + pad, iy, iw, kFHeading, c.text, T(gt::kReading), 1, false) + S(16);
    bd.Shape(Op::kMarquee, RECT{x + w / 2 - S(130), iy, x + w / 2 + S(130), iy + S(6)}, c.track, 0, 0, c.accent);
    iy += S(6) + S(12);
    std::wstring line;
    {
      std::lock_guard<std::mutex> lock(g.mu);
      if (g.toSize) line = TF(gt::kReadingSizes, {Count(g.sized), Count(g.toSize)});
      else if (g.listed) line = TF(gt::kReadingFiles, {Count(g.listed)});
    }
    if (!line.empty()) bd.Text(x + pad, iy, iw, kFBody, c.sub, line, 1, false);
    if (zip) ShowWindow(zip->h, SW_HIDE);
    if (folder) ShowWindow(folder->h, SW_HIDE);
  } else {
    bd.Glyph(RECT{x, iy, x + w, iy + glyph}, kGlyphDrop, kFIconBig, active ? c.ink : c.sub);
    iy += glyph + S(10);
    if (active) {
      bd.Text(x + pad, iy, iw, kFHeading, c.ink, T(gt::kDropRelease), 1);
    } else {
      iy += bd.Text(x + pad, iy, iw, kFHeading, c.text, T(gt::kDropTitle), 1) + S(2);
      iy += bd.Text(x + pad, iy, iw, kFBody, c.sub, T(gt::kDropSub), 1) + S(22);
    }
    if (zip && folder) {
      const int bw = std::max(ButtonWidth(zip->text, false), ButtonWidth(folder->text, false));
      const int gap = S(12);
      if (2 * bw + gap <= iw) {
        const int bx = x + (w - 2 * bw - gap) / 2;
        Place(zip, bx, iy, bw, btnH, c.dropBg);
        Place(folder, bx + bw + gap, iy, bw, btnH, c.dropBg);
      } else {
        Place(zip, x + pad, iy, iw, btnH, c.dropBg);
        Place(folder, x + pad, iy + btnH + S(8), iw, btnH, c.dropBg);
      }
      const bool shown = !active;
      ShowWindow(zip->h, shown ? SW_SHOWNA : SW_HIDE);
      ShowWindow(folder->h, shown ? SW_SHOWNA : SW_HIDE);
    }
  }
  y += zoneH + S(14);
  if (!g.message.empty()) y += bd.Text(x, y, w, kFBody, c.bad, W(g.message)) + S(8);
  return y;
}

int LayoutOptions(Builder &bd, int x, int w, int y) {
  const Colors &c = g.c;
  pack::Pack &p = *g.pack->pack;
  // the pack
  {
    Button *other = Find(kIdOther);
    const int bw = other ? ButtonWidth(other->text, false) : 0;
    const int pad = S(16), glyph = S(36);
    const int tx = x + pad + glyph + S(14), tw = std::max(S(120), w - (tx - x) - pad - bw - S(14));
    const std::wstring line = TF(gt::kImagesSize, {Count(p.byKey.size()), Size(p.bytes)}) + L" · " +
                              T(g.pack->zip ? gt::kKindZip : gt::kKindFolder);
    const std::size_t left = p.skipped.size() + p.duplicates.size();
    const std::wstring leftOut = left ? TF(gt::kLeftOut, {Count(left)}) : std::wstring();
    const int h1 = TextHeight(kFHeading, W(p.name), tw), h2 = TextHeight(kFBody, line, tw),
              h3 = left ? TextHeight(kFSmall, leftOut, tw) : 0;
    const int cardH = std::max(S(72), h1 + S(2) + h2 + (h3 ? S(2) + h3 : 0) + 2 * S(14));
    const RECT card{x, y, x + w, y + cardH};
    bd.Shape(Op::kFill, card, c.card, static_cast<float>(S(10)));
    bd.Shape(Op::kBorder, card, c.cardBorder, static_cast<float>(S(10)), 1);
    bd.Glyph(RECT{x + pad, y, x + pad + glyph, y + cardH}, g.pack->zip ? kGlyphZip : kGlyphFolder, kFIconMid, c.ink);
    int ty = y + (cardH - (h1 + S(2) + h2 + (h3 ? S(2) + h3 : 0))) / 2;
    ty += bd.Text(tx, ty, tw, kFHeading, c.text, W(p.name)) + S(2);
    ty += bd.Text(tx, ty, tw, kFBody, c.sub, line) + S(2);
    if (h3) bd.Text(tx, ty, tw, kFSmall, c.sub, leftOut);
    if (other) {
      Place(other, x + w - pad - bw, y + (cardH - S(36)) / 2, bw, S(36), c.card);
      ShowWindow(other->h, SW_SHOWNA);
    }
    y += cardH + S(22);
  }
  // the choices
  if (!p.groups.empty()) {
    y += bd.Text(x, y, w, kFHeading, c.text, T(gt::kChoices)) + S(8);
    for (std::size_t gi = 0; gi < p.groups.size(); gi++) {
      const pack::Group &grp = p.groups[gi];
      if (grp.addOn) {
        if (Button *b = Find(kIdAddOn + static_cast<int>(gi))) {
          const int bh = std::max(S(38), LineHeight(kFBody) + S(12));
          Place(b, x - S(8), y, std::min(w + S(8), TextWidth(kFBody, b->text) + S(64)), bh, c.bg);
          ShowWindow(b->h, SW_SHOWNA);
          y += bh + S(6);
        }
        continue;
      }
      y += bd.Text(x, y, w, kFBodyBold, c.text, W(grp.name)) + S(6);
      int cx = x;
      const int ch = S(34), gap = S(8);
      for (int v = -1; v < static_cast<int>(grp.variants.size()); v++) {
        Button *b = Find(kIdChoice + static_cast<int>(gi) * 64 + v + 1);
        if (!b) continue;
        const int bw = std::min(w, TextWidth(kFButtonBold, b->text) + S(32));
        if (cx > x && cx + bw > x + w) {
          cx = x;
          y += ch + gap;
        }
        Place(b, cx, y, bw, ch, c.bg);
        ShowWindow(b->h, SW_SHOWNA);
        cx += bw + gap;
      }
      y += ch + S(14);
    }
    y += S(8);
  }
  // the size
  y += bd.Text(x, y, w, kFHeading, c.text, T(gt::kSize)) + S(8);
  std::uint64_t source = 0;
  const auto est = Estimates(&source);
  for (int i = 0; i < 3; i++) {
    Button *b = Find(kIdSize + i);
    if (!b) continue;
    const gt::Id label = i == 0 ? gt::kSize256 : i == 1 ? gt::kSize512 : gt::kSize1024;
    const std::wstring sub = TF(gt::kEstimate, {Size(est[i]), gt::About(EstimateSeconds(source, est[i]))});
    SetButtonText(b, T(label), sub);
    b->selected = kSizes[i] == g.size;
    const int tw = w - S(64);
    const int bh = std::max(S(64), TextHeight(kFBodyBold, b->text, tw) + S(3) + TextHeight(kFSmall, sub, tw) + 2 * S(12));
    Place(b, x, y, w, bh, c.bg);
    ShowWindow(b->h, SW_SHOWNA);
    y += bh + S(8);
  }
  y += bd.Text(x, y, w, kFSmall, c.sub, T(gt::kNeverSmaller)) + S(10);
  if (!g.message.empty()) y += bd.Text(x, y, w, kFBody, c.bad, W(g.message)) + S(8);
  return y;
}

double Elapsed() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - g.convStart).count();
}

double SecondsLeft() {
  const pack::Progress &p = g.shown;
  const double elapsed = Elapsed();
  if (p.done >= 20 && elapsed >= 10 && p.total > p.done) return elapsed * static_cast<double>(p.total - p.done) / p.done;
  return std::max(10.0, g.convEstimate - elapsed);
}

int LayoutConverting(Builder &bd, int x, int w, int y) {
  const Colors &c = g.c;
  const pack::Progress &p = g.shown;
  const bool finishing = p.total && p.done >= p.total;
  y += bd.Text(x, y, w, kFHeading, c.text, T(gt::kConverting)) + S(4);
  const fs::path out(g.outPath);
  y += bd.Text(x, y, w, kFBody, c.sub, W(g.pack->pack->name) + L"  →  " + out.filename().wstring()) + S(2);
  y += bd.Text(x, y, w, kFSmall, c.sub, TF(gt::kInFolder, {U8(out.parent_path().wstring())}), 0, false, true) + S(18);
  // the steps
  struct Step {
    gt::Id title, sub;
    int state;  // 0 waiting, 1 in work, 2 done
  };
  const std::wstring prepareSub = TF(gt::kStepPrepareSub, {Size(g.outFree), Size(g.outNeed)});
  const Step steps[3] = {{gt::kStepPrepare, gt::kStepPrepareSub, 2},
                         {gt::kStepConvert, gt::kStepConvertSub, finishing ? 2 : 1},
                         {gt::kStepFinish, gt::kStepFinishSub, finishing ? 1 : 0}};
  const int dot = S(30), tx = x + dot + S(14), tw = w - dot - S(14);
  for (int i = 0; i < 3; i++) {
    const Step &s = steps[i];
    const std::wstring sub = i == 0 ? prepareSub : T(s.sub);
    const int h1 = TextHeight(kFBodyBold, T(s.title), tw), h2 = TextHeight(kFSmall, sub, tw);
    const RECT d{x, y, x + dot, y + dot};
    if (s.state == 2) {
      bd.Shape(Op::kCircle, d, c.ok);
      bd.Glyph(d, kGlyphCheck, kFIcon, g.dark ? RGB(0x10, 0x10, 0x10) : RGB(0xFF, 0xFF, 0xFF));
    } else if (s.state == 1) {
      bd.Shape(Op::kSpin, d, c.track, 0, static_cast<float>(S(3)), c.accent);
    } else {
      bd.Shape(Op::kRing, d, c.cardBorder, 0, static_cast<float>(S(2)));
      bd.Text(d.left, d.top, dot, kFBodyBold, c.sub, std::to_wstring(i + 1), 1, false);
      bd.ops.back().r.bottom = d.bottom;
    }
    const int top = y + std::max(0, (dot - h1) / 2 - S(1));
    bd.Text(tx, top, tw, kFBodyBold, s.state ? c.text : c.sub, T(s.title));
    bd.Text(tx, top + h1 + S(1), tw, kFSmall, c.sub, sub);
    y = std::max(y + dot, top + h1 + S(1) + h2) + S(12);
  }
  y += S(6);
  // the progress card
  {
    const int pad = S(18), iw = w - 2 * pad;
    const int bigH = LineHeight(kFBig), statLabel = LineHeight(kFSmall), statValue = LineHeight(kFBodyBold);
    const int lineH = LineHeight(kFSmall), timeH = LineHeight(kFBody);
    const int cardH = pad + bigH + S(8) + S(12) + S(16) + statLabel + S(2) + statValue + S(14) + lineH + S(10) + timeH + pad;
    const RECT card{x, y, x + w, y + cardH};
    bd.Shape(Op::kFill, card, c.card, static_cast<float>(S(10)));
    bd.Shape(Op::kBorder, card, c.cardBorder, static_cast<float>(S(10)), 1);
    int cy = y + pad;
    const double frac = p.total ? static_cast<double>(p.done) / p.total : 0;
    const int percent = static_cast<int>(frac * 100);
    bd.Text(x + pad, cy, iw / 2, kFBig, c.text, TF(gt::kPercent, {std::to_string(percent)}), 0, false);
    bd.Text(x + pad + iw / 3, cy + bigH - LineHeight(kFBody) - S(4), iw - iw / 3, kFBody, c.sub,
            TF(gt::kOfImages, {Count(p.done), Count(p.total)}), 2, false);
    cy += bigH + S(8);
    bd.Shape(Op::kTrack, RECT{x + pad, cy, x + w - pad, cy + S(12)}, c.track, 0, static_cast<float>(frac), c.accent);
    cy += S(12) + S(16);
    const std::size_t skipped = g.pack->pack->skipped.size() + g.pack->pack->duplicates.size();
    const std::wstring values[4] = {W(Count(p.done) + " / " + Count(p.total)), W(Size(p.written)), W(Count(skipped)),
                                    W(Count(p.failed))};
    const gt::Id labels[4] = {gt::kStatConverted, gt::kStatWritten, gt::kStatSkipped, gt::kStatRefused};
    const int col = iw / 4;
    for (int i = 0; i < 4; i++) {
      const int sx = x + pad + i * col;
      bd.Text(sx, cy, col - S(8), kFSmall, c.sub, T(labels[i]), 0, false);
      bd.Text(sx, cy + statLabel + S(2), col - S(8), kFBodyBold, i == 3 && p.failed ? c.warn : c.text, values[i], 0, false);
    }
    cy += statLabel + S(2) + statValue + S(14);
    bd.Text(x + pad, cy, iw, kFSmall, c.sub, p.current.empty() ? std::wstring() : TF(gt::kNow, {p.current}), 0, false, true);
    cy += lineH + S(10);
    bd.Text(x + pad, cy, iw / 2, kFBody, c.text, TF(gt::kElapsed, {gt::Duration(Elapsed())}), 0, false);
    if (!finishing && !g.cancelling)
      bd.Text(x + pad + iw / 2, cy, iw - iw / 2, kFBody, c.text, TF(gt::kTimeLeft, {gt::About(SecondsLeft())}), 2, false);
    y += cardH + S(12);
  }
  if (g.cancelling) y += bd.Text(x, y, w, kFBody, c.warn, T(gt::kCancelling)) + S(8);
  return y;
}

int LayoutDone(Builder &bd, int x, int w, int y) {
  const Colors &c = g.c;
  const ConvOutcome &d = *g.done;
  const int big = S(56);
  const RECT dot{x, y, x + big, y + big};
  const COLORREF onDot = g.dark ? RGB(0x10, 0x10, 0x10) : RGB(0xFF, 0xFF, 0xFF);
  bd.Shape(Op::kCircle, dot, d.ok ? c.ok : d.cancelled ? c.sub : c.bad);
  bd.Glyph(dot, d.ok ? kGlyphCheck : kGlyphCross, kFIconMid, onDot);
  const int tx = x + big + S(16), tw = w - big - S(16);
  const gt::Id title = d.ok ? gt::kDone : d.cancelled ? gt::kCancelled : gt::kFailed;
  const int th = LineHeight(kFTitle);
  bd.Text(tx, y + (big - th) / 2, tw, kFTitle, c.text, T(title), 0, false);
  y += big + S(18);
  if (!d.ok) {
    if (!d.cancelled && !d.error.empty()) y += bd.Text(x, y, w, kFBody, c.text, TF(gt::kReason, {d.error})) + S(6);
    y += bd.Text(x, y, w, kFBody, c.sub, T(gt::kNoFileLeft)) + S(8);
    return y;
  }
  const pack::Outcome &r = d.result;
  const fs::path out(d.out);
  y += bd.Text(x, y, w, kFBodyBold, c.text, TF(gt::kSaved, {U8(out.filename().wstring()), Size(r.fileBytes)})) + S(2);
  y += bd.Text(x, y, w, kFSmall, c.sub, TF(gt::kInFolder, {U8(out.parent_path().wstring())}), 0, false, true) + S(12);
  y += bd.Text(x, y, w, kFBody, c.text,
               TF(gt::kConvertedIn, {Count(r.entries), gt::Duration(r.seconds)}) + L" " +
                   TF(gt::kKinds, {Count(r.counts[1]), Count(r.counts[2]), Count(r.counts[3])})) +
       S(8);
  if (r.skipped) y += bd.Text(x, y, w, kFBody, c.sub, TF(gt::kSkippedDone, {Count(r.skipped)})) + S(8);
  if (!r.failures.empty()) {
    y += bd.Text(x, y, w, kFBody, c.warn, TF(gt::kRefusedDone, {Count(r.failures.size())})) + S(4);
    const std::size_t shown = std::min<std::size_t>(r.failures.size(), 6);
    for (std::size_t i = 0; i < shown; i++)
      y += bd.Text(x + S(16), y, w - S(16), kFSmall, c.text, W("• " + r.failures[i].path + ": " + r.failures[i].reason)) + S(2);
    if (r.failures.size() > shown) y += bd.Text(x + S(16), y, w - S(16), kFSmall, c.sub, TF(gt::kMore, {Count(r.failures.size() - shown)})) + S(2);
    y += S(8);
  }
  // Open folder, Show report
  Button *open = Find(kIdOpen), *report = Find(kIdReport);
  if (open && report) {
    const int bw1 = ButtonWidth(open->text, false), bw2 = ButtonWidth(report->text, false);
    Place(open, x, y + S(4), bw1, S(38), c.bg);
    Place(report, x + bw1 + S(10), y + S(4), bw2, S(38), c.bg);
    ShowWindow(open->h, SW_SHOWNA);
    ShowWindow(report->h, SW_SHOWNA);
    y += S(4) + S(38) + S(18);
  }
  // what comes next
  {
    const int pad = S(16);
    const int th2 = TextHeight(kFBody, T(gt::kNext), w - 2 * pad - S(4));
    const RECT card{x, y, x + w, y + th2 + 2 * pad};
    bd.Shape(Op::kFill, card, c.card, static_cast<float>(S(10)));
    bd.Shape(Op::kFill, RECT{x, y, x + S(4), y + th2 + 2 * pad}, c.accent, static_cast<float>(S(2)));
    bd.Text(x + pad + S(4), y + pad, w - 2 * pad - S(4), kFBody, c.text, T(gt::kNext));
    y += th2 + 2 * pad + S(8);
  }
  return y;
}

// the bottom bar of the main window: its background and its buttons
int BarHeight() { return S(68); }

void LayoutBar(int W, int H) {
  g.barOps.clear();
  Builder bd{g.barOps};
  const int bh = BarHeight(), top = H - bh;
  bd.Shape(Op::kFill, RECT{0, top, W, H}, g.c.bar);
  bd.Shape(Op::kFill, RECT{0, top, W, top + 1}, g.c.cardBorder);
  const int m = S(28), h = S(38), y = top + (bh - h) / 2;
  int start = m, end = W - m;
  // a pack dragged over the options or the end: the bar says what letting go does (the drop area is on page 1 only)
  const bool dragNote = g.dragOver && g.page != Page::kStart;
  if (dragNote) {
    bd.Text(m, top, W - 2 * m, kFHeading, g.c.ink, T(gt::kDropRelease), 1, false);
    bd.ops.back().r.bottom = H;
  }
  for (auto &b : g.buttons) {
    if (!b->bar) continue;
    if (dragNote) {
      ShowWindow(b->h, SW_HIDE);
      continue;
    }
    const bool primary = b->style == Style::kPrimary;
    const int bw = ButtonWidth(b->text, primary);
    // the main step at the end, Back at the start; Close at the start only beside a main step
    const bool atEnd = primary || b->id == kIdCancel || (b->id == kIdClose && g.page != Page::kOptions);
    if (atEnd) {
      Place(b.get(), end - bw, y, bw, h, g.c.bar);
      end -= bw + S(10);
    } else {
      Place(b.get(), start, y, bw, h, g.c.bar);
      start += bw + S(10);
    }
    ShowWindow(b->h, SW_SHOWNA);
  }
}

// (one by one: DeferWindowPos needs all windows under one parent, and the bar's buttons are not under the content)
void PositionButtons() {
  for (auto &b : g.buttons) {
    RECT r = b->rc;
    if (!b->bar) OffsetRect(&r, 0, -g.scroll);
    RECT now;
    GetWindowRect(b->h, &now);
    MapWindowPoints(nullptr, GetParent(b->h), reinterpret_cast<POINT *>(&now), 2);
    if (!EqualRect(&now, &r))
      SetWindowPos(b->h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(b->h, nullptr, FALSE);
  }
}

void Layout() {
  if (!g.hwnd || !g.content || g.inLayout) return;
  g.inLayout = true;
  RECT mc;
  GetClientRect(g.hwnd, &mc);
  const int W = mc.right, H = mc.bottom;
  const int contentH = std::max(0, H - BarHeight());
  for (int pass = 0; pass < 2; pass++) {
    RECT cc;
    GetClientRect(g.content, &cc);
    const int cw = cc.right;
    g.ops.clear();
    Builder bd{g.ops};
    const int m = S(28), x = m, w = std::max(S(200), std::min(cw - 2 * m, S(900)));
    const int left = cw - 2 * m > w ? (cw - w) / 2 : x;
    int y = S(22);
    y = Header(bd, left, w, y);
    switch (g.page) {
      case Page::kStart: y = LayoutStart(bd, left, w, y); break;
      case Page::kOptions: y = LayoutOptions(bd, left, w, y); break;
      case Page::kConverting: y = LayoutConverting(bd, left, w, y); break;
      case Page::kDone: y = LayoutDone(bd, left, w, y); break;
    }
    const int total = y + S(12);
    g.maxScroll = std::max(0, total - contentH);
    g.scroll = std::clamp(g.scroll, 0, g.maxScroll);
    SCROLLINFO si{sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, total - 1);
    si.nPage = static_cast<UINT>(std::max(1, contentH));
    si.nPos = g.scroll;
    SetScrollInfo(g.content, SB_VERT, &si, TRUE);
    GetClientRect(g.content, &cc);
    if (cc.right == cw) break;  // the scroll bar came or went: once more with the new width
  }
  RECT cc;
  GetClientRect(g.content, &cc);
  g.contentWidth = cc.right;
  LayoutBar(W, H);
  g.animRect = RECT{};
  for (const Op &o : g.ops)
    if (o.kind == Op::kSpin || o.kind == Op::kMarquee) UnionRect(&g.animRect, &g.animRect, &o.r);
  PositionButtons();
  InvalidateRect(g.content, nullptr, FALSE);
  InvalidateRect(g.hwnd, nullptr, FALSE);
  g.inLayout = false;
}

void EnsureVisible(Button *b) {
  RECT cc;
  GetClientRect(g.content, &cc);
  int y = g.scroll;
  if (b->rc.top - S(12) < y) y = b->rc.top - S(12);
  if (b->rc.bottom + S(12) > y + cc.bottom) y = b->rc.bottom + S(12) - cc.bottom;
  y = std::clamp(y, 0, g.maxScroll);
  if (y != g.scroll) {
    g.scroll = y;
    SetScrollPos(g.content, SB_VERT, y, TRUE);
    PositionButtons();
    InvalidateRect(g.content, nullptr, FALSE);
  }
}

void ScrollTo(int y) {
  y = std::clamp(y, 0, g.maxScroll);
  if (y == g.scroll) return;
  g.scroll = y;
  SetScrollPos(g.content, SB_VERT, y, TRUE);
  PositionButtons();
  InvalidateRect(g.content, nullptr, FALSE);
  UpdateWindow(g.content);
}

// ---------------- pages ----------------

void SetTaskbar(TBPFLAG state, std::uint64_t done = 0, std::uint64_t total = 0) {
  if (!g.taskbar) return;
  g.taskbar->SetProgressState(g.hwnd, state);
  if (state == TBPF_NORMAL || state == TBPF_ERROR) g.taskbar->SetProgressValue(g.hwnd, done, std::max<std::uint64_t>(total, 1));
}

void UpdateTimer() {
  const bool animate = g.reading || g.page == Page::kConverting;
  if (animate) SetTimer(g.hwnd, kTimerAnim, 33, nullptr);
  else KillTimer(g.hwnd, kTimerAnim);
}

// the buttons of the page as it is now
void Rebuild() {
  HWND focus = GetFocus();
  for (auto &b : g.buttons) {
    if (b->h == focus) SetFocus(g.hwnd);
    DestroyWindow(b->h);
  }
  g.buttons.clear();
  g.scroll = 0;
  Button *first = nullptr;
  switch (g.page) {
    case Page::kStart:
      AddButton(kIdZip, Style::kSecondary, T(gt::kChooseZip));
      AddButton(kIdFolder, Style::kSecondary, T(gt::kChooseFolder));
      first = AddButton(kIdClose, Style::kSecondary, T(gt::kClose), true);
      first = Find(kIdZip);
      break;
    case Page::kOptions: {
      AddButton(kIdOther, Style::kSecondary, T(gt::kOtherPack));
      const pack::Pack &p = *g.pack->pack;
      for (std::size_t gi = 0; gi < p.groups.size(); gi++) {
        const pack::Group &grp = p.groups[gi];
        if (grp.addOn) {
          Button *b = AddButton(kIdAddOn + static_cast<int>(gi), Style::kCheck, W(grp.name + ": ") + T(gt::kAdd));
          b->selected = grp.chosen == 0;
          continue;
        }
        for (int v = -1; v < static_cast<int>(grp.variants.size()); v++) {
          std::wstring name = v < 0 ? T(gt::kAsMain)
                                    : grp.variants[static_cast<std::size_t>(v)].sameAsMain
                                          ? TF(gt::kSameAsMain, {grp.variants[static_cast<std::size_t>(v)].name})
                                          : W(grp.variants[static_cast<std::size_t>(v)].name);
          Button *b = AddButton(kIdChoice + static_cast<int>(gi) * 64 + v + 1, Style::kChip, name);
          b->selected = grp.chosen == v;
        }
      }
      for (int i = 0; i < 3; i++) AddButton(kIdSize + i, Style::kRadio, L"");
      AddButton(kIdClose, Style::kSecondary, T(gt::kClose), true);
      first = AddButton(kIdStart, Style::kPrimary, T(gt::kStart), true);
      break;
    }
    case Page::kConverting:
      first = AddButton(kIdCancel, Style::kSecondary, T(gt::kCancel), true);
      break;
    case Page::kDone:
      if (g.done && g.done->ok) {
        AddButton(kIdOpen, Style::kSecondary, T(gt::kOpenFolder));
        AddButton(kIdReport, Style::kSecondary, T(gt::kShowReport));
      }
      AddButton(kIdBack, Style::kSecondary, T(gt::kBack), true);
      first = AddButton(kIdClose, g.done && g.done->ok ? Style::kPrimary : Style::kSecondary, T(gt::kClose), true);
      break;
  }
  UpdateTimer();
  Layout();
  if (first && IsWindowEnabled(first->h) && GetForegroundWindow() == g.hwnd) SetFocus(first->h);
  // the window's title shows the progress while converting
  g.percent = -1;
  SetWindowTextW(g.hwnd, kTitle);
}

void Go(Page page) {
  g.page = page;
  PostMessageW(g.hwnd, kMsgRebuild, 0, 0);
}

// ---------------- reading a pack ----------------

void PostReadProgress() {
  {
    std::lock_guard<std::mutex> lock(g.mu);
    if (g.posted) return;
    g.posted = true;
  }
  PostMessageW(g.hwnd, kMsgReadProgress, 0, 0);
}

void ReadThread(std::wstring path, bool zip) {
  auto out = std::make_unique<ReadOutcome>();
  try {
    auto data = std::make_unique<PackData>();
    data->path = path;
    data->zip = zip;
    const fs::path root(path);
    std::vector<pack::InputFile> files;
    std::string name;
    if (zip) {
      std::string error;
      auto access = zipr::OpenFile(fio::Utf8(root), error);
      if (!access || !pack::ListZip(access, fio::Utf8(root.filename()), files, name, error)) {
        out->error = gt::Format(T8(gt::kZipFailed), {error});
        PostMessageW(g.hwnd, kMsgReadDone, 0, reinterpret_cast<LPARAM>(out.release()));
        return;
      }
    } else {
      files = pack::ListFolder(fio::Utf8(root));
      name = fio::Utf8(root.filename());
      if (name.empty()) name = fio::Utf8(root);
    }
    {
      std::lock_guard<std::mutex> lock(g.mu);
      g.listed = files.size();
    }
    PostReadProgress();
    data->pack = std::make_unique<pack::Pack>();
    pack::Prepare(std::move(files), name, *data->pack);
    if (data->pack->byKey.empty()) {
      out->error = T8(gt::kNoImages);
    } else {
      // every image a choice can bring in: the main ones and each variant's
      std::vector<const pack::Source *> sources;
      std::set<const pack::Source *> seen;
      for (const auto &kv : data->pack->byKey)
        if (seen.insert(kv.second).second) sources.push_back(kv.second);
      for (const pack::Group &grp : data->pack->groups)
        for (const pack::Variant &v : grp.variants)
          for (const pack::Source &s : v.files)
            if (seen.insert(&s).second) sources.push_back(&s);
      {
        std::lock_guard<std::mutex> lock(g.mu);
        g.toSize = sources.size();
      }
      PostReadProgress();
      const auto each = pack::EstimateEach(sources, {kSizes[0], kSizes[1], kSizes[2]}, [](std::size_t done) {
        {
          std::lock_guard<std::mutex> lock(g.mu);
          g.sized = done;
        }
        PostReadProgress();
      });
      for (std::size_t i = 0; i < sources.size(); i++) data->each[sources[i]] = {each[i][0], each[i][1], each[i][2]};
      out->data = std::move(data);
    }
  } catch (const std::exception &e) {
    out->error = gt::Format(T8(gt::kZipFailed), {e.what()});
  }
  PostMessageW(g.hwnd, kMsgReadDone, 0, reinterpret_cast<LPARAM>(out.release()));
}

std::wstring Lower(std::wstring s) {
  for (wchar_t &ch : s) ch = static_cast<wchar_t>(towlower(ch));
  return s;
}

// a dropped or chosen path: a folder or a ZIP file
void Load(std::wstring path) {
  if (g.reading || g.page == Page::kConverting || g.worker.joinable()) return;
  while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
  std::error_code ec;
  fs::path p(path);
  std::string problem;
  bool zip = false;
  if (fs::is_directory(p, ec)) {
    zip = false;
  } else if (fs::is_regular_file(p, ec)) {
    const std::wstring ext = Lower(p.extension().wstring());
    if (ext == L".7z" || ext == L".rar") problem = T8(gt::kUnpackFirst);
    else if (ext != L".zip") problem = gt::Format(T8(gt::kNotZip), {U8(p.filename().wstring())});
    zip = true;
  } else {
    problem = gt::Format(T8(gt::kNotFound), {U8(path)});
  }
  if (!problem.empty()) {
    // the page stays (a pack already read stays too)
    g.message = problem;
    if (g.page == Page::kDone) {
      g.done.reset();
      Go(g.pack ? Page::kOptions : Page::kStart);
    } else {
      Layout();
    }
    return;
  }
  const fs::path canon = fs::weakly_canonical(p, ec);
  if (!ec && !canon.empty()) p = canon;
  g.message.clear();
  g.pack.reset();
  g.done.reset();
  g.page = Page::kStart;
  g.reading = true;
  {
    std::lock_guard<std::mutex> lock(g.mu);
    g.listed = g.sized = g.toSize = 0;
    g.posted = false;
  }
  SetTaskbar(TBPF_INDETERMINATE);
  Rebuild();
  g.worker = std::thread(ReadThread, p.wstring(), zip);
}

// ---------------- converting ----------------

void ConvThread(FILE *f, std::wstring outPath, std::wstring part, std::vector<const pack::Source *> todo,
                std::string choices, pack::Settings settings) {
  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);  // no sleep while converting
  auto r = std::make_unique<ConvOutcome>();
  r->out = outPath;
  const fs::path out(outPath);
  pc::FileSink sink(f, out.parent_path());
  r->result = pack::Convert(*g.pack->pack, todo, choices, settings, sink,
                            [](const pack::Progress &p) {
                              {
                                std::lock_guard<std::mutex> lock(g.mu);
                                g.progress = p;
                                if (g.posted) return;
                                g.posted = true;
                              }
                              PostMessageW(g.hwnd, kMsgConvProgress, 0, 0);
                            },
                            &g.cancel);
  const bool closed = std::fclose(f) == 0;
  std::error_code ec;
  if (!r->result.ok || !closed) {
    fs::remove(fs::path(part), ec);
    r->cancelled = r->result.cancelled;
    r->error = r->result.diskFull ? T8(gt::kDiskFull)
               : r->result.error == "no image could be converted" ? T8(gt::kNoImageConverted)
                                                                   : T8(gt::kWriteFailed);
  } else {
    fs::remove(out, ec);
    ec.clear();
    fs::rename(fs::path(part), out, ec);
    if (ec) {
      r->error = T8(gt::kWriteFailed) + " (" + SystemError(static_cast<DWORD>(ec.value())) + ")";
      fs::remove(fs::path(part), ec);
    } else {
      fs::path report = out;
      report.replace_extension(L".txt");
      fio::WriteFile(fio::Utf8(report), std::vector<std::uint8_t>(r->result.report.begin(), r->result.report.end()));
      r->report = report.wstring();
      r->ok = true;
    }
  }
  SetThreadExecutionState(ES_CONTINUOUS);
  PostMessageW(g.hwnd, kMsgConvDone, 0, reinterpret_cast<LPARAM>(r.release()));
}

// the folder the Save dialog starts in the first time (later Windows remembers the one chosen last): where the pack is
// (the ZIP's folder, the pack folder's parent)
IShellItem *StartFolder() {
  IShellItem *item = nullptr;
  if (g.pack) {
    const fs::path parent = fs::path(g.pack->path).parent_path();
    if (!parent.empty() && SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(), nullptr, IID_PPV_ARGS(&item)))) return item;
  }
  PWSTR desk = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk)))
    SHCreateItemFromParsingName(desk, nullptr, IID_PPV_ARGS(&item));
  CoTaskMemFree(desk);
  return item;
}

// Windows' Save dialog with the usual name; false when cancelled
bool AskSavePath(std::wstring &path) {
  if (!g.testSave.empty()) {
    path = g.testSave;
    return true;
  }
  IFileSaveDialog *d = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
  static const GUID kSaveGuid = {0x6c3b8d2a, 0x51e4, 0x4f0a, {0x9b, 0x77, 0x2e, 0x1c, 0x4a, 0x63, 0x0d, 0x15}};
  d->SetClientGuid(kSaveGuid);
  const std::wstring type = T(gt::kFileType) + L" (*.c4tex)";
  const COMDLG_FILTERSPEC spec[] = {{type.c_str(), L"*.c4tex"}};
  d->SetFileTypes(1, spec);
  d->SetFileTypeIndex(1);
  d->SetDefaultExtension(L"c4tex");
  const std::wstring title = T(gt::kSaveTitle);
  d->SetTitle(title.c_str());
  d->SetFileName((L"CubeEvil4-textures-" + std::to_wstring(g.size) + L".c4tex").c_str());
  if (IShellItem *start = StartFolder()) {
    d->SetDefaultFolder(start);
    start->Release();
  }
  DWORD opts = 0;
  d->GetOptions(&opts);
  d->SetOptions(opts | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOREADONLYRETURN);
  bool ok = false;
  g.modal = true;
  if (SUCCEEDED(d->Show(g.hwnd))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(d->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        path = p;
        CoTaskMemFree(p);
        ok = true;
      }
      item->Release();
    }
  }
  g.modal = false;
  d->Release();
  return ok;
}

// Windows' Open dialog for a ZIP file or a folder
bool AskPack(bool folder, std::wstring &path) {
  IFileOpenDialog *d = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
  static const GUID kZipGuid = {0x1f0e2a7c, 0x8d33, 0x4b6e, {0xa1, 0x52, 0x6c, 0x0b, 0x9e, 0x47, 0x3d, 0x21}};
  static const GUID kFolderGuid = {0x7a4d1b90, 0x2c6f, 0x4e85, {0x8f, 0x13, 0x55, 0xd2, 0x0a, 0x9c, 0x61, 0x3e}};
  d->SetClientGuid(folder ? kFolderGuid : kZipGuid);
  DWORD opts = 0;
  d->GetOptions(&opts);
  const std::wstring zipType = T(gt::kKindZip) + L" (*.zip)";
  const COMDLG_FILTERSPEC spec[] = {{zipType.c_str(), L"*.zip"}};
  if (folder) {
    d->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  } else {
    d->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    d->SetFileTypes(1, spec);
  }
  std::wstring title = T(folder ? gt::kChooseFolder : gt::kChooseZip);
  while (!title.empty() && (title.back() == L'…' || title.back() == L' ')) title.pop_back();
  d->SetTitle(title.c_str());
  bool ok = false;
  g.modal = true;
  if (SUCCEEDED(d->Show(g.hwnd))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(d->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        path = p;
        CoTaskMemFree(p);
        ok = true;
      }
      item->Release();
    }
  }
  g.modal = false;
  d->Release();
  return ok;
}

bool Inside(const fs::path &file, const fs::path &folder) {
  std::error_code ec;
  const std::wstring f = Lower(fs::weakly_canonical(file, ec).wstring()),
                     d = Lower(fs::weakly_canonical(folder, ec).wstring());
  if (d.empty()) return false;
  const std::wstring prefix = d.back() == L'\\' ? d : d + L"\\";
  return f.rfind(prefix, 0) == 0;
}

void StartConverting() {
  if (!g.pack || g.worker.joinable()) return;
  std::wstring path;
  if (!AskSavePath(path)) return;
  const fs::path out(path);
  g.message.clear();
  if (!g.pack->zip && Inside(out, fs::path(g.pack->path))) {
    g.message = T8(gt::kInsidePack);
    Layout();
    return;
  }
  std::uint64_t source = 0;
  const auto est = Estimates(&source);
  const int k = SizeIndex();
  std::error_code ec;
  const fs::space_info sp = fs::space(out.parent_path(), ec);
  const std::uint64_t free = ec ? ~0ull : sp.available;
  if (!ec && free < est[k] + kReserve) {
    std::string msg = gt::Format(T8(gt::kNoSpace), {Size(est[k] + kReserve), Size(free)});
    for (int i = k - 1; i >= 0; i--)
      if (free >= est[i] + kReserve) {
        msg += " " + gt::Format(T8(gt::kSmallerFits), {std::to_string(kSizes[i])});
        break;
      }
    msg += " " + T8(gt::kChooseOther);
    g.message = msg;
    Layout();
    return;
  }
  const std::wstring part = path + L".part";
  FILE *f = _wfopen(part.c_str(), L"wb");
  if (!f) {
    g.message = gt::Format(T8(gt::kCannotWrite), {SystemError(_doserrno ? static_cast<DWORD>(_doserrno) : ERROR_ACCESS_DENIED)});
    Layout();
    return;
  }
  pack::Settings s;
  s.size = g.size;
  s.effort = 1;
  s.threads = 0;
  s.memoryBudget = pc::MemoryBudget();
  s.date = pc::Today();
  s.outName = U8(out.filename().wstring());
  std::string choices;
  std::vector<const pack::Source *> todo = pack::Todo(*g.pack->pack, s, choices);
  g.cancel = false;
  g.cancelling = false;
  g.shown = pack::Progress{};
  g.shown.total = todo.size();
  {
    std::lock_guard<std::mutex> lock(g.mu);
    g.progress = g.shown;
    g.posted = false;
  }
  g.outPath = path;
  g.outFree = free;
  g.outNeed = est[k];
  g.convStart = std::chrono::steady_clock::now();
  g.convEstimate = EstimateSeconds(source, est[k]);
  g.done.reset();
  SetTaskbar(TBPF_NORMAL, 0, todo.size());
  g.worker = std::thread(ConvThread, f, path, part, std::move(todo), std::move(choices), s);
  Go(Page::kConverting);
}

bool AskStop() {
  TASKDIALOGCONFIG c{};
  c.cbSize = sizeof(c);
  c.hwndParent = g.hwnd;
  c.hInstance = g.inst;
  c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  const std::wstring main = T(gt::kCancelAsk), text = T(gt::kCancelAskText), stop = T(gt::kStop), keep = T(gt::kKeepGoing);
  const TASKDIALOG_BUTTON buttons[] = {{IDYES, stop.c_str()}, {IDNO, keep.c_str()}};
  c.pszWindowTitle = kTitle;
  c.pszMainInstruction = main.c_str();
  c.pszContent = text.c_str();
  c.cButtons = 2;
  c.pButtons = buttons;
  c.nDefaultButton = IDNO;
  c.pszMainIcon = TD_WARNING_ICON;
  int pressed = IDNO;
  g.modal = true;
  const HRESULT hr = TaskDialogIndirect(&c, &pressed, nullptr, nullptr);
  g.modal = false;
  return SUCCEEDED(hr) && pressed == IDYES;
}

void RequestCancel() {
  if (g.page != Page::kConverting || g.cancelling || !g.worker.joinable()) return;
  if (!AskStop()) return;
  if (g.page != Page::kConverting || !g.worker.joinable()) return;  // it ended while asking
  g.cancel = true;
  g.cancelling = true;
  if (Button *b = Find(kIdCancel)) EnableWindow(b->h, FALSE);
  SetTaskbar(TBPF_PAUSED, g.shown.done, g.shown.total);
  Layout();
}

void OpenFolder() {
  if (!g.done) return;
  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(g.done->out.c_str());
  if (pidl) {
    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
  }
}

void ShowReport() {
  if (g.done && !g.done->report.empty())
    ShellExecuteW(g.hwnd, L"open", g.done->report.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------- drag and drop ----------------

class DropTarget : public IDropTarget {
 public:
  DropTarget() {
    CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper_));
  }
  ~DropTarget() {
    if (helper_) helper_->Release();
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *out = static_cast<IDropTarget *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return static_cast<ULONG>(n);
  }
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data, DWORD, POINTL pt, DWORD *effect) override {
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    files_ = data && data->QueryGetData(&fmt) == S_OK;
    *effect = Accepting() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (helper_) {
      POINT p{pt.x, pt.y};
      helper_->DragEnter(g.hwnd, data, &p, *effect);
    }
    SetOver(*effect != DROPEFFECT_NONE);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD *effect) override {
    *effect = Accepting() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (helper_) {
      POINT p{pt.x, pt.y};
      helper_->DragOver(&p, *effect);
    }
    SetOver(*effect != DROPEFFECT_NONE);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() override {
    if (helper_) helper_->DragLeave();
    SetOver(false);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Drop(IDataObject *data, DWORD, POINTL pt, DWORD *effect) override {
    if (helper_) {
      POINT p{pt.x, pt.y};
      helper_->Drop(data, &p, *effect);
    }
    SetOver(false);
    *effect = DROPEFFECT_NONE;
    if (!Accepting() || !data) return S_OK;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&fmt, &medium))) return S_OK;
    std::vector<std::wstring> paths;
    if (HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
      const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < n; i++) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring p(len, L'\0');
        DragQueryFileW(drop, i, p.data(), len + 1);
        paths.push_back(p);
      }
      GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    *effect = DROPEFFECT_COPY;
    SetForegroundWindow(g.hwnd);
    if (paths.size() == 1) {
      // after this call returns: the sending program (Explorer) waits for Drop
      auto *copy = new std::wstring(paths[0]);
      PostMessageW(g.hwnd, kMsgLoad, 0, reinterpret_cast<LPARAM>(copy));
    } else if (!paths.empty()) {
      g.message = T8(gt::kOneAtATime);
      if (g.page != Page::kStart) Go(Page::kStart);
      else Layout();
    }
    return S_OK;
  }

 private:
  bool Accepting() const {
    return files_ && !g.reading && !g.modal && g.page != Page::kConverting && !g.worker.joinable();
  }
  void SetOver(bool over) {
    if (g.dragOver == over) return;
    g.dragOver = over;
    Layout();
  }
  LONG ref_ = 1;
  bool files_ = false;
  IDropTargetHelper *helper_ = nullptr;
};

// ---------------- the windows ----------------

void OnCommand(int id) {
  if (id == IDOK) {
    // Enter: the focused button, else the page's main step
    HWND f = GetFocus();
    for (auto &b : g.buttons)
      if (b->h == f) {
        OnCommand(b->id);
        return;
      }
    for (auto &b : g.buttons)
      if (b->style == Style::kPrimary && IsWindowEnabled(b->h)) {
        OnCommand(b->id);
        return;
      }
    return;
  }
  if (id == IDCANCEL) {
    if (g.page == Page::kConverting) RequestCancel();
    return;
  }
  switch (id) {
    case kIdZip:
    case kIdFolder: {
      std::wstring path;
      // loaded after this click is handled: loading rebuilds the page and its buttons
      if (AskPack(id == kIdFolder, path)) PostMessageW(g.hwnd, kMsgLoad, 0, reinterpret_cast<LPARAM>(new std::wstring(path)));
      return;
    }
    case kIdOther:
      g.pack.reset();
      g.message.clear();
      Go(Page::kStart);
      return;
    case kIdStart: StartConverting(); return;
    case kIdCancel: RequestCancel(); return;
    case kIdClose: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); return;
    case kIdBack:
      g.done.reset();
      g.message.clear();
      SetTaskbar(TBPF_NOPROGRESS);
      Go(g.pack ? Page::kOptions : Page::kStart);
      return;
    case kIdOpen: OpenFolder(); return;
    case kIdReport: ShowReport(); return;
    default: break;
  }
  if (id >= kIdSize && id < kIdSize + 3) {
    g.size = kSizes[id - kIdSize];
    g.message.clear();
    for (int i = 0; i < 3; i++)
      if (Button *b = Find(kIdSize + i)) {
        b->selected = kSizes[i] == g.size;
        InvalidateRect(b->h, nullptr, FALSE);
      }
    Layout();
    return;
  }
  if (g.pack && id >= kIdChoice && id < kIdAddOn) {
    const int gi = (id - kIdChoice) / 64, v = (id - kIdChoice) % 64 - 1;
    auto &groups = g.pack->pack->groups;
    if (gi < static_cast<int>(groups.size())) {
      groups[static_cast<std::size_t>(gi)].chosen = v;
      for (int k = -1; k < static_cast<int>(groups[static_cast<std::size_t>(gi)].variants.size()); k++)
        if (Button *b = Find(kIdChoice + gi * 64 + k + 1)) {
          b->selected = k == v;
          InvalidateRect(b->h, nullptr, FALSE);
        }
      g.message.clear();
      Layout();
    }
    return;
  }
  if (g.pack && id >= kIdAddOn) {
    const int gi = id - kIdAddOn;
    auto &groups = g.pack->pack->groups;
    if (gi < static_cast<int>(groups.size())) {
      pack::Group &grp = groups[static_cast<std::size_t>(gi)];
      grp.chosen = grp.chosen == 0 ? -1 : 0;
      if (Button *b = Find(id)) {
        b->selected = grp.chosen == 0;
        InvalidateRect(b->h, nullptr, FALSE);
      }
      g.message.clear();
      Layout();
    }
  }
}

LRESULT CALLBACK ContentProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      Paint(dc, ps.rcPaint, g.ops, g.c.bg, g.scroll);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_PRINTCLIENT: {
      RECT r;
      GetClientRect(h, &r);
      Paint(reinterpret_cast<HDC>(w), r, g.ops, g.c.bg, g.scroll);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: Layout(); return 0;
    case WM_DRAWITEM: DrawButton(reinterpret_cast<const DRAWITEMSTRUCT *>(l)); return TRUE;
    case WM_COMMAND:
      if (HIWORD(w) == BN_CLICKED || HIWORD(w) == BN_DOUBLECLICKED) OnCommand(LOWORD(w));
      return 0;
    case WM_MOUSEWHEEL:
      ScrollTo(g.scroll - GET_WHEEL_DELTA_WPARAM(w) * S(56) / WHEEL_DELTA);
      return 0;
    case WM_VSCROLL: {
      SCROLLINFO si{sizeof(si), SIF_ALL};
      GetScrollInfo(h, SB_VERT, &si);
      int y = g.scroll;
      switch (LOWORD(w)) {
        case SB_LINEUP: y -= S(40); break;
        case SB_LINEDOWN: y += S(40); break;
        case SB_PAGEUP: y -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: y += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: y = si.nTrackPos; break;
        case SB_TOP: y = 0; break;
        case SB_BOTTOM: y = g.maxScroll; break;
        default: break;
      }
      ScrollTo(y);
      return 0;
    }
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

void OnReadDone(ReadOutcome *raw) {
  std::unique_ptr<ReadOutcome> r(raw);
  if (g.worker.joinable()) g.worker.join();
  g.reading = false;
  SetTaskbar(TBPF_NOPROGRESS);
  if (g.closeAfter) {
    DestroyWindow(g.hwnd);
    return;
  }
  if (!r->data) {
    g.message = r->error;
    g.page = Page::kStart;
  } else {
    g.pack = std::move(r->data);
    g.message.clear();
    g.page = Page::kOptions;
  }
  Rebuild();
  if (GetForegroundWindow() != g.hwnd) {
    FLASHWINFO fi{sizeof(fi), g.hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
    FlashWindowEx(&fi);
  }
}

void OnConvDone(ConvOutcome *raw) {
  std::unique_ptr<ConvOutcome> r(raw);
  if (g.worker.joinable()) g.worker.join();
  if (g.closeAfter) {
    DestroyWindow(g.hwnd);
    return;
  }
  g.cancelling = false;
  g.done = std::move(r);
  if (g.done->ok) {
    SetTaskbar(TBPF_NOPROGRESS);
    MessageBeep(MB_OK);
  } else {
    SetTaskbar(g.done->cancelled ? TBPF_NOPROGRESS : TBPF_ERROR, 1, 1);
    if (!g.done->cancelled) MessageBeep(MB_ICONHAND);
  }
  if (GetForegroundWindow() != g.hwnd) {
    FLASHWINFO fi{sizeof(fi), g.hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
    FlashWindowEx(&fi);
  }
  g.page = Page::kDone;
  Rebuild();
}

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (g.taskbarCreated && m == g.taskbarCreated) {
    if (!g.taskbar && SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g.taskbar))))
      if (FAILED(g.taskbar->HrInit())) {
        g.taskbar->Release();
        g.taskbar = nullptr;
      }
    return 0;
  }
  switch (m) {
    case WM_CREATE: {
      g.hwnd = h;
      g.content = CreateWindowExW(WS_EX_CONTROLPARENT, kContentClass, L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10, h, nullptr,
                                  g.inst, nullptr);
      return 0;
    }
    case WM_SIZE: {
      const int W = LOWORD(l), H = HIWORD(l);
      if (g.content) MoveWindow(g.content, 0, 0, W, std::max(0, H - BarHeight()), TRUE);
      Layout();
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto *mm = reinterpret_cast<MINMAXINFO *>(l);
      mm->ptMinTrackSize.x = S(560);
      mm->ptMinTrackSize.y = S(520);
      return 0;
    }
    case WM_DPICHANGED: {
      g.dpi = HIWORD(w);
      MakeFonts();
      const RECT *r = reinterpret_cast<const RECT *>(l);
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      Layout();
      return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      ChooseColors();
      ApplyThemeToWindows();
      Layout();
      break;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      Paint(dc, ps.rcPaint, g.barOps, g.c.bg, 0);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_PRINTCLIENT: {
      RECT r;
      GetClientRect(h, &r);
      Paint(reinterpret_cast<HDC>(w), r, g.barOps, g.c.bg, 0);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: DrawButton(reinterpret_cast<const DRAWITEMSTRUCT *>(l)); return TRUE;
    case WM_COMMAND:
      if (HIWORD(w) == BN_CLICKED || HIWORD(w) == BN_DOUBLECLICKED || LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL)
        OnCommand(LOWORD(w));
      return 0;
    case WM_MOUSEWHEEL: return SendMessageW(g.content, m, w, l);
    case WM_TIMER:
      if (w == kTimerTestDrop) {
        // -testdrop: the mouse button let go over the drop area
        KillTimer(h, kTimerTestDrop);
        if (g.testData && g.drop) {
          RECT r;
          GetWindowRect(h, &r);
          const POINTL pt{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
          DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
          g.drop->DragOver(MK_LBUTTON, pt, &effect);
          effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
          g.drop->Drop(g.testData, 0, pt, &effect);
          g.testData->Release();
          g.testData = nullptr;
        }
        return 0;
      }
      if (w == kTimerAnim) {
        g.anim += 0.033f;
        // the numbers ten times a second, the turning parts in between
        if (g.reading || (g.page == Page::kConverting && ++g.ticks % 3 == 0)) {
          Layout();
        } else if (!IsRectEmpty(&g.animRect)) {
          RECT r = g.animRect;
          OffsetRect(&r, 0, -g.scroll);
          InflateRect(&r, 2, 2);
          InvalidateRect(g.content, &r, FALSE);
        }
      }
      return 0;
    case kMsgRebuild: Rebuild(); return 0;
    case kMsgLoad: {
      std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring *>(l));
      Load(*path);
      return 0;
    }
    case kMsgReadProgress: {
      std::lock_guard<std::mutex> lock(g.mu);
      g.posted = false;
      return 0;  // the timer shows it
    }
    case kMsgReadDone: OnReadDone(reinterpret_cast<ReadOutcome *>(l)); return 0;
    case kMsgConvProgress: {
      {
        std::lock_guard<std::mutex> lock(g.mu);
        g.shown = g.progress;
        g.posted = false;
      }
      if (g.page == Page::kConverting) {
        const int percent = g.shown.total ? static_cast<int>(g.shown.done * 100 / g.shown.total) : 0;
        if (percent != g.percent) {
          g.percent = percent;
          SetWindowTextW(h, (TF(gt::kPercent, {std::to_string(percent)}) + L" – " + kTitle).c_str());
        }
        if (!g.cancelling) SetTaskbar(TBPF_NORMAL, g.shown.done, g.shown.total);
        if (g.shown.done >= g.shown.total)
          if (Button *b = Find(kIdCancel)) EnableWindow(b->h, FALSE);
      }
      return 0;
    }
    case kMsgConvDone: OnConvDone(reinterpret_cast<ConvOutcome *>(l)); return 0;
    case WM_CLOSE:
      if (g.modal) return 0;  // a dialog of ours is open (closed from the taskbar): it answers first
      if (g.page == Page::kConverting && g.worker.joinable()) {
        if (!g.cancelling) {
          if (!AskStop()) return 0;
          if (!g.worker.joinable()) {
            DestroyWindow(h);
            return 0;
          }
          g.cancel = true;
          g.cancelling = true;
          Layout();
        }
        g.closeAfter = true;
        return 0;
      }
      if (g.worker.joinable()) {  // reading: close when it is done
        g.closeAfter = true;
        ShowWindow(h, SW_HIDE);
        return 0;
      }
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      KillTimer(h, kTimerAnim);
      RevokeDragDrop(h);
      PostQuitMessage(0);
      return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

// command-line options the window does not take: point to the console version
void ConsoleHint() {
  const std::wstring text = T(gt::kConsoleHint);
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) {
    const std::string s = U8(text) + "\n";
    DWORD written = 0;
    WriteFile(out, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
    return;
  }
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    HANDLE con = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (con != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      const std::wstring line = L"\n" + text + L"\n";
      WriteConsoleW(con, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
      CloseHandle(con);
      FreeConsole();
      return;
    }
    FreeConsole();
  }
  MessageBoxW(nullptr, text.c_str(), kTitle, MB_OK | MB_ICONINFORMATION);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  g.inst = inst;
  // per-monitor DPI (also in the manifest; this is for when the manifest is not used)
  {
    using Fn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    if (auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
      fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }
  // the command line: a pack (dropped onto the program's icon), -theme light|dark; for the tests
  // -testdrop <pack>: hands the pack to the window's drop target as Explorer does when it is dragged in (the same data
  // object), the mouse button let go after 1.5 seconds; -testsave <file.c4tex>: that file instead of the Save dialog
  std::wstring startPath, testDrop;
  bool unknown = false;
  {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
      const std::wstring a = Lower(argv[i]);
      if ((a == L"-testdrop" || a == L"--testdrop") && i + 1 < argc) testDrop = argv[++i];
      else if ((a == L"-testsave" || a == L"--testsave") && i + 1 < argc) g.testSave = argv[++i];
      else if ((a == L"-theme" || a == L"--theme") && i + 1 < argc) {
        const std::wstring t = Lower(argv[++i]);
        g.forceTheme = t == L"dark" ? 1 : t == L"light" ? 0 : -1;
      } else if (!a.empty() && a[0] != L'-' && a[0] != L'/' && startPath.empty()) startPath = argv[i];
      else unknown = true;
    }
    if (argv) LocalFree(argv);
  }
  if (unknown) {
    ConsoleHint();
    return 2;
  }
  if (FAILED(OleInitialize(nullptr))) return 1;
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  Gdiplus::GdiplusStartupInput gsi;
  ULONG_PTR gdiplus = 0;
  Gdiplus::GdiplusStartup(&gdiplus, &gsi, nullptr);
  g.measure = CreateCompatibleDC(nullptr);
  g.taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
  ChooseFaces();
  ChooseColors();

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = ContentProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kContentClass;
  RegisterClassExW(&wc);
  wc.lpfnWndProc = MainProc;
  wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                           GetSystemMetrics(SM_CYICON), 0));
  wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), 0));
  wc.lpszClassName = kMainClass;
  RegisterClassExW(&wc);

  const DWORD exStyle = 0;
  HWND h = CreateWindowExW(exStyle, kMainClass, kTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                           800, 760, nullptr, nullptr, inst, nullptr);
  if (!h) return 1;
  g.dpi = WindowDpi(h);
  MakeFonts();
  ApplyThemeToWindows();
  // 780 x 840 DIP, centred on its screen, within the work area
  {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT wa = mi.rcWork;
    RECT r{0, 0, S(780), S(840)};
    using Adjust = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust = reinterpret_cast<Adjust>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (adjust) adjust(&r, WS_OVERLAPPEDWINDOW, FALSE, exStyle, g.dpi);
    else AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, exStyle);
    const int ww = std::min<int>(r.right - r.left, wa.right - wa.left), wh = std::min<int>(r.bottom - r.top, wa.bottom - wa.top);
    SetWindowPos(h, nullptr, wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  g.drop = new DropTarget();
  RegisterDragDrop(h, g.drop);
  Rebuild();
  ShowWindow(h, show);
  UpdateWindow(h);
  if (Button *b = Find(kIdZip)) SetFocus(b->h);
  if (!startPath.empty()) Load(startPath);
  if (!testDrop.empty()) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(testDrop.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
      item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&g.testData));
      item->Release();
    }
    if (g.testData) {
      RECT r;
      GetWindowRect(h, &r);
      const POINTL pt{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
      DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
      g.drop->DragEnter(g.testData, MK_LBUTTON, pt, &effect);
      SetTimer(h, kTimerTestDrop, 1500, nullptr);
    }
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (g.hwnd && IsDialogMessageW(g.hwnd, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (g.worker.joinable()) {
    g.cancel = true;
    g.worker.join();
  }
  if (g.taskbar) g.taskbar->Release();
  if (g.testData) g.testData->Release();
  if (g.drop) g.drop->Release();
  g.pack.reset();
  for (HFONT f : g.font)
    if (f) DeleteObject(f);
  if (g.icon) DestroyIcon(g.icon);
  DeleteDC(g.measure);
  Gdiplus::GdiplusShutdown(gdiplus);
  OleUninitialize();
  return 0;
}
