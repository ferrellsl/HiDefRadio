#include "sigtext.hpp"
#include "gainkeeper.hpp"
#include "stations.hpp"
#include "util.hpp"

namespace sigtext {
namespace {
const long long WEAK_HOLD_MS = 5000, WEAK_NO_AUDIO_MS = 5000, WEAK_NO_AUDIO_AM_MS = 10000;
const long long AM_ACQUIRE_MS = 2500;
const float QUIET_NOISE_DB = 15, QUIET_WEAK_DB = 25;
const float BARS_DB[5] = {15, 20, 28, 36, 44};     // FM quieting and AM C/N alike
const char *OVERLOAD = "overload! Lower the gain";
const char *OVERLOAD_AUTO = "overload! Lowering the gain\xE2\x80\xA6";
const char *OVERLOAD_DIRECT = "overload! Try a shorter antenna";

std::string meter(float v) {
    int bars = 0;
    for (float b : BARS_DB) bars += v >= b ? 1 : 0;
    std::string m;
    for (int i = 0; i < 5; i++) m += i < bars ? "\xE2\x96\xAE" : "\xE2\x96\xAF";   // ▮ ▯
    return m;
}

bool hdResting(const Sig &s) { std::string h = s.str("hdSearch"); return h == "rest" || h == "look"; }

std::string hdInBrackets(const Sig &s) {
    if (!s.b("hdOn")) return "HD off";
    if (!s.everSynced()) return hdResting(s) ? "no HD" : "no HD yet";
    if (!s.synced()) return "HD lost";
    if (s.b("blendMismatch")) return "HD \xE2\x89\xA0 analog";
    if (s.am()) return fmt("HD %s BER %s", s.str("mode").c_str(), Sig::percent(s.f("ber")).c_str());
    return fmt("HD %s MER %.1f / %.1f", s.str("mode").c_str(), s.f("merLower", 0), s.f("merUpper", 0));
}

Line analogAmLine(const Sig &s) {
    std::string hd = hdInBrackets(s), label = "Analog AM";
    if (!s.b("amLocked") && s.l("tunedMs", 0) < AM_ACQUIRE_MS) return {OFF, label + "  \xE2\x80\xA6  (" + hd + ")"};
    if (!s.b("amLocked")) return {LOST, label + "  no carrier  (" + hd + ")"};
    float cn = s.f("amSnrDb", -99);
    bool measuring = cn <= -98;
    std::string st = s.l("amStereoPct", 0) >= 50 ? " ST" : "";
    std::string level = measuring ? "\xE2\x80\xA6" : cn < QUIET_NOISE_DB ? meter(cn) + " noise"
                        : cn < QUIET_WEAK_DB ? meter(cn) + " weak" + st : meter(cn) + st;
    bool overload = s.f("peakDbfs", -99) > OVERLOAD_DBFS;
    Color c = measuring ? OFF : cn < QUIET_NOISE_DB ? LOST : (cn < QUIET_WEAK_DB || overload) ? HOLDING : GOOD;
    return {c, label + "  " + level + "  (" + hd + ")"};
}

Line analogLine(const Sig &s) {
    if (s.am()) return analogAmLine(s);
    float q = s.f("fmQuietDb", -99);
    bool measuring = q <= -98;
    float quiet = -q;
    std::string st = std::string(s.l("fmStereoPct", 0) >= 50 ? " ST" : "") +
                     (!trim(s.str("rdsPi")).empty() && s.b("rdsSync") ? " RDS" : "");
    std::string level = measuring ? "\xE2\x80\xA6" : quiet < QUIET_NOISE_DB ? meter(quiet) + " noise"
                        : quiet < QUIET_WEAK_DB ? meter(quiet) + " weak" + st : meter(quiet) + st;
    bool overload = s.f("peakDbfs", -99) > OVERLOAD_DBFS;
    Color c = measuring ? OFF : quiet < QUIET_NOISE_DB ? LOST : (quiet < QUIET_WEAK_DB || overload) ? HOLDING : GOOD;
    return {c, "Analog FM  " + level + "  (" + hdInBrackets(s) + ")"};
}

std::string cpuLine(const Sig &) { return ""; }   // a PC has no phone-style big/little cores to report

using Rows = std::vector<std::pair<std::string, std::string>>;

Rows stationRow(const Sig &s, int freqHz) {
    std::vector<std::string> fromRds;
    for (const std::string &n : {trim(s.str("rdsCall")), trim(s.str("hdSameCall"))})
        if (!n.empty()) fromRds.push_back(n);
    std::vector<std::string> names;
    if (!(s.otherHd() && s.analog()) && !s.station().empty()) names.push_back(s.station());
    for (auto &n : fromRds) names.push_back(n);
    for (const std::string &name : names) {
        std::vector<Station> holder;
        const Station *st = stations::find(freqHz, name, holder);
        if (!st) continue;
        std::string facts;
        auto add = [&](const std::string &x) { if (!x.empty()) facts += (facts.empty() ? "" : ", ") + x; };
        add(st->power());
        add(st->kind());
        if (st->haatM > 0) add(fmt("HAAT %d m", st->haatM));
        if (st->directional == "d") add("directional by day");
        else if (st->directional == "n") add("directional at night");
        else if (st->directional == "dn") add("directional");
        if (st->hd) add("HD");
        return {{"Station", st->call + " \xC2\xB7 " + st->place() + (facts.empty() ? "" : "\n" + facts)}};
    }
    for (const std::string &name : fromRds) {
        Station st;
        if (!stations::elsewhere(freqHz, name, st)) continue;
        return {{"Station", st.call + " \xC2\xB7 " + st.place() + "\nlicensed on " + band::bandText(st.freqHz) + " - relayed here"}};
    }
    std::string maybe = trim(s.str("rdsCallMaybe"));
    if (!maybe.empty()) {
        Station st;
        std::string where = stations::elsewhere(freqHz, maybe, st)
                                ? "which is licensed on " + band::bandText(st.freqHz) + " (" + st.place() + ")"
                                : "which is not in the station list";
        return {{"Station", "unconfirmed: the RDS code spells " + maybe + ",\n" + where}};
    }
    return {};
}

std::string hdDecode(const Sig &s) {
    if (!s.b("hdOn")) return "off";
    if (s.str("hdSearch") == "rest") return "resting";
    return fmt("%lld %%", s.l("hdLoadPct", 0));
}

Rows analogRow(const Sig &s) {
    if (!s.b("demodOn")) return {{"Analog", "off (Digital only)"}};
    if (s.am()) {
        std::string first;
        if (!s.b("amLocked") && s.l("tunedMs", 0) < AM_ACQUIRE_MS) first = "looking for the carrier\xE2\x80\xA6";
        else if (!s.b("amLocked")) first = "no carrier" + (s.f("amCarrierDb", -99) > -98 ? fmt(" (level %.1f dBFS)", s.f("amCarrierDb", -99)) : "");
        else first = (s.f("amSnrDb", -99) <= -98 ? std::string("C/N measuring\xE2\x80\xA6") : fmt("C/N %.1f dB", s.f("amSnrDb", -99))) +
                     (s.f("amCarrierDb", -99) > -98 ? fmt(", carrier %.1f dBFS", s.f("amCarrierDb", -99)) : "") +
                     fmt(", offset %.0f Hz", s.f("amOffsetHz", 0));
        std::string stereo;
        if (s.str("fmStereoMode") == "mono") stereo = "mono (always mono)";
        else if (s.b("hdOn") && s.synced()) stereo = "mono (C-QUAM is off while the HD is in sync)";
        else if (!s.b("amPilot")) stereo = "mono (no C-QUAM pilot)";
        else if (s.l("amStereoPct", 0) == 0) stereo = fmt("mono (blended - noisy), pilot %.1f %%", s.f("amPilotPct", 0));
        else stereo = fmt("C-QUAM stereo %lld %%, pilot %.1f %%", s.l("amStereoPct", 0), s.f("amPilotPct", 0));
        std::string bw = fmt(", audio %.0f kHz", s.l("amBandwidth", 5000) / 1000.0) +
                         (s.l("amDeemph", 1) != 0 ? ", NRSC de-emphasis" : ", flat (no de-emphasis)");
        return {{"Analog", first + "\n" + stereo + bw + "\n" + fmt("demod %lld %% CPU, HD decode ", s.l("fmLoadPct", 0)) + hdDecode(s)}};
    }
    float q = s.f("fmQuietDb", -99), c = s.f("fmCarrierDb", -99);
    std::string quiet = q <= -98 ? "quieting measuring\xE2\x80\xA6" : fmt("quieting %.1f dB", -q);
    std::string carrier = c <= -98 ? quiet + ", carrier measuring\xE2\x80\xA6" : quiet + fmt(", carrier %.1f dB", c);
    std::string stereo;
    if (s.str("fmStereoMode") == "mono") stereo = "mono (always mono)";
    else if (!s.b("fmPilot")) stereo = "mono (no pilot)";
    else if (s.l("fmStereoPct", 0) == 0) stereo = fmt("mono (blended - noisy), pilot %.1f %%", s.f("fmPilotPct", 0));
    else stereo = fmt("stereo %lld %%, pilot %.1f %%", s.l("fmStereoPct", 0), s.f("fmPilotPct", 0));
    return {{"Analog", carrier + fmt(", offset %d Hz\n", (int)s.f("fmOffsetHz", 0)) + stereo + "\n" +
                           fmt("demod %lld %% CPU, HD decode ", s.l("fmLoadPct", 0)) + hdDecode(s) + cpuLine(s)}};
}

Rows rdsRow(const Sig &s) {
    if (s.am() || !s.b("demodOn")) return {};
    std::string pi = trim(s.str("rdsPi"));
    if (pi.empty()) return {{"RDS", s.b("rdsSync") ? "found, identifying\xE2\x80\xA6" : "none (yet)"}};
    std::string call = trim(s.str("rdsCall"));
    std::string who = call.empty() ? "PI " + pi : call + " (PI " + pi + ")";
    std::string type = ptyName((int)s.l("rdsPty", -1));
    std::string text = who + (type.empty() ? "" : ", " + type) +
                       (s.b("rdsSync") ? fmt(", errors %lld %%", s.l("rdsBler", 100)) : ", lost");
    std::string ps = trim(s.str("rdsPs")), psText = trim(s.str("rdsPsText")), title = trim(s.str("rdsTitle")),
                artist = trim(s.str("rdsArtist")), rt = trim(s.str("rdsRt"));
    if (!ps.empty()) text += "\nPS \"" + ps + "\"" + (s.l("rdsPsChanges", 0) >= 3 ? " (scrolling)" : "");
    if (!psText.empty()) text += "\nPS scrolls: " + psText;
    if (!title.empty()) text += "\n\xE2\x99\xAA " + title + (artist.empty() ? "" : " \xE2\x80\x93 " + artist);
    if (!rt.empty()) text += "\n" + rt;
    return {{"RDS", text}};
}

Rows blendRow(const Sig &s) {
    if (!s.autoMode()) return {};
    std::string st = s.blendState(), what;
    long long since = s.l("blendSinceSecs", 0);
    if (st == "sub") what = "sub-channel plays live (no analog to blend with)";
    else if (st == "alone") {
        if (s.b("blendSilent")) what = fmt("waiting for the HD (silence) %lld s", s.l("blendSilentSecs", 0));
        else if (s.ownAnalogBack())
            what = fmt("HD alone for %lld s - another station was heard here less than a minute ago (silence in dropouts)", since);
        else what = fmt("HD alone for %lld s - the analog here is another station (silence in dropouts)", since);
    } else if (st == "hd") what = std::string("HD") + (s.b("blendAligned") ? "" : " (unaligned)") + fmt(" for %lld s", since);
    else if (st == "toHd") what = "fading to HD";
    else if (st == "toAnalog") what = "fading to analog";
    else what = std::string("analog") + (s.b("blendMismatch") ? " - the HD is another station's" : "");
    float match = s.f("blendMatchDb", 0);
    std::string level = match != 0 ? fmt(", analog %+.1f dB", match) : "";
    std::string history;
    if (s.l("blendToHd", 0) != 0) {
        history = fmt("\nto HD %lld\xC3\x97, to analog %lld\xC3\x97", s.l("blendToHd", 0), s.l("blendToAnalog", 0));
        if (s.l("blendSilences", 0) > 0) history += fmt(", to silence %lld\xC3\x97", s.l("blendSilences", 0));
        history += fmt(", HD %lld s in all", s.l("blendHdSecs", 0));
        if (!s.str("blendReason").empty()) history += ", last: " + s.str("blendReason");
    }
    return {{"Blend", what + level + history}};
}

Rows alignRow(const Sig &s) {
    std::string text, st = s.alignState();
    long long count = s.l("alignCount", 0), tries = s.l("alignTries", 0), jumps = s.l("alignJumps", 0);
    if (!s.b("demodOn")) text = "off (Digital only)";
    else if (st == "waiting") text = "waiting for HD audio";
    else if (st == "nomatch") text = fmt("no match (r %.2f, %lld tries)", s.f("alignCorr", 0), tries);
    else if (count == 0) text = "measuring\xE2\x80\xA6";
    else {
        float ms = s.f("alignMs", 0);
        std::string lag = ms >= 0 ? fmt("analog lags HD1 by %.1f ms", ms) : fmt("analog is %.1f ms AHEAD of HD1", -ms);
        text = fmt("%s (r %.2f), analog %+.1f dB%s\n%lld measurement%s of %lld%s", lag.c_str(), s.f("alignCorr", 0),
                   s.f("alignGainDb", 0), st == "ok" ? "" : " (unconfirmed)", count, count == 1 ? "" : "s", tries,
                   jumps > 0 ? fmt(", changed %lld\xC3\x97", jumps).c_str() : "");
    }
    std::string rdsPi = trim(s.str("rdsPi")), sameCall = trim(s.str("hdSameCall")),
                otherName = trim(s.str("hdOtherName")), ownPi = trim(s.str("hdOwnPi"));
    std::string call = !sameCall.empty() ? sameCall : !rdsPi.empty() ? "PI " + rdsPi : "RDS";
    std::string hdName = !otherName.empty() ? otherName : !s.station().empty() ? s.station() : "the HD station";
    std::string ownCode = ownPi.empty() ? "" : " (" + ownPi + ")";
    bool foreign = !rdsPi.empty() && upper(rdsPi) != upper(ownPi);
    std::string same;
    if (s.ownAnalogBack())
        same = "\ntwo stations share this frequency - the analog is " + hdName + "'s own again" + ownCode +
               "; the HD plays alone until the other station has been gone for a minute";
    else if (s.hdContest() == 2)
        same = "\nthe analog here is " + (sameCall.empty() ? std::string("another station") : sameCall) +
               (foreign ? " (RDS code " + rdsPi + ")" : "") + " - not " + hdName + ownCode;
    else if (s.hdContest() == 1 && s.hdSame() != 2)
        same = (foreign ? "\nRDS code " + rdsPi + " is not " + hdName + "'s" + ownCode
                        : "\nanother RDS code than " + hdName + "'s" + ownCode + " was received") + " - checking the audio\xE2\x80\xA6";
    else if (s.hdSame() == 1) same = "\nsame station (RDS " + call + " = HD " + s.station() + ")";
    else if (s.hdSame() == 2) {
        if (st == "ok") same = "\nRDS says " + call + ", HD says " + otherName + " - but the audio matches";
        else if (st == "nomatch") same = "\nHD = " + otherName + ", another station than " + call + " (RDS)";
        else same = "\nRDS says " + call + ", HD says " + otherName + " - checking the audio\xE2\x80\xA6";
    }
    std::string corrected = s.autoMode() ? " - corrected" : "";
    std::string clock, cs = s.str("clockState");
    if (!(s.am() || !s.b("demodOn"))) {
        if (cs == "measured") clock = fmt("\ndongle clock %+.1f ppm (measured)", s.f("clockPpm", 0)) + corrected;
        else if (cs == "guess") clock = fmt("\ndongle clock about %+.0f ppm (from the tuning offset)", s.f("clockPpm", 0)) + corrected;
    }
    std::string unused;
    if (!clock.empty()) {
        if (s.str("clockUnused") == "audio")
            unused = fmt("\naudio measurement %+.0f ppm not used: too far from the tuning offset", s.f("clockUnusedPpm", 0));
        else if (s.str("clockUnused") == "tuning")
            unused = fmt("\ntuning offset %+.0f ppm not used: the audio measurements disagree", s.f("clockUnusedPpm", 0));
    }
    return {{"Alignment", text + same + clock + unused}};
}

Rows watchdogRow(const Sig &s, const GainKeeper *k) {
    if (!k || !s.b("gainAuto") || s.f("gainDb") < 0) return {};
    std::string changes;
    if (k->lastTo < 0) changes = "no changes";
    else {
        long long ago = (nowMs() - k->lastAtMs) / 1000;
        std::string agoText = ago < 120 ? fmt("%lld s ago", ago) : fmt("%lld min ago", ago / 60);
        changes = fmt("\xE2\x86\x93%d \xE2\x86\x91%d \xC2\xB7 last %s %.1f \xE2\x86\x92 %.1f dB, %s", k->downs, k->ups,
                      k->lastTo < k->lastFrom ? "\xE2\x86\x93" : "\xE2\x86\x91", k->lastFrom / 10.0, k->lastTo / 10.0, agoText.c_str());
    }
    std::string tries = k->tryingTo() >= 0 ? fmt("trying %.1f dB\xE2\x80\xA6", k->tryingTo() / 10.0)
                                           : fmt("MER tries %d, kept %d", k->tries, k->kept);
    return {{"Watchdog", changes + "\n" + tries}};
}
}  // namespace

std::string Weak::reason(const Sig &s) {
    long long now = nowMs();
    long long crc = s.l("crcErrors", 0);
    if (crc > lastCrc) crcAtMs = now;
    else if (crc < lastCrc) crcAtMs = -1;
    lastCrc = crc;
    if (!s.synced() || s.holding()) return "";
    if (crcAtMs >= 0 && now - crcAtMs < WEAK_HOLD_MS) return "audio errors";
    long long noAudio = s.am() ? WEAK_NO_AUDIO_AM_MS : WEAK_NO_AUDIO_MS;
    long long syncMs = s.l("syncMs");
    if (s.l("audioMs") < 0 && syncMs >= 0 && s.l("tunedMs", 0) - syncMs > noAudio) return "no audio yet";
    return "";
}

Line signalLine(bool running, const Sig &s, const std::string &weak) {
    Line line;
    std::string bs = s.blendState();
    if (!running) line = {OFF, "Radio off"};
    else if (s.autoMode() && bs == "toHd") line = {GOOD, "blending \xE2\x86\x92 HD"};
    else if (s.autoMode() && bs == "toAnalog") line = {HOLDING, "blending \xE2\x86\x92 analog  (" + s.str("blendReason") + ")"};
    else if (s.autoMode() && bs == "alone" && s.b("blendSilent"))
        line = {LOST, fmt("HD dropout - silence (%lld s)", s.l("blendSilentSecs", 0))};
    else if (s.analog()) line = analogLine(s);
    else if (!s.everSynced()) line = {OFF, fmt("Searching for HD\xE2\x80\xA6 %.1f s", s.l("tunedMs", 0) / 1000.0)};
    else if (!s.synced()) line = {LOST, "HD signal lost"};
    else {
        Color c = (s.holding() || !weak.empty()) ? HOLDING : GOOD;
        std::string w = weak.empty() ? "" : "weak  ";
        if (s.am()) line = {c, fmt("HD %s  %sBER %s", s.str("mode").c_str(), w.c_str(), Sig::percent(s.f("ber")).c_str())};
        else line = {c, fmt("HD %s  %sMER %.1f / %.1f  BER %s", s.str("mode").c_str(), w.c_str(), s.f("merLower", 0),
                            s.f("merUpper", 0), Sig::percent(s.f("ber")).c_str())};
    }
    if (running && s.f("peakDbfs", -99) > OVERLOAD_DBFS) line.text += "  \xE2\x9A\xA0";
    return line;
}

std::string otherHdNotice(const Sig &s) {
    if (!s.otherHd()) return "";
    std::string other = trim(s.str("hdOtherName"));
    std::string hdName = !other.empty() ? other : !s.station().empty() ? s.station() : "HD";
    std::string sameCall = trim(s.str("hdSameCall"));
    if (s.ownAnalogBack() && s.analog()) return "\xE2\x9A\xA0 Two stations share this frequency - click for the HD (" + hdName + ")";
    if (s.ownAnalogBack()) return "\xE2\x9A\xA0 Two stations share this frequency - click for the analog";
    if (s.analog()) return "\xE2\x9A\xA0 The HD here is " + (other.empty() ? s.station() : other) + " (another station) - click to listen";
    if (!sameCall.empty()) return "\xE2\x9A\xA0 The analog here is " + sameCall + " (another station) - click to listen";
    return "\xE2\x9A\xA0 The analog here is another station - click to listen";
}

std::vector<std::pair<std::string, std::string>> details(bool running, const Sig &s, const std::string &weak,
                                                          int freqHz, long long soundMs, const GainKeeper *keeper) {
    if (!running) return {{"Radio", "off"}};
    std::string gain;
    float g = s.f("gainDb");
    if (s.b("direct")) gain = "none - direct sampling (the tuner is bypassed on AM)";
    else if (s.b("gainNoEffect")) gain = "no effect - the dongle is probably in direct sampling";
    else if (g < 0) gain = "tuner AGC";
    else if (!s.b("gainAuto")) gain = fmt("%.1f dB (manual)", g);
    else if (s.b("gainRemembered")) gain = fmt("%.1f dB (auto, remembered)", g);
    else if (s.l("gainSearchMs") >= 0) gain = fmt("%.1f dB (auto, searched in %lld ms)", g, s.l("gainSearchMs"));
    else gain = fmt("%.1f dB (auto)", g);
    std::string overload = (s.b("direct") || s.b("gainNoEffect")) ? OVERLOAD_DIRECT : s.b("gainAuto") ? OVERLOAD_AUTO : OVERLOAD;
    float kbps = s.f("kbps", 0);
    std::string rest;
    if (s.str("hdSearch") == "rest") rest = fmt(" - resting, next look in %lld s", s.l("hdLookIn", 0));
    else if (s.str("hdSearch") == "look") rest = " - looking again\xE2\x80\xA6";
    std::string state, mode = s.str("mode");
    if (s.holding()) state = "holding (sync lost a moment ago)";
    else if (s.locked()) state = "synced" + (mode.empty() ? "" : ", mode " + mode) + (weak.empty() ? "" : " - weak (" + weak + ")");
    else if (s.everSynced()) state = "lost" + rest;
    else if (hdResting(s)) state = "no HD found" + rest;
    else state = "searching";

    Rows rows = stationRow(s, freqHz);
    rows.push_back({"Sync", state});
    rows.push_back({"MER", s.am() ? "- (AM sends none; see BER)" : fmt("lower %.1f dB, upper %.1f dB", s.f("merLower", 0), s.f("merUpper", 0))});
    rows.push_back({"BER", "now " + Sig::percent(s.f("ber"), 4) + ", avg " + Sig::percent(s.f("berAvg"), 4) + "\nmin " +
                               Sig::percent(s.f("berMin"), 4) + ", max " + Sig::percent(s.f("berMax"), 4)});
    rows.push_back({"Audio", (kbps > 0 ? fmt("%.1f kbps", kbps) : std::string("-")) + fmt(", CRC errors %lld", s.l("crcErrors", 0))});
    for (auto &r : analogRow(s)) rows.push_back(r);
    for (auto &r : rdsRow(s)) rows.push_back(r);
    for (auto &r : alignRow(s)) rows.push_back(r);
    for (auto &r : blendRow(s)) rows.push_back(r);
    rows.push_back({"Gain", gain});
    for (auto &r : watchdogRow(s, keeper)) rows.push_back(r);
    float peak = s.f("peakDbfs", -99);
    rows.push_back({"Peak", fmt("%.1f dBFS", peak) + (peak > OVERLOAD_DBFS ? " - " + overload : "")});
    rows.push_back({"Offset", fmt("%d Hz", (int)s.f("offsetHz", 0))});
    rows.push_back({"Tuning", "sync after " + Sig::seconds(s.l("syncMs")) + ", audio after " + Sig::seconds(s.l("audioMs")) +
                                  "\nsound after " + Sig::seconds(soundMs)});
    return rows;
}

std::string ptyName(int pty) {
    static const char *RBDS[] = {"", "News", "Information", "Sports", "Talk", "Rock", "Classic rock", "Adult hits",
                                 "Soft rock", "Top 40", "Country", "Oldies", "Soft", "Nostalgia", "Jazz", "Classical",
                                 "Rhythm and blues", "Soft R&B", "Foreign language", "Religious music", "Religious talk",
                                 "Personality", "Public", "College", "Spanish talk", "Spanish music", "Hip hop", "", "",
                                 "Weather", "Emergency test", "Emergency"};
    return pty >= 0 && pty < 32 ? RBDS[pty] : "";
}

bool isWarning(const std::string &v) {
    return v.find(OVERLOAD) != std::string::npos || v.find(OVERLOAD_AUTO) != std::string::npos ||
           v.find(OVERLOAD_DIRECT) != std::string::npos;
}
}  // namespace sigtext
