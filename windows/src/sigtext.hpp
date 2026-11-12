// The signal line and the signal-details rows, worded as in the Android app's MainActivity.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "signal.hpp"

class GainKeeper;

namespace sigtext {
enum Color { OFF, GOOD, HOLDING, LOST };
const float OVERLOAD_DBFS = -1.0f;                 // the screen warns from here

struct Line { Color color; std::string text; };

// Remembers when audio errors were last seen (the "weak" word stays 5 s).
struct Weak {
    long long lastCrc = 0, crcAtMs = -1;
    std::string reason(const Sig &s);              // "" = not weak
};

Line signalLine(bool running, const Sig &s, const std::string &weak);
std::string otherHdNotice(const Sig &s);           // "" = none
std::vector<std::pair<std::string, std::string>> details(bool running, const Sig &s, const std::string &weak,
                                                          int freqHz, long long soundMs, const GainKeeper *keeper);
std::string ptyName(int pty);                      // RBDS (North America)
bool isWarning(const std::string &value);          // shown in red
}
