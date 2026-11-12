// HiDef Radio for Windows: the window. Win32 + Direct2D/DirectWrite, drawn the way the
// Android app's tablet screen looks: the VFD frequency, HD1-HD8, favorites, now playing with
// album art, and a right-hand pane with the signal details or the traffic/weather maps.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objbase.h>
#include <d2d1.h>
#include <shobjidl.h>
#include <dwrite_3.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../res/resource.h"
#include "engine.hpp"
#include "imaging.hpp"
#include "radio.hpp"
#include "recorder.hpp"
#include "rtl-sdr.h"
extern "C" {
#include "nrsc5.h"
}
#include "sigtext.hpp"
#include "stations.hpp"
#include "store.hpp"
#include "util.hpp"

extern "C" void hidef_log_open(const char *path);
extern "C" int __android_log_print(int, const char *, const char *, ...);
#define ULOG(...) __android_log_print(4, "Window", __VA_ARGS__)

#define HIDEF_VERSION "1.0.1"

namespace {

// ------------------------------------------------------------------ colors (Android colors.xml)

D2D1_COLOR_F rgb(uint32_t c, float a = 1.0f) {
    return D2D1::ColorF(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, a);
}
const uint32_t C_BG = 0x000000, C_TEXT = 0xECECEC, C_DIM = 0x9AA0A6, C_SURFACE = 0x121414;
const uint32_t C_VFD_ON = 0x4DF5D2, C_VFD_PANEL = 0x070D0C, C_VFD_EDGE = 0x1C2826;
const uint32_t C_BUTTON = 0xBB86FC, C_BUTTON_TEXT = 0x000000;
const uint32_t C_GOOD = 0x3DDC84, C_HOLDING = 0xFFB300, C_LOST = 0xFF5252, C_OFF = 0x808080;
const uint32_t C_STAR = 0xFFC107, C_TILE_EDGE = 0x808080;

uint32_t signalColor(sigtext::Color c) {
    switch (c) {
    case sigtext::GOOD: return C_GOOD;
    case sigtext::HOLDING: return C_HOLDING;
    case sigtext::LOST: return C_LOST;
    default: return C_OFF;
    }
}

// ------------------------------------------------------------------ hit areas

enum HitId {
    H_NONE, H_TUNE_DOWN, H_TUNE_UP, H_VFD, H_PAUSE, H_POWER, H_RECORD, H_SETTINGS, H_VOLUME, H_PROGRAM, H_PRESET,
    H_STAR, H_SIGNAL, H_OTHER_HD, H_DETAILS_HEAD, H_TAB_TRAFFIC, H_TAB_WEATHER, H_MAP, H_PLAY, H_SLIDER,
    H_NOW_IMAGE, H_START, H_DETAILS
};
struct Hit {
    D2D1_RECT_F r;
    HitId id;
    int arg;
};
bool inside(const D2D1_RECT_F &r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// A Direct2D picture made from WIC pixels, remembered by what it was made from.
struct Pic {
    std::string key;
    ID2D1Bitmap *bmp = nullptr;
    float w = 0, h = 0;
    void reset() {
        if (bmp) bmp->Release();
        bmp = nullptr;
        key.clear();
        w = h = 0;
    }
};

// ------------------------------------------------------------------ the app

struct App {
    HWND hwnd = nullptr;
    float dpi = 96;
    ID2D1Factory *d2d = nullptr;
    IDWriteFactory *dw = nullptr;
    IDWriteFontCollection *vfdFonts = nullptr;
    std::wstring vfdFamily = L"Consolas";
    ID2D1HwndRenderTarget *rt = nullptr;
    ID2D1SolidColorBrush *brush = nullptr;
    std::map<std::wstring, IDWriteTextFormat *> formats;

    Settings cfg;
    std::unique_ptr<Radio> radio;
    std::vector<Hit> hits;
    Hit pressed{{0, 0, 0, 0}, H_NONE, 0};
    bool dragging = false;

    // what the screen shows
    sigtext::Weak weak;
    Sig shown;
    std::vector<Program> programs;
    int program = 0;
    std::string picturesKey;
    std::string artBytes, logoBytes;       // album art (may be empty), the program's logo
    Pic nowPic, smallLogo, trafficPic;
    std::map<std::string, Pic> radarPics;  // by frame
    std::map<std::string, Pic> presetPics; // by "freq/program/version"
    long long trafficTimeMs = 0;
    int mapGen = -1;

    bool mapFull = false;                  // a map fills the window (double-click)
    int radarFrame = 0;
    bool radarPlaying = false;
    long long radarNextMs = 0;
    float detailsScroll = 0, detailsMaxScroll = 0;
    D2D1_RECT_F detailsArea{};
    bool winFull = false;                  // F11: the whole screen, no frame (car use)
    std::string toast;                     // a short message at the bottom ("Saved 3.2 MB: ...")
    long long toastUntilMs = 0;
    WINDOWPLACEMENT prevPlacement{sizeof(WINDOWPLACEMENT)};
    long long lastSaveMs = 0;
};
App app;

// ------------------------------------------------------------------ Direct2D helpers

IDWriteTextFormat *format(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, bool vfd = false) {
    std::wstring key = std::to_wstring((int)(size * 10)) + L"/" + std::to_wstring((int)weight) + (vfd ? L"/v" : L"");
    auto it = app.formats.find(key);
    if (it != app.formats.end()) return it->second;
    IDWriteTextFormat *f = nullptr;
    app.dw->CreateTextFormat(vfd ? app.vfdFamily.c_str() : L"Segoe UI", vfd ? app.vfdFonts : nullptr, weight,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    app.formats[key] = f;
    return f;
}

void setColor(uint32_t c, float a = 1.0f) { app.brush->SetColor(rgb(c, a)); }

void fill(const D2D1_RECT_F &r, uint32_t c, float a = 1.0f, float radius = 0) {
    setColor(c, a);
    if (radius > 0) app.rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), app.brush);
    else app.rt->FillRectangle(r, app.brush);
}
void stroke(const D2D1_RECT_F &r, uint32_t c, float a, float radius, float width) {
    setColor(c, a);
    app.rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), app.brush, width);
}

enum Align { LEFT, CENTER, RIGHT };

IDWriteTextLayout *layout(const std::string &text, IDWriteTextFormat *f, float w, float h, Align align, bool wrap) {
    std::wstring t = widen(text);
    IDWriteTextLayout *l = nullptr;
    app.dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, std::max(w, 1.0f), std::max(h, 1.0f), &l);
    if (!l) return nullptr;
    l->SetTextAlignment(align == CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER : align == RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                                                      : DWRITE_TEXT_ALIGNMENT_LEADING);
    l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap) {   // a line that doesn't fit ends in "..."
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        IDWriteInlineObject *dots = nullptr;
        app.dw->CreateEllipsisTrimmingSign(f, &dots);
        l->SetTrimming(&trim, dots);
        if (dots) dots->Release();
    }
    return l;
}

