// The built-in FCC station list (data\stations\fm.txt / am.txt, the same files as the
// Android app's assets): who is licensed on a frequency. A port of Stations.kt.
#pragma once
#include <string>
#include <vector>

struct Station {
    int freqHz = 0;
    std::string call, service, fccClass;
    bool hd = false;
    std::string city, state, country;
    double lat = 0, lon = 0, kw = -1, nightKw = -1;
    int haatM = 0;
    std::string directional;

    std::string baseCall() const;
    std::string place() const;
    std::string power() const;
    std::string kind() const;
};

namespace stations {
std::vector<Station> on(int hz);                                  // licensed on this frequency
const Station *find(int hz, const std::string &name, std::vector<Station> &holder);
bool elsewhere(int hz, const std::string &name, Station &out);    // licensed on another frequency
std::string callsOn(int hz);                                      // "KYOT KXYZ" (US calls), "" = none
std::string baseCall(const std::string &call);
std::string info();                                               // "FCC data of 2026-10-26"
}
