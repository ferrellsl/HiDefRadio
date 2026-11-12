// What the app keeps between runs: settings, favorites, remembered gains, blocked pictures and
// saved logos. Settings, favorites and blocks live in %APPDATA%\HiDefRadio; logos and radar
// frames (things that can be fetched again) in %LOCALAPPDATA%\HiDefRadio.
#pragma once
#include <string>
#include <vector>

struct Preset {
    int freqHz = 0;
    int program = 0;          // 0 = HD1
    std::string name;         // the station's name when it was saved (or yours)
};

struct Settings {
    int freqHz = 95500000;
    int program = 0;
    int gainTenths = -1;              // -1 = auto
    int audioSource = 2;              // 0 = HD only, 1 = analog only, 2 = auto (blend)
    bool unmatchedPlaysHd = true;     // the HD1 isn't the analog's programme: play the HD anyway
    int stereoMode = 0;               // 0 auto, 1 mono, 2 stereo
    int amBandwidthHz = 5000;
    bool amDeemphasis = true;
    int volume = 100;                 // percent
    bool detailsOpen = false;         // the signal-details pane (closed: the maps take its place)
    int mapTab = 0;                   // 0 traffic, 1 weather
    bool radarAutoPlay = true;        // the radar loop plays by itself
    int device = 0;                   // which RTL-SDR (index)
    int recWhat = 0;                  // ⏺ records: 0 audio, 1 IQ, 2 both
    int recIqLimitS = 60;             // IQ stops after this many seconds; 0 = no limit
    std::wstring recFolder;           // "" = Music\HiDef Radio
    int winX = -1, winY = -1, winW = 1280, winH = 800;
    bool winMax = false;
    std::vector<Preset> presets;

    void load();
    void save() const;
};

namespace gainmem {
int get(int freqHz);                  // -1 = none
void set(int freqHz, int tenths);
}

namespace artblock {
bool isBlocked(const std::string &picture);
void block(const std::string &picture);
int count();
void clear();
int version();                        // goes up when the list changes
}

namespace logocache {
struct Owner { std::string name, pi; };
std::string ownerKey(const std::string &name);
bool owner(int freqHz, Owner &out);
bool fits(int freqHz, const std::string &station, bool analogHeard, const std::string &rdsPi);
std::string load(int freqHz, int program);
void save(int freqHz, int program, const std::string &bytes, const std::string &owner, const std::string &ownerPi);
void setOwner(int freqHz, const std::string &owner, const std::string &pi);
int version();
int clear();
}