// Draws text; returns its height. vcenter: centered in the rectangle's height.
float text(const std::string &s, const D2D1_RECT_F &r, IDWriteTextFormat *f, uint32_t c, float a = 1.0f,
           Align align = LEFT, bool wrap = false, bool vcenter = false) {
    if (s.empty()) return 0;
    IDWriteTextLayout *l = layout(s, f, r.right - r.left, r.bottom - r.top, align, wrap);
    if (!l) return 0;
    if (vcenter) l->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    setColor(c, a);
    app.rt->DrawTextLayout(D2D1::Point2F(r.left, r.top), l, app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    l->Release();
    return m.height;
}
float textHeight(const std::string &s, IDWriteTextFormat *f, float w) {
    IDWriteTextLayout *l = layout(s.empty() ? " " : s, f, w, 10000, LEFT, true);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    l->Release();
    return m.height;
}
float textWidth(const std::string &s, IDWriteTextFormat *f) {
    IDWriteTextLayout *l = layout(s, f, 10000, 1000, LEFT, false);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    l->Release();
    return m.widthIncludingTrailingWhitespace;
}

void addHit(const D2D1_RECT_F &r, HitId id, int arg = 0) { app.hits.push_back(Hit{r, id, arg}); }

void button(const D2D1_RECT_F &r, const std::string &label, HitId id, int arg = 0, float size = 20,
            uint32_t bg = C_BUTTON, uint32_t fg = C_BUTTON_TEXT, float bgAlpha = 1.0f) {
    fill(r, bg, bgAlpha, 12);
    text(label, r, format(size, DWRITE_FONT_WEIGHT_SEMI_BOLD), fg, 1.0f, CENTER, false, true);
    addHit(r, id, arg);
}

// WIC pixels -> a Direct2D bitmap (premultiplied alpha).
bool makePic(Pic &p, const Image &im, const std::string &key) {
    p.reset();
    if (im.empty() || !app.rt) return false;
    std::vector<uint32_t> pm(im.px.size());
    for (size_t i = 0; i < im.px.size(); i++) {
        uint32_t v = im.px[i], a = v >> 24;
        if (a == 255) pm[i] = v;
        else pm[i] = a << 24 | ((((v >> 16) & 255) * a / 255) << 16) | ((((v >> 8) & 255) * a / 255) << 8) | ((v & 255) * a / 255);
    }
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(app.rt->CreateBitmap(D2D1::SizeU(im.w, im.h), pm.data(), im.w * 4, props, &p.bmp))) return false;
    p.key = key;
    p.w = (float)im.w;
    p.h = (float)im.h;
    return true;
}
bool makePic(Pic &p, const std::string &bytes, const std::string &key) {
    Image im;
    if (bytes.empty() || !img::decode(bytes, im)) { p.reset(); p.key = key; return false; }
    // album art can be big; nothing on screen needs more than 1200 px
    if (im.w > 1200 || im.h > 1200) {
        float s = 1200.0f / std::max(im.w, im.h);
        im = img::scale(im, std::max(1, (int)(im.w * s)), std::max(1, (int)(im.h * s)));
    }
    return makePic(p, im, key);
}

// Draws a picture fitted into r (kept in proportion, centered); returns where it went.
D2D1_RECT_F drawFit(const Pic &p, const D2D1_RECT_F &r) {
    if (!p.bmp || p.w <= 0 || p.h <= 0) return D2D1::RectF(0, 0, 0, 0);
    float rw = r.right - r.left, rh = r.bottom - r.top;
    float s = std::min(rw / p.w, rh / p.h);
    float w = p.w * s, h = p.h * s;
    D2D1_RECT_F d = D2D1::RectF(r.left + (rw - w) / 2, r.top + (rh - h) / 2, r.left + (rw - w) / 2 + w, r.top + (rh - h) / 2 + h);
    app.rt->DrawBitmap(p.bmp, d, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return d;
}

void discardDevice() {
    app.nowPic.reset();
    app.smallLogo.reset();
    app.trafficPic.reset();
    for (auto &kv : app.radarPics) kv.second.reset();
    app.radarPics.clear();
    for (auto &kv : app.presetPics) kv.second.reset();
    app.presetPics.clear();
    app.picturesKey.clear();
    app.mapGen = -1;
    if (app.brush) app.brush->Release();
    app.brush = nullptr;
    if (app.rt) app.rt->Release();
    app.rt = nullptr;
}

bool ensureDevice() {
    if (app.rt) return true;
    RECT rc;
    GetClientRect(app.hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = app.dpi;
    if (FAILED(app.d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(app.hwnd, D2D1::SizeU(rc.right, rc.bottom)), &app.rt)))
        return false;
    app.rt->CreateSolidColorBrush(rgb(C_TEXT), &app.brush);
    app.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

// The DSEG7 font (the VFD digits) from data\fonts, without installing it.
void loadVfdFont() {
    IDWriteFactory5 *f5 = nullptr;
    if (FAILED(app.dw->QueryInterface(__uuidof(IDWriteFactory5), (void **)&f5))) return;
    IDWriteFontSetBuilder1 *builder = nullptr;
    IDWriteFontFile *file = nullptr;
    IDWriteFontSet *set = nullptr;
    IDWriteFontCollection1 *coll = nullptr;
    std::wstring path = dataDir() + L"\\fonts\\DSEG7Classic-Bold.ttf";
    if (SUCCEEDED(f5->CreateFontSetBuilder(&builder)) && SUCCEEDED(f5->CreateFontFileReference(path.c_str(), nullptr, &file)) &&
        SUCCEEDED(builder->AddFontFile(file)) && SUCCEEDED(builder->CreateFontSet(&set)) &&
        SUCCEEDED(f5->CreateFontCollectionFromFontSet(set, &coll))) {
        IDWriteFontFamily1 *fam = nullptr;
        IDWriteLocalizedStrings *names = nullptr;
        wchar_t name[100] = L"";
        if (coll->GetFontFamilyCount() > 0 && SUCCEEDED(coll->GetFontFamily(0, &fam)) &&
            SUCCEEDED(fam->GetFamilyNames(&names)) && SUCCEEDED(names->GetString(0, name, 100))) {
            app.vfdFamily = name;
            app.vfdFonts = coll;
            coll = nullptr;
        }
        if (names) names->Release();
        if (fam) fam->Release();
    }
    if (coll) coll->Release();
    if (set) set->Release();
    if (file) file->Release();
    if (builder) builder->Release();
    f5->Release();
}

// ------------------------------------------------------------------ what's on the air

bool running() { return app.radio && app.radio->on(); }

std::string stationName(int freqHz) {
    const Sig &s = app.shown;
    bool otherAnalog = running() && s.otherHd() && s.analog();
    std::string n;
    if (otherAnalog) {
        n = s.rdsName();
        if (n.empty()) n = trim(s.str("hdSameCall"));
    } else {
        n = s.station();
        if (n.empty()) n = s.rdsName();
    }
    return n.empty() ? band::bandText(freqHz) : n;
}

const Program *selectedProgram() {
    for (const Program &p : app.programs) if (p.number == app.program) return &p;
    return nullptr;
}

// The pictures: album art, else the program's logo, else the logo saved from last time.
void updatePictures(int freqHz) {
    const Sig &s = app.shown;
    bool on = running();
    bool otherAnalog = on && s.otherHd() && s.analog();
    bool savedFits = !on || logocache::fits(freqHz, s.station(), s.analog(), trim(s.str("rdsPi")));
    std::string key = fmt("%d/%d/%d/%d/%d/%d/%d/%d", on ? app.radio->pictureGen() : -1, app.program, freqHz, otherAnalog,
                          savedFits, logocache::version(), artblock::version(), on);
    if (key == app.picturesKey) return;
    app.picturesKey = key;
    app.artBytes = otherAnalog || !on ? "" : app.radio->art(app.program);
    app.logoBytes.clear();
    if (!otherAnalog) {
        if (on) app.logoBytes = app.radio->logo(app.program);
        if (app.logoBytes.empty() && savedFits) app.logoBytes = logocache::load(freqHz, app.program);
    }
    const std::string &big = !app.artBytes.empty() ? app.artBytes : app.logoBytes;
    makePic(app.nowPic, big, key);
    if (!app.artBytes.empty() && !app.logoBytes.empty()) makePic(app.smallLogo, app.logoBytes, key);
    else app.smallLogo.reset();
}

// ------------------------------------------------------------------ painting

void drawVfd(const D2D1_RECT_F &r, int freqHz) {
    fill(r, C_VFD_PANEL, 1, 16);
    stroke(r, C_VFD_EDGE, 1, 16, 2);
    addHit(r, H_VFD);
    float h = r.bottom - r.top;
    bool am = band::isAm(freqHz);
    std::string f = band::freqText(freqHz);
    // DSEG: '!' is a blank as wide as a digit, so the ghost "888.8" sits under every digit.
    std::string intPart = f.substr(0, f.find('.'));
    std::string shown = f, ghost;
    int pad = (am ? 4 : 3) - (int)intPart.size();
    for (int i = 0; i < pad; i++) shown = "!" + shown;
    for (char c : shown) ghost += c == '.' ? '.' : '8';
    float unitW = h * 0.95f;
    D2D1_RECT_F digits = D2D1::RectF(r.left + 12, r.top, r.right - unitW - 8, r.bottom);
    IDWriteTextFormat *vf = format(h * 0.62f, DWRITE_FONT_WEIGHT_BOLD, true);
    text(ghost, digits, vf, C_VFD_ON, 0.12f, RIGHT, false, true);
    for (float dx : {-1.5f, 1.5f}) text(shown, D2D1::RectF(digits.left + dx, digits.top, digits.right + dx, digits.bottom), vf, C_VFD_ON, 0.25f, RIGHT, false, true);
    text(shown, digits, vf, C_VFD_ON, 1.0f, RIGHT, false, true);
    // the little "HD" lamp (lit while synced) with the stereo dot before it, and the unit
    bool hd = running() && app.shown.synced();
    const Sig &s = app.shown;
    bool stereo = running() && (s.analog() ? (s.am() ? s.l("amStereoPct", 0) >= 50 : s.l("fmStereoPct", 0) >= 50)
                                           : (s.synced() && s.b("hdStereo")));
    D2D1_RECT_F side = D2D1::RectF(r.right - unitW - 4, r.top + h * 0.14f, r.right - 8, r.top + h * 0.5f);
    text(std::string(stereo ? "\xE2\x97\x8F " : "") + "HD", side, format(h * 0.24f, DWRITE_FONT_WEIGHT_BOLD), C_VFD_ON, hd ? 1.0f : 0.15f, LEFT, false, true);
    D2D1_RECT_F unit = D2D1::RectF(side.left, r.top + h * 0.5f, side.right, r.bottom - h * 0.14f);
    text(am ? "kHz" : "MHz", unit, format(h * 0.24f), C_VFD_ON, 1.0f, LEFT, false, true);
}

void drawTopBar(float W, int freqHz) {
    float top = 12, bh = 84;
    button(D2D1::RectF(16, top, 80, top + bh), "\xE2\x9D\xAE", H_TUNE_DOWN, 0, 30);       // ❮
    float vfdW = std::min(460.0f, std::max(300.0f, W * 0.36f));
    drawVfd(D2D1::RectF(92, top, 92 + vfdW, top + bh), freqHz);
    button(D2D1::RectF(104 + vfdW, top, 168 + vfdW, top + bh), "\xE2\x9D\xAF", H_TUNE_UP, 0, 30);   // ❯
    float x = 188 + vfdW;
    bool on = running();
    bool paused = on && app.radio->paused();
    D2D1_RECT_F pr = D2D1::RectF(x, top + 14, x + 56, top + bh - 14);
    if (on) button(pr, paused ? "\xE2\x96\xB6" : "\xE2\x8F\xB8", H_PAUSE, 0, 22, C_SURFACE, C_TEXT);   // ▶ ⏸
    x += 64;
    D2D1_RECT_F sr = D2D1::RectF(x, top + 14, x + 56, top + bh - 14);
    button(sr, on ? "\xE2\x96\xA0" : "\xE2\x8F\xBB", H_POWER, 0, 22, C_SURFACE, on ? C_TEXT : C_GOOD);  // ■ ⏻
    x += 64;
    // ⏺ record: red while recording, with the time (and the IQ limit while only IQ records)
    if (on) {
        const Recorder &rec = app.radio->recorder();
        bool recording = rec.recording();
        D2D1_RECT_F rr = D2D1::RectF(x, top + 14, x + 56, top + bh - 14);
        button(rr, "\xE2\x8F\xBA", H_RECORD, 0, 22, recording ? C_LOST : C_SURFACE, recording ? 0xFFFFFF : C_LOST);
        x += 64;
        if (recording) {
            long long s = rec.elapsedMs() / 1000;
            std::string t = fmt("%lld:%02lld", s / 60, s % 60);
            if (rec.iqOnly() && rec.iqLimitS() > 0) t += " / " + recordings::limitText(rec.iqLimitS());
            float tw = textWidth(t, format(16)) + 8;
            text(t, D2D1::RectF(x, top, x + tw, top + bh), format(16), C_LOST, 1, LEFT, false, true);
            x += tw;
        }
    }
    x += 12;
    // volume
    float vw = std::min(180.0f, W - x - 90);
    if (vw > 60) {
        D2D1_RECT_F vr = D2D1::RectF(x, top + 30, x + vw, top + bh - 30);
        text("\xF0\x9F\x94\x8A", D2D1::RectF(x, top, x + 30, top + 28), format(14), C_DIM);
        float cy = (vr.top + vr.bottom) / 2;
        fill(D2D1::RectF(vr.left, cy - 2, vr.right, cy + 2), C_DIM, 0.4f, 2);
        float kx = vr.left + vw * app.cfg.volume / 100.0f;
        fill(D2D1::RectF(vr.left, cy - 2, kx, cy + 2), C_VFD_ON, 1, 2);
        setColor(C_VFD_ON);
        app.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, cy), 8, 8), app.brush);
        text(fmt("%d %%", app.cfg.volume), D2D1::RectF(x + 30, top, x + vw, top + 28), format(13), C_DIM, 1, RIGHT);
        addHit(D2D1::RectF(vr.left - 8, top + 20, vr.right + 8, top + bh - 20), H_VOLUME);
    }
    button(D2D1::RectF(W - 72, top + 14, W - 16, top + bh - 14), "\xE2\x9A\x99", H_SETTINGS, 0, 24, C_SURFACE, C_TEXT);   // ⚙
}

