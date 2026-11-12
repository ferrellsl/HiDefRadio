// Small helpers shared by the Windows app.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

std::wstring widen(const std::string &utf8);
std::string narrow(const std::wstring &w);
std::string fmt(const char *format, ...);            // printf into a std::string

std::wstring exeDir();                                // folder of HiDefRadio.exe
std::wstring dataDir();                               // read-only data next to the exe (data folder)
std::wstring roamingDir();                            // %APPDATA%\HiDefRadio (settings, presets, blocks)
std::wstring localDir();                              // %LOCALAPPDATA%\HiDefRadio (logos, radar, log)
void makeDirs(const std::wstring &path);

bool readFile(const std::wstring &path, std::string &out);
bool writeFile(const std::wstring &path, const std::string &data);
std::vector<std::wstring> listFiles(const std::wstring &dir);   // names only, files only
std::vector<std::wstring> listDirs(const std::wstring &dir);

std::vector<std::string> split(const std::string &s, char sep);
std::string trim(const std::string &s);
std::string upper(std::string s);
bool startsWith(const std::string &s, const std::string &p);
bool endsWith(const std::string &s, const std::string &p);

long long nowMs();                                    // monotonic (GetTickCount64)
long long wallMs();                                   // Unix time in ms (UTC)

// FM/AM frequencies, the Americas band (FmBand.AMERICAS on Android).
namespace band {
const int AM_BELOW_HZ = 30000000;
inline bool isAm(int hz) { return hz > 0 && hz < AM_BELOW_HZ; }
std::string mhzText(int hz);                          // "95.5", "87.75"
std::string freqText(int hz);                         // FM "95.5", AM "1260"
std::string bandText(int hz);                         // "95.5 FM", "1260 AM"
int step(int hz, bool up);                            // next channel, wraps around
int parseTyped(const std::string &text);              // "101.1" or "1260" -> Hz, -1 = no
bool valid(int hz);
}  // namespace band
