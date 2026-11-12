#include "store.hpp"
#include "util.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>

extern "C" int __android_log_print(int, const char *, const char *, ...);
#define SLOG(...) __android_log_print(4, "Store", __VA_ARGS__)

// ------------------------------------------------------------------ settings

static std::wstring iniPath() { return roamingDir() + L"\\HiDefRadio.ini"; }
static std::wstring fmt_w(int i) { return L"F" + std::to_wstring(i); }   // a favorite's key

static int iniInt(const wchar_t *sec, const wchar_t *key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, iniPath().c_str());
}
static std::string iniStr(const wchar_t *sec, const wchar_t *key) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(sec, key, L"", buf, 1024, iniPath().c_str());
    return narrow(buf);
}

void Settings::load() {
    const wchar_t *S = L"Radio";
    freqHz = iniInt(S, L"Frequency", freqHz);
    if (!band::valid(freqHz)) freqHz = 95500000;
    program = std::min(std::max(iniInt(S, L"Program", 0), 0), 7);
    gainTenths = iniInt(S, L"Gain", -1);
    audioSource = std::min(std::max(iniInt(S, L"AudioSource", 2), 0), 2);
    unmatchedPlaysHd = iniInt(S, L"UnmatchedPlaysHd", 1) != 0;
    stereoMode = std::min(std::max(iniInt(S, L"Stereo", 0), 0), 2);
    amBandwidthHz = iniInt(S, L"AmBandwidth", 5000);
    amDeemphasis = iniInt(S, L"AmDeemphasis", 1) != 0;
    volume = std::min(std::max(iniInt(S, L"Volume", 100), 0), 100);
    device = std::max(iniInt(S, L"Device", 0), 0);
    recWhat = std::min(std::max(iniInt(S, L"RecordWhat", 0), 0), 2);
    recIqLimitS = std::max(iniInt(S, L"RecordIqLimit", 60), 0);
    recFolder = widen(iniStr(S, L"RecordFolder"));
    const wchar_t *W = L"Window";
    detailsOpen = iniInt(W, L"DetailsOpen", 0) != 0;
    mapTab = iniInt(W, L"MapTab", 0) == 1 ? 1 : 0;
    radarAutoPlay = iniInt(W, L"RadarAutoPlay", 1) != 0;
    winX = iniInt(W, L"X", -1);
    winY = iniInt(W, L"Y", -1);
    winW = iniInt(W, L"Width", 1280);
    winH = iniInt(W, L"Height", 800);
    winMax = iniInt(W, L"Maximized", 0) != 0;
    presets.clear();
    int n = iniInt(L"Favorites", L"Count", 0);
    for (int i = 0; i < n && i < 100; i++) {
        std::vector<std::string> p = split(iniStr(L"Favorites", fmt_w(i).c_str()), '|');
        if (p.size() < 2) continue;
        Preset pr;
        pr.freqHz = atoi(p[0].c_str());
        pr.program = atoi(p[1].c_str());
        if (p.size() > 2) pr.name = p[2];
        if (band::valid(pr.freqHz)) presets.push_back(pr);
    }
}

void Settings::save() const {
    std::wstring path = iniPath();
    auto put = [&](const wchar_t *sec, const wchar_t *key, const std::string &v) {
        WritePrivateProfileStringW(sec, key, widen(v).c_str(), path.c_str());
    };
    auto num = [&](const wchar_t *sec, const wchar_t *key, int v) { put(sec, key, fmt("%d", v)); };
    const wchar_t *S = L"Radio";
    num(S, L"Frequency", freqHz);
    num(S, L"Program", program);
    num(S, L"Gain", gainTenths);
    num(S, L"AudioSource", audioSource);
    num(S, L"UnmatchedPlaysHd", unmatchedPlaysHd);
    num(S, L"Stereo", stereoMode);
    num(S, L"AmBandwidth", amBandwidthHz);
    num(S, L"AmDeemphasis", amDeemphasis);
    num(S, L"Volume", volume);
    num(S, L"Device", device);
    num(S, L"RecordWhat", recWhat);
    num(S, L"RecordIqLimit", recIqLimitS);
    put(S, L"RecordFolder", narrow(recFolder));
    const wchar_t *W = L"Window";
    num(W, L"DetailsOpen", detailsOpen);
    num(W, L"MapTab", mapTab);
    num(W, L"RadarAutoPlay", radarAutoPlay);
    num(W, L"X", winX);
    num(W, L"Y", winY);
    num(W, L"Width", winW);
    num(W, L"Height", winH);
    num(W, L"Maximized", winMax);
    WritePrivateProfileStringW(L"Favorites", nullptr, nullptr, path.c_str());   // start the section over
    num(L"Favorites", L"Count", (int)presets.size());
    for (size_t i = 0; i < presets.size(); i++) {
        std::string name = presets[i].name;
        name.erase(std::remove(name.begin(), name.end(), '|'), name.end());
        put(L"Favorites", fmt_w((int)i).c_str(), fmt("%d|%d|", presets[i].freqHz, presets[i].program) + name);
    }
}