// HD1-HD8 across the left column, wrapping to a new row; returns the height used.
float drawPrograms(float left, float right, float y) {
    if (app.programs.empty()) return 0;
    float x = left, top = y;
    for (const Program &p : app.programs) {
        std::string name = p.nameFor(app.shown.station());
        float w = std::max(84.0f, std::min(220.0f, textWidth(name, format(12)) + 28));
        if (x + w > right && x > left) {
            x = left;
            y += 58;
        }
        D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + 48);
        bool sel = p.number == app.program;
        fill(r, sel ? C_BUTTON : C_SURFACE, 1, 10);
        if (!sel) stroke(r, C_TILE_EDGE, 0.35f, 10, 1);
        uint32_t fg = sel ? C_BUTTON_TEXT : C_TEXT;
        if (name.empty()) text(p.hdName(), r, format(18, DWRITE_FONT_WEIGHT_SEMI_BOLD), fg, 1, CENTER, false, true);
        else {
            text(p.hdName(), D2D1::RectF(r.left, r.top + 3, r.right, r.top + 26), format(16, DWRITE_FONT_WEIGHT_SEMI_BOLD), fg, 1, CENTER, false, true);
            text(name, D2D1::RectF(r.left + 6, r.top + 25, r.right - 6, r.bottom - 3), format(12), fg, 0.85f, CENTER, false, true);
        }
        addHit(r, H_PROGRAM, p.number);
        x += w + 10;
    }
    return y + 58 - top;
}

// The favorites as tiles across the left column, wrapping; returns the height used.
float drawPresets(float left, float right, float y, int freqHz) {
    const float tw = 150, th = 112;
    if (app.cfg.presets.empty()) {
        text("No favorites yet. Click \xE2\x98\x86 next to the station name to add one.", D2D1::RectF(left, y, right, y + 40),
             format(15), C_DIM, 1, LEFT, false, true);
        return 48;
    }
    float x = left, top = y;
    for (size_t i = 0; i < app.cfg.presets.size(); i++) {
        const Preset &p = app.cfg.presets[i];
        if (x + tw > right && x > left) {
            x = left;
            y += th + 12;
        }
        D2D1_RECT_F r = D2D1::RectF(x, y, x + tw, y + th);
        bool current = p.freqHz == freqHz && p.program == app.program;
        fill(r, C_SURFACE, 1, 14);
        stroke(r, current ? C_VFD_ON : C_TILE_EDGE, current ? 1.0f : 0.35f, 14, current ? 3.0f : 1.0f);
        std::string key = fmt("%d/%d/%d", p.freqHz, p.program, logocache::version());
        Pic &pic = app.presetPics[fmt("%d/%d", p.freqHz, p.program)];
        if (pic.key != key) makePic(pic, logocache::load(p.freqHz, p.program), key);
        D2D1_RECT_F logoArea = D2D1::RectF(r.left + 10, r.top + 8, r.right - 10, r.top + 64);
        if (pic.bmp) drawFit(pic, logoArea);
        else text(band::freqText(p.freqHz), logoArea, format(24, DWRITE_FONT_WEIGHT_BOLD, true), C_VFD_ON, 0.8f, CENTER, false, true);
        std::string name = p.name.empty() ? band::bandText(p.freqHz) : p.name;
        text(name, D2D1::RectF(r.left + 6, r.top + 66, r.right - 6, r.top + 88), format(16, DWRITE_FONT_WEIGHT_BOLD), C_TEXT, 1, CENTER, false, true);
        text(band::freqText(p.freqHz) + " HD" + std::to_string(p.program + 1), D2D1::RectF(r.left + 6, r.top + 88, r.right - 6, r.bottom - 6),
             format(13), C_DIM, 1, CENTER, false, true);
        addHit(r, H_PRESET, (int)i);
        x += tw + 12;
    }
    return y + th + 14 - top;
}

