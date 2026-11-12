// The engine's signal details ("key=value" lines), read the way Signal.kt reads them.
#pragma once
#include <cstdlib>
#include <map>
#include <string>

struct Sig {
    std::map<std::string, std::string> m;
    static Sig parse(const std::string &text);

    std::string str(const char *k) const {
        auto i = m.find(k);
        return i == m.end() ? std::string() : i->second;
    }
    bool b(const char *k) const { return str(k) == "1"; }
    float f(const char *k, float def = -1) const {
        auto i = m.find(k);
        return i == m.end() || i->second.empty() ? def : strtof(i->second.c_str(), nullptr);
    }
    long long l(const char *k, long long def = -1) const {
        auto i = m.find(k);
        return i == m.end() || i->second.empty() ? def : atoll(i->second.c_str());
    }

    bool synced() const { return b("synced"); }
    bool locked() const { return b("locked"); }
    bool everSynced() const { return b("everSynced"); }
    bool am() const { return b("am"); }
    bool holding() const { return synced() && !locked(); }
    std::string blendState() const { std::string s = str("blendState"); return s.empty() ? "off" : s; }
    bool autoMode() const { return str("audioSource") == "auto"; }
    bool analog() const { return str("audioSource") == "analog" || (autoMode() && blendState() == "analog"); }
    std::string alignState() const { std::string s = str("alignState"); return s.empty() ? "waiting" : s; }
    int hdSame() const { return (int)l("hdSame", 0); }
    int hdContest() const { return (int)l("hdContest", 0); }
    bool otherHd() const { return hdSame() == 2 && (alignState() == "nomatch" || hdContest() == 2); }
    bool ownAnalogBack() const;
    std::string station() const;
    std::string slogan() const;
    std::string rdsName() const;   // call letters, or a PS name that stands still
    std::string rdsLine1() const;
    std::string rdsLine2() const;

    static std::string percent(float ber, int decimals = 2);
    static std::string seconds(long long ms);
};