// ------------------------------------------------------------------ gain memory

namespace gainmem {
int get(int freqHz) { return iniInt(L"GainMemory", std::to_wstring(freqHz).c_str(), -1); }
void set(int freqHz, int tenths) {
    if (get(freqHz) == tenths) return;               // only writes if it's different
    WritePrivateProfileStringW(L"GainMemory", std::to_wstring(freqHz).c_str(),
                               std::to_wstring(tenths).c_str(), iniPath().c_str());
    SLOG("Remembered gain %.1f dB for %d Hz", tenths / 10.0, freqHz);
}
}  // namespace gainmem

// ------------------------------------------------------------------ blocked pictures

namespace artblock {
static std::mutex g_mutex;
static std::set<std::string> g_list;
static bool g_loaded = false;
static std::atomic<int> g_version{0};
static std::wstring path() { return roamingDir() + L"\\blocked_pictures.txt"; }

static std::string sha256(const std::string &data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    unsigned char hash[32] = {0};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        BCryptHash(alg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), hash, 32);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    std::string hex;
    for (unsigned char c : hash) hex += fmt("%02x", c);
    return hex;
}
static void loadLocked() {
    if (g_loaded) return;
    g_loaded = true;
    std::string text;
    if (!readFile(path(), text)) return;
    for (const std::string &line : split(text, '\n')) {
        std::string h = trim(line);
        if (h.size() == 64) g_list.insert(h);
    }
}
static void saveLocked() {
    std::string text;
    for (const std::string &h : g_list) text += h + "\n";
    writeFile(path(), text);
}
bool isBlocked(const std::string &picture) {
    std::lock_guard<std::mutex> l(g_mutex);
    loadLocked();
    return !g_list.empty() && g_list.count(sha256(picture)) != 0;
}
void block(const std::string &picture) {
    std::lock_guard<std::mutex> l(g_mutex);
    loadLocked();
    g_list.insert(sha256(picture));
    saveLocked();
    g_version++;
}
int count() {
    std::lock_guard<std::mutex> l(g_mutex);
    loadLocked();
    return (int)g_list.size();
}
void clear() {
    std::lock_guard<std::mutex> l(g_mutex);
    g_list.clear();
    g_loaded = true;
    DeleteFileW(path().c_str());
    g_version++;
}
int version() { return g_version.load(); }
}  // namespace artblock

// ------------------------------------------------------------------ logo cache (LogoCache.kt)