bool isFavorite(int freqHz) {
    for (const Preset &p : app.cfg.presets) if (p.freqHz == freqHz && p.program == app.program) return true;
    return false;
}

void drawNowPlaying(const D2D1_RECT_F &area, int freqHz) {
    bool on = running();
    const Sig &s = app.shown;
    bool otherAnalog = on && s.otherHd() && s.analog();
    bool rdsMode = on && ((s.station().empty() && app.programs.empty()) || otherAnalog);
    float W = area.right - area.left;
    float y = area.top;

    // the big picture
    float textBlock = 230;
    float picH = std::min(W * 0.75f, area.bottom - area.top - textBlock);
    if (app.nowPic.bmp && picH > 80) {
        D2D1_RECT_F pr = D2D1::RectF(area.left, y, area.right, y + picH);
        D2D1_RECT_F where = drawFit(app.nowPic, pr);
        addHit(where, H_NOW_IMAGE);
        y += picH + 14;
    } else if (picH > 80) {
        y += std::min(picH, 60.0f);
    }

    // station row: small logo (while the art has the big spot), name, star
    float rowH = 56;
    float x = area.left;
    if (app.smallLogo.bmp) {
        drawFit(app.smallLogo, D2D1::RectF(x, y, x + rowH, y + rowH));
        x += rowH + 14;
    }
    D2D1_RECT_F star = D2D1::RectF(area.right - 48, y + 4, area.right, y + rowH - 4);
    text(stationName(freqHz), D2D1::RectF(x, y, star.left - 8, y + rowH), format(38, DWRITE_FONT_WEIGHT_SEMI_LIGHT), C_TEXT, 1, LEFT, false, true);
    bool fav = isFavorite(freqHz);
    text(fav ? "\xE2\x98\x85" : "\xE2\x98\x86", star, format(30), fav ? C_STAR : C_TEXT, 1, CENTER, false, true);
    addHit(star, H_STAR);
    y += rowH + 4;

    // program: "HD1 · Adult Hits"; an RDS station: "FM · Religious talk"
    const Program *prog = rdsMode ? nullptr : selectedProgram();
    std::string progLine;
    if (rdsMode) {
        std::string t = sigtext::ptyName((int)s.l("rdsPty", -1));
        if (!t.empty()) progLine = "FM \xC2\xB7 " + t;
    } else if (prog) {
        std::string label = prog->nameFor(s.station());
        progLine = label.empty() ? prog->hdName() : prog->hdName() + " \xC2\xB7 " + label;
    }
    if (!progLine.empty()) {
        text(progLine, D2D1::RectF(area.left, y, area.right, y + 30), format(20), C_VFD_ON, 1, LEFT, false, true);
        y += 34;
    }

    // song, or the slogan; radio off: why
    std::string title, artist;
    if (!on) {
        title = app.radio ? app.radio->message() : "";
    } else if (prog && (!trim(prog->title).empty() || !trim(prog->artist).empty())) {
        title = prog->title;
        artist = prog->artist;
    } else {
        std::string slogan = s.slogan() != s.station() ? s.slogan() : "";
        title = rdsMode ? s.rdsLine1() : slogan;
        artist = rdsMode ? s.rdsLine2() : "";
    }
    if (!on) {
        float h = text(title, D2D1::RectF(area.left, y + 6, area.right, y + 80), format(17), C_HOLDING, 1, LEFT, true);
        y += h + 18;
        D2D1_RECT_F sb = D2D1::RectF(area.left, y, area.left + 220, y + 52);
        button(sb, "\xE2\x96\xB6  Start radio", H_START, 0, 20);
        y += 66;
    } else {
        if (!trim(title).empty()) {
            text(title, D2D1::RectF(area.left, y, area.right, y + 40), format(28), C_TEXT, 1, LEFT, false, true);
            y += 40;
        }
        if (!trim(artist).empty()) {
            text(artist, D2D1::RectF(area.left, y, area.right, y + 32), format(22), C_TEXT, 0.85f, LEFT, false, true);
            y += 34;
        }
        if (app.radio->paused()) {
            text("\xE2\x8F\xB8 Paused", D2D1::RectF(area.left, y, area.right, y + 26), format(16), C_HOLDING, 1, LEFT, false, true);
            y += 28;
        }
    }
    y += 10;

    // signal line: dot + text; click = open/close the details
    std::string weakWhy = on ? app.weak.reason(s) : "";
    sigtext::Line line = sigtext::signalLine(on, s, weakWhy);
    D2D1_RECT_F sl = D2D1::RectF(area.left, y, area.right, y + 30);
    text("\xE2\x97\x8F", D2D1::RectF(sl.left, sl.top, sl.left + 24, sl.bottom), format(16), signalColor(line.color), 1, LEFT, false, true);
    text(line.text, D2D1::RectF(sl.left + 26, sl.top, sl.right - 30, sl.bottom), format(18), C_TEXT, 0.9f, LEFT, false, true);
    text(app.cfg.detailsOpen ? "\xE2\x96\xB4" : "\xE2\x96\xBE", D2D1::RectF(sl.right - 26, sl.top, sl.right, sl.bottom), format(16), C_DIM, 1, CENTER, false, true);
    addHit(sl, H_SIGNAL);
    y += 34;
    std::string notice = on ? sigtext::otherHdNotice(s) : "";
    if (!notice.empty()) {
        float h = text(notice, D2D1::RectF(area.left, y, area.right, area.bottom), format(16), C_HOLDING, 1, LEFT, true);
        addHit(D2D1::RectF(area.left, y, area.right, y + h), H_OTHER_HD);
    }
}

void drawDetails(const D2D1_RECT_F &area, int freqHz) {
    bool on = running();
    std::string weakWhy = on ? app.weak.reason(app.shown) : "";
    auto rows = sigtext::details(on, app.shown, weakWhy, freqHz, on ? app.radio->soundMs() : -1,
                                 on ? app.radio->gainKeeper() : nullptr);
    IDWriteTextFormat *f = format(15);
    float nameW = 110, gap = 16;
    float valueW = area.right - area.left - nameW - gap;
    float total = 0;
    std::vector<float> heights;
    for (auto &r : rows) {
        float h = textHeight(r.second, f, valueW) + 6;
        heights.push_back(h);
        total += h;
    }
    app.detailsArea = area;
    app.detailsMaxScroll = std::max(0.0f, total - (area.bottom - area.top));
    app.detailsScroll = std::min(std::max(app.detailsScroll, 0.0f), app.detailsMaxScroll);
    app.rt->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
    float y = area.top - app.detailsScroll;
    for (size_t i = 0; i < rows.size(); i++) {
        if (y + heights[i] > area.top && y < area.bottom) {
            text(rows[i].first, D2D1::RectF(area.left, y, area.left + nameW, y + heights[i]), f, C_TEXT, 0.65f);
            text(rows[i].second, D2D1::RectF(area.left + nameW + gap, y, area.right, y + heights[i]), f,
                 sigtext::isWarning(rows[i].second) ? C_LOST : C_TEXT, 1, LEFT, true);
        }
        y += heights[i];
    }
    app.rt->PopAxisAlignedClip();
    addHit(area, H_DETAILS);
}

// "Nov 11, 2026  7:42 PM" in the computer's own time zone and clock style.
std::string stampText(long long ms) {
    ULARGE_INTEGER u;
    u.QuadPart = (unsigned long long)(ms + 11644473600000LL) * 10000ULL;
    FILETIME ft{u.LowPart, u.HighPart};
    SYSTEMTIME utc, local;
    FileTimeToSystemTime(&ft, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
    wchar_t d[64] = L"", t[64] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"MMM d, yyyy", d, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, t, 64);
    return narrow(d) + "  " + narrow(t);
}

