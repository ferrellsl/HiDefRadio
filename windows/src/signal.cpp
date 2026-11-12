#include "signal.hpp"
#include "util.hpp"

Sig Sig::parse(const std::string &text) {
    Sig s;
    for (const std::string &line : split(text, '\n')) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) s.m[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return s;
}
bool Sig::ownAnalogBack() const {
    std::string own = trim(str("hdOwnPi"));
    return hdContest() == 2 && !own.empty() && upper(trim(str("rdsPi"))) == upper(own);
}
std::string Sig::station() const { return trim(str("station")); }
std::string Sig::slogan() const { return trim(str("slogan")); }
std::string Sig::rdsName() const {
    std::string call = trim(str("rdsCall"));
    if (!call.empty()) return call;
    if (l("rdsPsSecs", 0) >= 15 && l("rdsPsChanges", 0) < 3) return trim(str("rdsPs"));
    return {};
}
std::string Sig::rdsLine1() const {
    if (!trim(str("rdsTitle")).empty()) return trim(str("rdsTitle"));
    if (!trim(str("rdsPsText")).empty()) return trim(str("rdsPsText"));
    return trim(str("rdsRt"));
}
std::string Sig::rdsLine2() const {
    if (!trim(str("rdsTitle")).empty()) return trim(str("rdsArtist"));
    if (!trim(str("rdsPsText")).empty()) return trim(str("rdsRtOther"));
    return {};
}
std::string Sig::percent(float ber, int decimals) {
    if (ber < 0) return "-";
    return fmt("%.*f%%", decimals, ber * 100);
}
std::string Sig::seconds(long long ms) { return ms < 0 ? "-" : fmt("%.2f s", ms / 1000.0); }