namespace logocache {
static std::mutex g_mutex;
static std::atomic<int> g_version{0};
static std::wstring dir() { std::wstring d = localDir() + L"\\logos"; makeDirs(d); return d; }

static std::string key(int freqHz, int program) {
    std::string f = freqHz >= 30000000 ? band::mhzText(freqHz) : fmt("%dk", freqHz / 1000);
    return f + "-HD" + std::to_string(program + 1);
}
std::string ownerKey(const std::string &name) {
    std::string out;
    for (char c : upper(name))
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
    return out.substr(0, 12);
}
static std::string piKey(const std::string &pi) {
    std::string p = upper(trim(pi));
    if (p.size() != 4) return "";
    for (char c : p) if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return "";
    return p;
}
static std::string fileName(const std::string &k, const std::string &owner, const std::string &pi) {
    if (owner.empty()) return k + ".img";
    if (pi.empty()) return k + "-" + owner + ".img";
    return k + "-" + owner + "-" + pi + ".img";
}
static Owner ownerOf(std::string name) {
    if (endsWith(name, ".img")) name.resize(name.size() - 4);
    std::vector<std::string> p = split(name, '-');
    Owner o;
    if (p.size() > 2) o.name = p[2];
    if (p.size() > 3) o.pi = piKey(p[3]);
    return o;
}
static bool isFor(const std::string &name, const std::string &k) {
    return endsWith(name, ".img") && (name == k + ".img" || startsWith(name, k + "-"));
}
static std::vector<std::string> names() {
    std::vector<std::string> out;
    for (const std::wstring &w : listFiles(dir())) out.push_back(narrow(w));
    return out;
}
static std::string findName(int freqHz, int program) {
    std::string k = key(freqHz, program);
    for (const std::string &n : names()) if (isFor(n, k)) return n;
    return "";
}
static std::vector<std::string> filesOf(int freqHz) {
    std::string prefix = key(freqHz, 0);
    prefix = prefix.substr(0, prefix.rfind("HD"));       // "91.1-"
    std::vector<std::string> out;
    for (const std::string &n : names())
        if (endsWith(n, ".img") && startsWith(n, prefix + "HD")) out.push_back(n);
    std::sort(out.begin(), out.end());
    return out;
}

bool owner(int freqHz, Owner &out) {
    std::lock_guard<std::mutex> l(g_mutex);
    bool found = false;
    for (const std::string &n : filesOf(freqHz)) {
        Owner o = ownerOf(n);
        if (o.name.empty()) continue;
        if (!o.pi.empty()) { out = o; return true; }
        if (!found) { out = o; found = true; }
    }
    return found;
}
bool fits(int freqHz, const std::string &station, bool analogHeard, const std::string &rdsPi) {
    Owner o;
    if (!owner(freqHz, o)) return true;
    if (!station.empty() && !ownerKey(station).empty() && ownerKey(station) != o.name) return false;
    if (analogHeard && !o.pi.empty() && !piKey(rdsPi).empty() && piKey(rdsPi) != o.pi) return false;
    return true;
}
std::string load(int freqHz, int program) {
    std::lock_guard<std::mutex> l(g_mutex);
    std::string n = findName(freqHz, program), bytes;
    if (!n.empty()) readFile(dir() + L"\\" + widen(n), bytes);
    return bytes;
}
void save(int freqHz, int program, const std::string &bytes, const std::string &ownerName, const std::string &ownerPi) {
    if (bytes.empty()) return;
    std::lock_guard<std::mutex> l(g_mutex);
    std::string k = key(freqHz, program);
    std::string old = findName(freqHz, program);
    Owner had = old.empty() ? Owner() : ownerOf(old);
    std::string name = ownerKey(ownerName);
    if (name.empty()) name = had.name;
    std::string pi = name == had.name ? (piKey(ownerPi).empty() ? had.pi : piKey(ownerPi)) : piKey(ownerPi);
    std::string target = fileName(k, name, pi);
    std::wstring d = dir();
    if (!old.empty()) {
        std::string oldBytes;
        if (readFile(d + L"\\" + widen(old), oldBytes) && oldBytes == bytes) {
            if (old != target && MoveFileExW((d + L"\\" + widen(old)).c_str(), (d + L"\\" + widen(target)).c_str(), 0))
                g_version++;
            return;
        }
    }
    if (!writeFile(d + L"\\" + widen(target), bytes)) return;
    if (!old.empty() && old != target) DeleteFileW((d + L"\\" + widen(old)).c_str());
    if (!name.empty())
        for (const std::string &f : filesOf(freqHz)) {
            std::string o = ownerOf(f).name;
            if (!o.empty() && o != name) DeleteFileW((d + L"\\" + widen(f)).c_str());   // the HD here is someone else's now
        }
    g_version++;
    SLOG("Saved logo %s (%zu bytes)", target.c_str(), bytes.size());
}
void setOwner(int freqHz, const std::string &ownerName, const std::string &pi) {
    std::string name = ownerKey(ownerName);
    if (name.empty()) return;
    std::lock_guard<std::mutex> l(g_mutex);
    std::wstring d = dir();
    int renamed = 0;
    for (const std::string &f : filesOf(freqHz)) {
        Owner o = ownerOf(f);
        if (!o.name.empty() && o.name != name) continue;
        std::vector<std::string> parts = split(f.substr(0, f.size() - 4), '-');
        if (parts.size() < 2) continue;
        std::string target = fileName(parts[0] + "-" + parts[1], name, piKey(pi).empty() ? o.pi : piKey(pi));
        if (target == f) continue;
        if (MoveFileExW((d + L"\\" + widen(f)).c_str(), (d + L"\\" + widen(target)).c_str(), 0)) renamed++;
    }
    if (renamed > 0) g_version++;
}
int version() { return g_version.load(); }
int clear() {
    std::lock_guard<std::mutex> l(g_mutex);
    int n = 0;
    std::wstring d = dir();
    for (const std::string &f : names()) n += DeleteFileW((d + L"\\" + widen(f)).c_str()) ? 1 : 0;
    g_version++;
    return n;
}
}  // namespace logocache