void drawStamp(const D2D1_RECT_F &img, long long ms) {
    if (ms <= 0 || img.right <= img.left) return;
    float size = (img.right - img.left) / 26.0f;
    IDWriteTextFormat *f = format(size, DWRITE_FONT_WEIGHT_BOLD);
    std::string s = stampText(ms);
    float pad = size / 3;
    float w = textWidth(s, f) + 2 * pad, h = textHeight(s, f, 10000) + 2 * pad;
    fill(D2D1::RectF(img.left, img.top, img.left + w, img.top + h), 0x000000, 0.69f);
    text(s, D2D1::RectF(img.left + pad, img.top + pad, img.left + w, img.top + h), f, 0xFFFFFF);
}

// tabsX/headY: where the Traffic / Weather buttons go (the pane's heading row).
void drawMaps(const D2D1_RECT_F &area, float tabsX, float headY) {
    MapFeed &maps = app.radio->maps();
    int gen = maps.generation();
    std::vector<MapFeed::Frame> frames = maps.frames();
    if (gen != app.mapGen) {
        app.mapGen = gen;
        long long t = 0;
        std::shared_ptr<const Image> tr = maps.traffic(t);
        std::string key = fmt("%p", (const void *)tr.get());
        if (!tr) app.trafficPic.reset();
        else if (app.trafficPic.key != key) makePic(app.trafficPic, *tr, key);
        app.trafficTimeMs = t;
        // radar pictures: keep the ones still in the list
        std::map<std::string, Pic> keep;
        for (const auto &f : frames) {
            std::string k = fmt("%p", (const void *)f.jpeg.get());
            auto it = app.radarPics.find(k);
            if (it != app.radarPics.end()) { keep[k] = it->second; app.radarPics.erase(it); }
        }
        for (auto &kv : app.radarPics) kv.second.reset();
        app.radarPics = keep;
        if (!frames.empty() && app.radarFrame >= (int)frames.size() - 1) app.radarFrame = (int)frames.size() - 1;
    }
    bool weather = app.cfg.mapTab == 1;
    float y = area.top;
    if (tabsX >= 0) {   // the Traffic / Weather buttons (in the pane's heading row unless full screen)
        float ty = app.mapFull ? y : headY;
        D2D1_RECT_F t1 = D2D1::RectF(tabsX, ty, tabsX + 120, ty + 40);
        D2D1_RECT_F t2 = D2D1::RectF(tabsX + 128, ty, tabsX + 248, ty + 40);
        button(t1, "Traffic", H_TAB_TRAFFIC, 0, 17, C_SURFACE, C_TEXT, weather ? 0.5f : 1.0f);
        if (!weather) stroke(t1, C_VFD_ON, 1, 12, 2);
        button(t2, "Weather", H_TAB_WEATHER, 0, 17, C_SURFACE, C_TEXT, weather ? 1.0f : 0.5f);
        if (weather) stroke(t2, C_VFD_ON, 1, 12, 2);
        if (app.mapFull) {
            text("Double-click or Esc to go back", D2D1::RectF(t2.right + 16, ty, area.right, ty + 40), format(14), C_DIM, 1, RIGHT, false, true);
            y += 50;
        }
    }
    bool radarRow = weather && frames.size() > 1;
    float bottom = area.bottom - (weather ? 56 : 0);
    D2D1_RECT_F mapArea = D2D1::RectF(area.left, y, area.right, bottom);
    if (!weather) {
        if (!app.trafficPic.bmp) {
            text("Waiting for a traffic map. Not every station sends one.", mapArea, format(16), C_DIM, 1, LEFT, true);
            return;
        }
        D2D1_RECT_F where = drawFit(app.trafficPic, mapArea);
        drawStamp(where, app.trafficTimeMs);
        addHit(where, H_MAP);
        return;
    }
    if (frames.empty()) {
        text("Waiting for weather radar. Not every station sends it.", mapArea, format(16), C_DIM, 1, LEFT, true);
        return;
    }
    int n = (int)frames.size();
    app.radarFrame = std::min(std::max(app.radarFrame, 0), n - 1);
    const MapFeed::Frame &cur = frames[app.radarFrame];
    std::string k = fmt("%p", (const void *)cur.jpeg.get());
    Pic &pic = app.radarPics[k];
    if (pic.key != k) makePic(pic, *cur.jpeg, k);
    D2D1_RECT_F where = drawFit(pic, mapArea);
    drawStamp(where, cur.timeMs);
    addHit(where, H_MAP);
    // the radar row: play/pause, slider, "Frame 3 of 12. Map © OpenStreetMap contributors"
    float ry = bottom + 8;
    std::string note = fmt("Frame %d of %d. Map \xC2\xA9 OpenStreetMap contributors", app.radarFrame + 1, n);
    if (radarRow) {
        D2D1_RECT_F pb = D2D1::RectF(area.left, ry, area.left + 48, ry + 40);
        button(pb, app.radarPlaying ? "\xE2\x8F\xB8" : "\xE2\x96\xB6", H_PLAY, 0, 18, C_SURFACE, C_TEXT);
        float sx = area.left + 64, ex = area.right - 8;
        float noteW = std::min(textWidth(note, format(13)) + 8, (ex - sx) * 0.55f);
        ex -= noteW + 12;
        float cy = ry + 20;
        fill(D2D1::RectF(sx, cy - 2, ex, cy + 2), C_DIM, 0.4f, 2);
        float kx = sx + (ex - sx) * (n > 1 ? (float)app.radarFrame / (n - 1) : 1.0f);
        fill(D2D1::RectF(sx, cy - 2, kx, cy + 2), C_VFD_ON, 1, 2);
        setColor(C_VFD_ON);
        app.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, cy), 9, 9), app.brush);
        addHit(D2D1::RectF(sx - 10, ry, ex + 10, ry + 40), H_SLIDER, n);
        text(note, D2D1::RectF(ex + 12, ry, area.right, ry + 40), format(13), C_DIM, 1, RIGHT, true, true);
    } else {
        text(note, D2D1::RectF(area.left, ry, area.right, ry + 40), format(13), C_DIM, 1, LEFT, false, true);
    }
}

void paint() {
    if (!ensureDevice()) return;
    app.hits.clear();
    D2D1_SIZE_F size = app.rt->GetSize();
    float W = size.width, H = size.height;
    int freqHz = running() ? app.radio->freqHz() : app.cfg.freqHz;

    app.rt->BeginDraw();
    app.rt->Clear(rgb(C_BG));
    updatePictures(freqHz);

    if (app.mapFull && running()) {
        drawMaps(D2D1::RectF(16, 16, W - 16, H - 12), 16, 16);
    } else {
        drawTopBar(W, freqHz);
        // left column: HD1-HD8, favorites, now playing; the right-hand pane starts right under
        // the frequency bar, so the details and maps get the full height
        float top = 116;
        float split = std::floor(W * 0.46f);
        float y = top;
        y += drawPrograms(16, split - 16, y);
        y += drawPresets(16, split - 16, y, freqHz);
        drawNowPlaying(D2D1::RectF(16, y, split - 16, H - 16), freqHz);
        // the right-hand pane: its heading opens and closes the details; closed, the maps
        y = top;
        float rx = split + 16;
        setColor(C_TILE_EDGE, 0.25f);
        app.rt->DrawLine(D2D1::Point2F(split, y), D2D1::Point2F(split, H - 16), app.brush, 1);
        // one heading row: "Signal details ▾" and, while they're closed, Traffic / Weather beside it
        std::string headText = std::string("Signal details  ") + (app.cfg.detailsOpen ? "\xE2\x96\xB4" : "\xE2\x96\xBE");
        IDWriteTextFormat *hf = format(19, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float headW = textWidth(headText, hf) + 8;
        D2D1_RECT_F head = D2D1::RectF(rx, y, rx + headW, y + 40);
        text(headText, head, hf, C_TEXT, 0.9f, LEFT, false, true);
        addHit(head, H_DETAILS_HEAD);
        D2D1_RECT_F pane = D2D1::RectF(rx, y + 50, W - 16, H - 12);
        if (app.cfg.detailsOpen) drawDetails(pane, freqHz);
        else if (running()) drawMaps(pane, head.right + 24, y);
        else text("The traffic and weather maps show here while the radio plays.", pane, format(16), C_DIM, 1, LEFT, true);
    }
    // the toast: a message at the bottom for a few seconds
    if (!app.toast.empty() && nowMs() < app.toastUntilMs) {
        IDWriteTextFormat *tf = format(16);
        float tw = std::min(W - 64, 900.0f);
        float th = textHeight(app.toast, tf, tw - 32) + 24;
        D2D1_RECT_F box = D2D1::RectF((W - tw) / 2, H - th - 24, (W + tw) / 2, H - 24);
        fill(box, 0x2A2D2E, 0.97f, 12);
        stroke(box, C_TILE_EDGE, 0.5f, 12, 1);
        text(app.toast, D2D1::RectF(box.left + 16, box.top + 12, box.right - 16, box.bottom - 12), tf, C_TEXT, 1, LEFT, true);
    }
    HRESULT hr = app.rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) discardDevice();
}

