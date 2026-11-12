#include "util.hpp"
#include <windows.h>
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cmath>

std::wstring widen(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string narrow(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::string fmt(const char *format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    if (n < 0) return {};
    if (n < (int)sizeof buf) return std::string(buf, n);
    std::string big(n + 1, 0);
    va_start(ap, format);
    vsnprintf(&big[0], n + 1, format, ap);
    va_end(ap);
    big.resize(n);
    return big;
}

std::wstring exeDir() {
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}
std::wstring dataDir() { return exeDir() + L"\\data"; }
static std::wstring known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r + L"\\HiDefRadio";
}
std::wstring roamingDir() { static std::wstring d = known(FOLDERID_RoamingAppData); makeDirs(d); return d; }
std::wstring localDir() { static std::wstring d = known(FOLDERID_LocalAppData); makeDirs(d); return d; }
void makeDirs(const std::wstring &path) { SHCreateDirectoryExW(nullptr, path.c_str(), nullptr); }

bool readFile(const std::wstring &path, std::string &out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    out.resize((size_t)size.QuadPart);
    DWORD got = 0;
    BOOL ok = out.empty() || ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    return ok && (out.empty() || got == out.size());
}
bool writeFile(const std::wstring &path, const std::string &data) {
    std::wstring tmp = path + L".tmp";                 // write, then rename: never a half file
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr);
    CloseHandle(h);
    if (!ok || (!data.empty() && put != data.size())) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
static std::vector<std::wstring> listEntries(const std::wstring &dir, bool dirs) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::wstring n = fd.cFileName;
        if (isDir == dirs && n != L"." && n != L"..") out.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
std::vector<std::wstring> listFiles(const std::wstring &dir) { return listEntries(dir, false); }
std::vector<std::wstring> listDirs(const std::wstring &dir) { return listEntries(dir, true); }

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    while (true) {
        size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}
std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::string upper(std::string s) {
    for (auto &c : s) if (c >= 'a' && c <= 'z') c -= 32;
    return s;
}
bool startsWith(const std::string &s, const std::string &p) { return s.compare(0, p.size(), p) == 0; }
bool endsWith(const std::string &s, const std::string &p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
long long nowMs() { return (long long)GetTickCount64(); }
long long wallMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000ULL) - 11644473600000LL;
}

namespace band {
static const int FM_FIRST = 87900000, FM_LAST = 107900000, FM_STEP = 200000, FM_EXTRA = 87750000;
static const int FM_MIN = 87750000, FM_MAX = 107900000;
static const int AM_FIRST = 530000, AM_LAST = 1700000, AM_STEP = 10000;

std::string mhzText(int hz) { return hz % 100000 == 0 ? fmt("%.1f", hz / 1e6) : fmt("%.2f", hz / 1e6); }
std::string freqText(int hz) { return isAm(hz) ? fmt("%d", hz / 1000) : mhzText(hz); }
std::string bandText(int hz) { return freqText(hz) + (isAm(hz) ? " AM" : " FM"); }
static std::vector<int> channels(bool am) {
    std::vector<int> v;
    if (am) {
        for (int f = AM_FIRST; f <= AM_LAST; f += AM_STEP) v.push_back(f);
    } else {
        v.push_back(FM_EXTRA);
        for (int f = FM_FIRST; f <= FM_LAST; f += FM_STEP) v.push_back(f);
    }
    return v;
}
int step(int hz, bool up) {
    static const std::vector<int> fm = channels(false), am = channels(true);
    const std::vector<int> &list = isAm(hz) ? am : fm;
    if (up) {
        for (int f : list) if (f > hz) return f;
        return list.front();
    }
    for (auto it = list.rbegin(); it != list.rend(); ++it) if (*it < hz) return *it;
    return list.back();
}
bool valid(int hz) {
    if (isAm(hz)) return hz >= AM_FIRST && hz <= AM_LAST && (hz - AM_FIRST) % AM_STEP == 0;
    return hz >= FM_MIN && hz <= FM_MAX;
}
int parseTyped(const std::string &text) {
    std::string t = trim(text);
    for (auto &c : t) if (c == ',') c = '.';
    if (t.empty()) return -1;
    char *end = nullptr;
    double v = strtod(t.c_str(), &end);
    if (end == t.c_str() || *end) return -1;
    int hz = -1;
    if (v >= 100.0 && v < 10000.0 && v >= AM_FIRST / 1000 - 50) hz = (int)lround(v) * 1000;
    else if (v >= 1.0 && v < 1000.0) hz = (int)lround(v * 20) * 50000;
    return hz > 0 && valid(hz) ? hz : -1;
}
}  // namespace band
