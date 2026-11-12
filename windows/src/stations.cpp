#include "stations.hpp"
#include "util.hpp"
#include <map>
#include <mutex>

namespace {
struct Band {
    bool loaded = false;
    std::map<int, std::vector<std::string>> lines;   // key -> station lines
    std::string header;
};
std::mutex g_lock;
Band g_fm, g_am;

int keyOf(int hz) { return band::isAm(hz) ? hz / 1000 : (hz + 5000) / 10000; }

Band &load(bool am) {
    Band &b = am ? g_am : g_fm;
    if (b.loaded) return b;
    b.loaded = true;
    std::string text;
    if (!readFile(dataDir() + (am ? L"\\stations\\am.txt" : L"\\stations\\fm.txt"), text)) return b;
    int key = 0;
    size_t a = 0;
    while (a < text.size()) {
        size_t e = text.find('\n', a);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(a, e - a);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        a = e + 1;
        if (line.empty()) continue;
        if (line[0] == '#') { if (b.header.empty()) b.header = line; continue; }
        if (line[0] == '@') { key = atoi(line.c_str() + 1); continue; }
        b.lines[key].push_back(line);
    }
    return b;
}

bool parse(const std::string &line, bool am, int key, Station &s) {
    std::vector<std::string> p = split(line, '|');
    if (p.size() < 11) return false;
    auto num = [](const std::string &t, double def) { return t.empty() ? def : strtod(t.c_str(), nullptr); };
    if (am) {
        s.freqHz = key * 1000;
        s.call = p[0]; s.service = p[1]; s.fccClass = p[2]; s.hd = false;
        s.city = p[3]; s.state = p[4]; s.country = p[5];
        s.lat = num(p[6], 0); s.lon = num(p[7], 0);
        s.kw = num(p[8], -1); s.nightKw = num(p[9], -1); s.haatM = 0; s.directional = p[10];
    } else {
        s.freqHz = key * 10000;
        s.call = p[0]; s.service = p[1]; s.fccClass = p[2]; s.hd = p[3] == "H";
        s.city = p[4]; s.state = p[5]; s.country = p[6];
        s.lat = num(p[7], 0); s.lon = num(p[8], 0);
        s.kw = num(p[9], -1); s.nightKw = -1; s.haatM = atoi(p[10].c_str());
    }
    return true;
}

std::vector<std::string> words(const std::string &name) {
    std::vector<std::string> out;
    std::string w;
    for (char c : name + " ") {
        if (c == ' ' || c == '/' || c == ',') {
            std::string b = stations::baseCall(w);
            if (!b.empty()) out.push_back(b);
            w.clear();
        } else w += c;
    }
    return out;
}

bool isUsCall(const std::string &b) {
    if (b.size() < 3 || b.size() > 4 || (b[0] != 'K' && b[0] != 'W')) return false;
    for (char c : b) if (c < 'A' || c > 'Z') return false;
    return true;
}

std::string trimZeros(std::string n) {
    if (n.find('.') == std::string::npos) return n;
    while (!n.empty() && n.back() == '0') n.pop_back();
    if (!n.empty() && n.back() == '.') n.pop_back();
    return n;
}
std::string powerText(double kw) {
    if (kw < 0) return "";
    if (kw >= 1.0) return trimZeros(fmt("%.2f", kw)) + " kW";
    return trimZeros(fmt("%.1f", kw * 1000.0)) + " W";
}
std::string countryName(const std::string &c) {
    if (c == "US") return "";
    if (c == "CA") return "Canada";
    if (c == "MX") return "Mexico";
    if (c == "VG") return "British Virgin Islands";
    return c;
}
}  // namespace

std::string Station::baseCall() const { return stations::baseCall(call); }
std::string Station::place() const {
    std::string out;
    for (const std::string &p : {city, state, countryName(country)})
        if (!trim(p).empty()) out += (out.empty() ? "" : ", ") + p;
    return out;
}
std::string Station::power() const {
    if (kw < 0 && nightKw < 0) return "";
    if (service != "AM") return powerText(kw);
    if (nightKw < 0) return powerText(kw) + " day only";
    if (kw < 0) return powerText(nightKw) + " night only";
    if (kw == nightKw) return powerText(kw);
    return powerText(kw) + " day, " + powerText(nightKw) + " night";
}
std::string Station::kind() const {
    if (service == "FX") return "translator";
    if (service == "FL") return "low power (LPFM)";
    if (service == "FB") return "booster";
    return fccClass.empty() ? "" : "class " + fccClass;
}

namespace stations {
std::string baseCall(const std::string &call) {
    return upper(trim(call.substr(0, call.find('-'))));
}

std::vector<Station> on(int hz) {
    std::lock_guard<std::mutex> l(g_lock);
    bool am = band::isAm(hz);
    Band &b = load(am);
    std::vector<Station> out;
    auto it = b.lines.find(keyOf(hz));
    if (it == b.lines.end()) return out;
    for (const std::string &line : it->second) {
        Station s;
        if (parse(line, am, keyOf(hz), s)) out.push_back(s);
    }
    return out;
}

const Station *find(int hz, const std::string &name, std::vector<Station> &holder) {
    holder = on(hz);
    for (const std::string &w : words(name))
        for (const Station &s : holder)
            if (s.baseCall() == w) return &s;
    return nullptr;
}

bool elsewhere(int hz, const std::string &name, Station &out) {
    std::lock_guard<std::mutex> l(g_lock);
    bool am = band::isAm(hz);
    Band &b = load(am);
    for (const std::string &w : words(name)) {
        if (w.size() < 3) continue;
        for (auto &kv : b.lines)
            for (const std::string &line : kv.second) {
                if (!startsWith(line, w) || line.size() <= w.size()) continue;
                char next = line[w.size()];
                if (next != '|' && next != '-') continue;
                Station s;
                if (!parse(line, am, kv.first, s) || s.service == "FB" || s.service == "FX") continue;
                out = s;
                return true;
            }
    }
    return false;
}

std::string callsOn(int hz) {
    std::string out;
    std::vector<std::string> seen;
    for (const Station &s : on(hz)) {
        std::string b = s.baseCall();
        if (!isUsCall(b)) continue;
        bool dup = false;
        for (auto &x : seen) dup |= x == b;
        if (dup) continue;
        seen.push_back(b);
        out += (out.empty() ? "" : " ") + b;
    }
    return out;
}

std::string info() {
    std::lock_guard<std::mutex> l(g_lock);
    const std::string &h = load(false).header;
    size_t p = h.find("date ");
    return p == std::string::npos ? "" : "FCC data of " + h.substr(p + 5, 10);
}
}  // namespace stations