// ------------------------------------------------------------------ dialogs

INT_PTR CALLBACK tuneProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(dlg, IDC_FREQ, widen(band::freqText(app.cfg.freqHz)).c_str());
        SendDlgItemMessageW(dlg, IDC_FREQ, EM_SETSEL, 0, -1);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[64];
            GetDlgItemTextW(dlg, IDC_FREQ, buf, 64);
            int hz = band::parseTyped(narrow(buf));
            if (hz < 0) {
                MessageBoxW(dlg, L"Enter FM 87.75–107.9 MHz, or AM 530–1700 kHz (10 kHz steps).", L"Tune to", MB_ICONINFORMATION);
                return TRUE;
            }
            EndDialog(dlg, hz);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, -1); return TRUE; }
    }
    return FALSE;
}

std::string g_renameText;
INT_PTR CALLBACK renameProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(dlg, IDC_NAME, widen(g_renameText).c_str());
        SendDlgItemMessageW(dlg, IDC_NAME, EM_SETSEL, 0, -1);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[128];
            GetDlgItemTextW(dlg, IDC_NAME, buf, 128);
            g_renameText = trim(narrow(buf));
            EndDialog(dlg, 1);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, 0); return TRUE; }
    }
    return FALSE;
}

const int GAIN_STEPS[] = {0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254,
                          280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496};

void comboAdd(HWND dlg, int id, const std::string &s) { SendDlgItemMessageW(dlg, id, CB_ADDSTRING, 0, (LPARAM)widen(s).c_str()); }
int comboGet(HWND dlg, int id) { return (int)SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0); }
void comboSet(HWND dlg, int id, int i) { SendDlgItemMessageW(dlg, id, CB_SETCURSEL, i, 0); }

const int IQ_LIMITS[] = {30, 60, 120, 300, 900, 0};   // as on Android; 0 = no limit
std::wstring g_recFolder;                              // the dialog's choice until OK

std::wstring shownFolder() { return g_recFolder.empty() ? recordings::defaultFolder() : g_recFolder; }

// Windows' own folder picker. "" = cancelled.
std::wstring pickFolder(HWND owner, const std::wstring &start) {
    std::wstring result;
    IFileOpenDialog *d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return result;
    DWORD opts = 0;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->SetTitle(L"Where should recordings go?");
    makeDirs(start);
    IShellItem *folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        d->SetFolder(folder);
        folder->Release();
    }
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) result = p;
            CoTaskMemFree(p);
            item->Release();
        }
    }
    d->Release();
    return result;
}

void settingsCounts(HWND dlg) {
    SetDlgItemTextW(dlg, IDC_BLOCKED, widen(fmt("Blocked pictures: %d (right-click a picture to block it)", artblock::count())).c_str());
    int logos = (int)listFiles(localDir() + L"\\logos").size();
    SetDlgItemTextW(dlg, IDC_LOGOS, widen(fmt("Saved station logos: %d", logos)).c_str());
}

INT_PTR CALLBACK settingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        int n = (int)rtlsdr_get_device_count();
        for (int i = 0; i < n; i++) comboAdd(dlg, IDC_DEVICE, fmt("%d: ", i) + rtlsdr_get_device_name(i));
        if (n == 0) comboAdd(dlg, IDC_DEVICE, "0: (no RTL-SDR found)");
        comboSet(dlg, IDC_DEVICE, std::min(app.cfg.device, std::max(n - 1, 0)));
        comboAdd(dlg, IDC_GAIN, "Auto (recommended)");
        int sel = 0;
        for (int i = 0; i < (int)(sizeof GAIN_STEPS / sizeof GAIN_STEPS[0]); i++) {
            comboAdd(dlg, IDC_GAIN, fmt("%.1f dB", GAIN_STEPS[i] / 10.0));
            if (GAIN_STEPS[i] == app.cfg.gainTenths) sel = i + 1;
        }
        comboSet(dlg, IDC_GAIN, sel);
        for (const char *s : {"Digital only (HD audio)", "Analog only (FM/AM audio)", "Auto: analog at once, HD when it's good"})
            comboAdd(dlg, IDC_HDMODE, s);
        comboSet(dlg, IDC_HDMODE, app.cfg.audioSource);
        comboAdd(dlg, IDC_MISMATCH, "Play the HD anyway (like car radios)");
        comboAdd(dlg, IDC_MISMATCH, "Stay on the analog");
        comboSet(dlg, IDC_MISMATCH, app.cfg.unmatchedPlaysHd ? 0 : 1);
        for (const char *s : {"Auto: stereo, mono when noisy", "Always mono", "Always stereo"}) comboAdd(dlg, IDC_STEREO, s);
        comboSet(dlg, IDC_STEREO, app.cfg.stereoMode);
        const int bws[] = {2500, 5000, 7500, 10000};
        for (int i = 0; i < 4; i++) {
            comboAdd(dlg, IDC_AMBW, fmt("%.1f kHz", bws[i] / 1000.0));
            if (bws[i] == app.cfg.amBandwidthHz) comboSet(dlg, IDC_AMBW, i);
        }
        if (comboGet(dlg, IDC_AMBW) < 0) comboSet(dlg, IDC_AMBW, 1);
        CheckDlgButton(dlg, IDC_AMDEEMPH, app.cfg.amDeemphasis ? BST_CHECKED : BST_UNCHECKED);
        settingsCounts(dlg);
        for (const char *t : {"Audio (.m4a, about 1 MB a minute)", "IQ samples (.cu8, about 180 MB a minute)", "Audio and IQ"})
            comboAdd(dlg, IDC_RECWHAT, t);
        comboSet(dlg, IDC_RECWHAT, app.cfg.recWhat);
        for (int i = 0; i < 6; i++) {
            comboAdd(dlg, IDC_RECLIMIT, IQ_LIMITS[i] ? recordings::limitText(IQ_LIMITS[i]) : std::string("No limit"));
            if (IQ_LIMITS[i] == app.cfg.recIqLimitS) comboSet(dlg, IDC_RECLIMIT, i);
        }
        if (comboGet(dlg, IDC_RECLIMIT) < 0) comboSet(dlg, IDC_RECLIMIT, 1);
        g_recFolder = app.cfg.recFolder;
        SetDlgItemTextW(dlg, IDC_RECFOLDER, shownFolder().c_str());
        const char *ver = "";
        nrsc5_get_version(&ver);
        std::string about = "HiDef Radio for Windows " HIDEF_VERSION ", from HiDef Radio by derek20la (Android) and its Maps fork. "
                            "nrsc5 " + std::string(ver) + ", FAAD2, FFTW, librtlsdr (RTL-SDR Blog), libusb. GPL-3.0. "
                            "Maps \xC2\xA9 OpenStreetMap contributors (CC BY-SA). " + stations::info() + ".";
        SetDlgItemTextW(dlg, IDC_ABOUT, widen(about).c_str());
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CLEARBLOCKED:
            if (MessageBoxW(dlg, L"Show all blocked pictures again?", L"Clear blocked pictures", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                artblock::clear();
                settingsCounts(dlg);
            }
            return TRUE;
        case IDC_RECBROWSE: {
            std::wstring f = pickFolder(dlg, shownFolder());
            if (!f.empty()) {
                g_recFolder = f;
                SetDlgItemTextW(dlg, IDC_RECFOLDER, f.c_str());
            }
            return TRUE;
        }
        case IDC_CLEARLOGOS:
            if (MessageBoxW(dlg, L"Delete the saved station logos? Stations send them again.", L"Clear saved logos", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                logocache::clear();
                settingsCounts(dlg);
            }
            return TRUE;
        case IDOK: {
            Settings old = app.cfg;
            int g = comboGet(dlg, IDC_GAIN);
            app.cfg.gainTenths = g <= 0 ? -1 : GAIN_STEPS[g - 1];
            app.cfg.device = std::max(comboGet(dlg, IDC_DEVICE), 0);
            app.cfg.audioSource = std::max(comboGet(dlg, IDC_HDMODE), 0);
            app.cfg.unmatchedPlaysHd = comboGet(dlg, IDC_MISMATCH) != 1;
            app.cfg.stereoMode = std::max(comboGet(dlg, IDC_STEREO), 0);
            const int bws[] = {2500, 5000, 7500, 10000};
            app.cfg.amBandwidthHz = bws[std::min(std::max(comboGet(dlg, IDC_AMBW), 0), 3)];
            app.cfg.amDeemphasis = IsDlgButtonChecked(dlg, IDC_AMDEEMPH) == BST_CHECKED;
            app.cfg.recWhat = std::max(comboGet(dlg, IDC_RECWHAT), 0);
            app.cfg.recIqLimitS = IQ_LIMITS[std::min(std::max(comboGet(dlg, IDC_RECLIMIT), 0), 5)];
            app.cfg.recFolder = g_recFolder;
            app.cfg.save();
            if (running()) {
                if (old.device != app.cfg.device) {
                    app.radio->stop();
                    app.radio->start();
                } else {
                    if (old.gainTenths != app.cfg.gainTenths) app.radio->applyGain();
                    if (old.audioSource != app.cfg.audioSource || old.unmatchedPlaysHd != app.cfg.unmatchedPlaysHd ||
                        old.stereoMode != app.cfg.stereoMode || old.amBandwidthHz != app.cfg.amBandwidthHz ||
                        old.amDeemphasis != app.cfg.amDeemphasis)
                        app.radio->applyAudioSettings();
                }
            }
            EndDialog(dlg, 1);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        }
    }
    return FALSE;
}

// ------------------------------------------------------------------ actions

void tuneTo(int hz, int program = 0) {
    if (!band::valid(hz)) return;
    app.cfg.freqHz = hz;
    app.cfg.program = program;
    app.program = program;
    app.programs.clear();
    app.shown = Sig();
    app.radarFrame = 1 << 20;   // the newest radar frame first
    if (running()) app.radio->tune(hz, program);
    app.picturesKey.clear();
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void step(bool up) {
    int hz = running() ? app.radio->freqHz() : app.cfg.freqHz;
    tuneTo(band::step(hz, up));
}

void toggleFavorite() {
    int hz = running() ? app.radio->freqHz() : app.cfg.freqHz;
    auto &ps = app.cfg.presets;
    for (size_t i = 0; i < ps.size(); i++)
        if (ps[i].freqHz == hz && ps[i].program == app.program) {
            ps.erase(ps.begin() + i);
            app.cfg.save();
            return;
        }
    Preset p;
    p.freqHz = hz;
    p.program = app.program;
    std::string name = stationName(hz);
    p.name = name == band::bandText(hz) ? "" : name;
    ps.push_back(p);
    app.cfg.save();
}

void presetMenu(int index, POINT screen) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (index == 0 ? MF_GRAYED : 0), 1, L"Move left");
    AppendMenuW(m, MF_STRING | (index + 1 >= (int)app.cfg.presets.size() ? MF_GRAYED : 0), 2, L"Move right");
    AppendMenuW(m, MF_STRING, 3, L"Rename…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"Delete");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, app.hwnd, nullptr);
    DestroyMenu(m);
    auto &ps = app.cfg.presets;
    if (cmd == 1 && index > 0) std::swap(ps[index], ps[index - 1]);
    else if (cmd == 2 && index + 1 < (int)ps.size()) std::swap(ps[index], ps[index + 1]);
    else if (cmd == 3) {
        g_renameText = ps[index].name;
        if (DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RENAME), app.hwnd, renameProc) == 1) ps[index].name = g_renameText;
    } else if (cmd == 4) ps.erase(ps.begin() + index);
    if (cmd) app.cfg.save();
}

void artMenu(POINT screen) {
    if (app.artBytes.empty()) return;          // only album art can be blocked, never the station logo
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Block this picture…");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, app.hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1 && MessageBoxW(app.hwnd, L"It won't be shown again, even when the station sends it again. Settings can clear the list.",
                                L"Block this picture?", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
        artblock::block(app.artBytes);
        app.picturesKey.clear();
    }
}

void showToast(const std::string &text, long long ms = 7000) {
    app.toast = text;
    app.toastUntilMs = nowMs() + ms;
}

void toggleRecording() {
    if (!running()) return;
    if (app.radio->recorder().recording()) {
        app.radio->stopRecording();
        return;
    }
    std::string problem = app.radio->startRecording(stationName(app.radio->freqHz()));
    if (!problem.empty()) { showToast(problem); return; }
    const Recorder &r = app.radio->recorder();
    std::string lim = recordings::limitText(app.cfg.recIqLimitS);
    if (app.cfg.recWhat == Recorder::AUDIO) showToast("âº Recording audio", 3000);
    else if (app.cfg.recWhat == Recorder::IQ)
        showToast(app.cfg.recIqLimitS ? "âº Recording IQ - stops after " + lim
                                      : std::string("âº Recording IQ - no limit, about 180 MB a minute"), 4000);
    else showToast(app.cfg.recIqLimitS ? "âº Recording audio and IQ (IQ stops after " + lim + ")"
                                       : std::string("âº Recording audio and IQ"), 4000);
    (void)r;
}

void setRadarPlaying(bool on) {
    app.radarPlaying = on;
    app.radarNextMs = nowMs() + 600;
}

void setVolumeFromX(float x, const D2D1_RECT_F &r) {
    float v = (x - (r.left + 8)) / (r.right - r.left - 16);
    int pct = (int)std::lround(std::min(std::max(v, 0.0f), 1.0f) * 100);
    if (app.radio) app.radio->setVolume(pct);
    app.cfg.volume = pct;
}

void setFrameFromX(float x, const Hit &h) {
    float sx = h.r.left + 10, ex = h.r.right - 10;
    int n = h.arg;
    if (n < 2) return;
    float v = std::min(std::max((x - sx) / (ex - sx), 0.0f), 1.0f);
    app.radarFrame = (int)std::lround(v * (n - 1));
}

void toggleWindowFull() {
    DWORD style = GetWindowLongW(app.hwnd, GWL_STYLE);
    if (!app.winFull) {
        MONITORINFO mi{sizeof mi};
        if (GetWindowPlacement(app.hwnd, &app.prevPlacement) && GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(app.hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(app.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            app.winFull = true;
        }
    } else {
        SetWindowLongW(app.hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(app.hwnd, &app.prevPlacement);
        SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        app.winFull = false;
    }
}

const Hit *hitAt(float x, float y) {
    for (auto it = app.hits.rbegin(); it != app.hits.rend(); ++it)
        if (inside(it->r, x, y)) return &*it;
    return nullptr;
}

void click(const Hit &h, float x, float y) {
    switch (h.id) {
    case H_TUNE_DOWN: step(false); break;
    case H_TUNE_UP: step(true); break;
    case H_VFD: {
        INT_PTR hz = DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TUNE), app.hwnd, tuneProc);
        if (hz > 0) tuneTo((int)hz);
        break;
    }
    case H_PAUSE: if (running()) app.radio->setPaused(!app.radio->paused()); break;
    case H_POWER:
    case H_START:
        if (running()) app.radio->stop();
        else app.radio->start();
        app.picturesKey.clear();
        break;
    case H_RECORD: toggleRecording(); break;
    case H_SETTINGS: DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), app.hwnd, settingsProc); break;
    case H_VOLUME: setVolumeFromX(x, h.r); break;
    case H_PROGRAM:
        if (running()) {
            app.radio->selectProgram(h.arg);
            app.program = h.arg;
            app.picturesKey.clear();
        }
        break;
    case H_PRESET: {
        const Preset p = app.cfg.presets[h.arg];
        tuneTo(p.freqHz, p.program);
        break;
    }
    case H_STAR: toggleFavorite(); break;
    case H_SIGNAL:
    case H_DETAILS_HEAD:
        app.cfg.detailsOpen = !app.cfg.detailsOpen;
        app.detailsScroll = 0;
        break;
    case H_OTHER_HD: if (running()) app.radio->listenToOtherStation(); break;
    case H_TAB_TRAFFIC: app.cfg.mapTab = 0; break;
    case H_TAB_WEATHER: app.cfg.mapTab = 1; app.radarFrame = 1 << 20; break;
    case H_PLAY:
        setRadarPlaying(!app.radarPlaying);
        app.cfg.radarAutoPlay = app.radarPlaying;
        break;
    case H_SLIDER:
        setFrameFromX(x, h);
        setRadarPlaying(false);
        app.cfg.radarAutoPlay = false;
        break;
    default: break;
    }
    (void)y;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// Five times a second: the radio's timers, what the screen shows, the radar loop.
void onTick() {
    if (app.radio) {
        app.radio->tick();
        std::string m = app.radio->takeRecordingMessage();   // "Saved 3.2 MB: ..."
        if (!m.empty()) showToast(m, 10000);
    }
    if (running()) {
        app.shown = app.radio->signal();
        if (!app.radio->retuning()) {
            app.programs = app.radio->programs();
            app.program = app.radio->currentProgram();
        }
    } else {
        app.shown = Sig();
        app.programs.clear();
        app.program = app.cfg.program;
    }
    // the radar loop: 0.6 s a frame, a rest on the newest one
    if (running() && app.cfg.mapTab == 1) {
        int n = (int)app.radio->maps().frames().size();
        if (app.cfg.radarAutoPlay && !app.radarPlaying && n > 1) setRadarPlaying(true);
        if (app.radarPlaying && n > 1 && nowMs() >= app.radarNextMs) {
            app.radarFrame = app.radarFrame >= n - 1 ? 0 : app.radarFrame + 1;
            app.radarNextMs = nowMs() + (app.radarFrame == n - 1 ? 1800 : 600);
        }
    }
    if (nowMs() - app.lastSaveMs > 30000) {    // keep the last station even if Windows shuts down hard
        app.lastSaveMs = nowMs();
        app.cfg.save();
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void saveWindow() {
    WINDOWPLACEMENT wp{sizeof wp};
    if (app.winFull) wp = app.prevPlacement;
    else GetWindowPlacement(app.hwnd, &wp);
    app.cfg.winX = wp.rcNormalPosition.left;
    app.cfg.winY = wp.rcNormalPosition.top;
    app.cfg.winW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    app.cfg.winH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    app.cfg.winMax = wp.showCmd == SW_SHOWMAXIMIZED;
}

float dips(int px) { return px * 96.0f / app.dpi; }

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        app.hwnd = hwnd;
        app.dpi = (float)GetDpiForWindow(hwnd);
        SetTimer(hwnd, 1, 200, nullptr);
        return 0;
    case WM_TIMER:
        onTick();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (app.rt) app.rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        app.dpi = (float)HIWORD(wp);
        if (app.rt) app.rt->SetDpi(app.dpi, app.dpi);
        RECT *r = (RECT *)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = MulDiv(900, (int)app.dpi, 96);
        mm->ptMinTrackSize.y = MulDiv(600, (int)app.dpi, 96);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            const Hit *h = hitAt(dips(p.x), dips(p.y));
            bool hand = h && h->id != H_DETAILS && h->id != H_MAP;
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        float x = dips(GET_X_LPARAM(lp)), y = dips(GET_Y_LPARAM(lp));
        const Hit *h = hitAt(x, y);
        app.pressed = h ? *h : Hit{{0, 0, 0, 0}, H_NONE, 0};
        if (h && (h->id == H_VOLUME || h->id == H_SLIDER)) {
            app.dragging = true;
            SetCapture(hwnd);
            click(*h, x, y);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (app.dragging) {
            float x = dips(GET_X_LPARAM(lp));
            if (app.pressed.id == H_VOLUME) setVolumeFromX(x, app.pressed.r);
            else if (app.pressed.id == H_SLIDER) setFrameFromX(x, app.pressed);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        float x = dips(GET_X_LPARAM(lp)), y = dips(GET_Y_LPARAM(lp));
        if (app.dragging) {
            app.dragging = false;
            ReleaseCapture();
            app.pressed.id = H_NONE;
            return 0;
        }
        const Hit *h = hitAt(x, y);
        if (h && h->id == app.pressed.id && h->arg == app.pressed.arg && h->id != H_NONE) click(*h, x, y);
        app.pressed.id = H_NONE;
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        const Hit *h = hitAt(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)));
        if (h && h->id == H_MAP) {
            app.mapFull = !app.mapFull;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (h) {
            Hit copy = *h;
            if (copy.id == H_TUNE_DOWN || copy.id == H_TUNE_UP) click(copy, 0, 0);   // a fast second click steps again
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        float x = dips(GET_X_LPARAM(lp)), y = dips(GET_Y_LPARAM(lp));
        const Hit *h = hitAt(x, y);
        POINT sp{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd, &sp);
        if (h && h->id == H_PRESET) presetMenu(h->arg, sp);
        else if (h && h->id == H_NOW_IMAGE) artMenu(sp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        float x = dips(p.x), y = dips(p.y);
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const Hit *h = hitAt(x, y);
        if (h && (h->id == H_VFD || h->id == H_TUNE_DOWN || h->id == H_TUNE_UP)) step(delta > 0);
        else if (h && h->id == H_VOLUME) {
            int v = std::min(100, std::max(0, app.cfg.volume + (delta > 0 ? 5 : -5)));
            app.cfg.volume = v;
            if (app.radio) app.radio->setVolume(v);
        } else if (inside(app.detailsArea, x, y) && app.cfg.detailsOpen)
            app.detailsScroll = std::min(std::max(app.detailsScroll - delta / 2.0f, 0.0f), app.detailsMaxScroll);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT: case VK_DOWN: step(false); break;
        case VK_RIGHT: case VK_UP: step(true); break;
        case VK_SPACE: if (running()) app.radio->setPaused(!app.radio->paused()); break;
        case VK_ESCAPE:
            if (app.mapFull) app.mapFull = false;
            else if (app.winFull) toggleWindowFull();
            break;
        case VK_F11: toggleWindowFull(); break;
        case 'D': app.cfg.detailsOpen = !app.cfg.detailsOpen; break;
        case 'F': toggleFavorite(); break;
        case 'R': toggleRecording(); break;
        case 'T': {
            INT_PTR hz = DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TUNE), hwnd, tuneProc);
            if (hz > 0) tuneTo((int)hz);
            break;
        }
        default:
            if (wp >= '1' && wp <= '8' && running()) {
                int p = (int)(wp - '1');
                for (const Program &pr : app.programs)
                    if (pr.number == p) {
                        app.radio->selectProgram(p);
                        app.program = p;
                        app.picturesKey.clear();
                    }
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        saveWindow();
        app.cfg.save();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (app.radio) app.radio->stop();
        app.radio.reset();
        discardDevice();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"HiDefRadioWindowsSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {        // one dongle, one radio
        HWND other = FindWindowW(L"HiDefRadioWindow", nullptr);
        if (other) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    hidef_log_open(narrow(localDir() + L"\\hidefradio.log").c_str());
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&app.dw);
    loadVfdFont();
    app.cfg.load();
    app.program = app.cfg.program;
    app.radarPlaying = false;

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"HiDefRadioWindow";
    RegisterClassExW(&wc);

    int x = app.cfg.winX, y = app.cfg.winY, w = app.cfg.winW, h = app.cfg.winH;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"HiDef Radio", WS_OVERLAPPEDWINDOW,
                                x >= 0 ? x : CW_USEDEFAULT, y >= 0 ? y : CW_USEDEFAULT, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    // dark title bar, like the rest of the window
    BOOL dark = TRUE;
    typedef HRESULT(WINAPI * DwmSet)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll"))
        if (auto set = (DwmSet)GetProcAddress(dwm, "DwmSetWindowAttribute")) set(hwnd, 20, &dark, sizeof dark);
    if (x < 0 || y < 0) {   // first run: a size that suits the screen
        float s = app.dpi / 96.0f;
        SetWindowPos(hwnd, nullptr, 0, 0, (int)(1280 * s), (int)(820 * s), SWP_NOMOVE | SWP_NOZORDER);
    }
    ShowWindow(hwnd, app.cfg.winMax ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hwnd);

    app.radio.reset(new Radio(app.cfg));
    app.radio->start();                                  // like the Android app with the dongle plugged in

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    for (auto &kv : app.formats) if (kv.second) kv.second->Release();
    if (app.vfdFonts) app.vfdFonts->Release();
    if (app.dw) app.dw->Release();
    if (app.d2d) app.d2d->Release();
    CoUninitialize();
    ReleaseMutex(single);
    return 0;
}
