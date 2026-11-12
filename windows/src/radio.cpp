#include "radio.hpp"
#include "engine.hpp"
#include "stations.hpp"
#include "util.hpp"
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <regex>

extern "C" int __android_log_print(int, const char *, const char *, ...);
#define RLOG(...) __android_log_print(4, "HiDefRadio", __VA_ARGS__)

static const long long WANTED_GRACE_MS = 5000;     // after the station's audio started
static const long long WANTED_TIMEOUT_MS = 30000;  // in any case
static const long long REMEMBER_GAIN_AFTER_MS = 20000;

// ------------------------------------------------------------------ Program (Program.kt)

static bool genericName(const std::string &s) {
    static const std::regex re("(MPS|SPS\\d|HD[- ]?\\d)", std::regex::icase);
    return std::regex_match(s, re);
}
static std::string callSign(std::string s) {
    s = upper(trim(s));
    if (endsWith(s, "FM")) s.resize(s.size() - 2);
    while (!s.empty() && (s.back() == '-' || s.back() == ' ')) s.pop_back();
    return s;
}
std::string Program::nameFor(const std::string &station) const {
    std::string sig = trim(sigName);
    bool same = !trim(station).empty() && callSign(sig) == callSign(station);
    if (!sig.empty() && !genericName(sig) && !same) return sig;
    if (!trim(type).empty() && type != "None" && type != "Unknown") return type;
    return "";
}
std::vector<Program> Program::parseList(const std::string &text) {
    std::vector<Program> out;
    for (const std::string &line : split(text, '\n')) {
        if (trim(line).empty()) continue;
        std::vector<std::string> f = split(line, '\t');
        if (f.empty() || f[0].empty() || !isdigit((unsigned char)f[0][0])) continue;
        Program p;
        p.number = atoi(f[0].c_str());
        if (f.size() > 1) p.type = f[1];
        if (f.size() > 2) p.sigName = f[2];
        if (f.size() > 3) p.artist = f[3];
        if (f.size() > 4) p.title = f[4];
        out.push_back(p);
    }
    return out;
}

// ------------------------------------------------------------------ Radio

Radio::Radio(Settings &settings) : cfg_(settings) {
    freqHz_ = cfg_.freqHz;
    engine_ = std::thread([this] { engineLoop(); });
}

Radio::~Radio() {
    stop();
    { std::lock_guard<std::mutex> l(qm_); stopEngine_ = true; }
    qcv_.notify_one();
    if (engine_.joinable()) engine_.join();
}

void Radio::post(std::function<void()> f) {
    { std::lock_guard<std::mutex> l(qm_); queue_.push_back(std::move(f)); }
    qcv_.notify_one();
}

void Radio::engineLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> l(qm_);
            qcv_.wait(l, [&] { return stopEngine_ || !queue_.empty(); });
            if (stopEngine_ && queue_.empty()) break;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        job();
    }
    CoUninitialize();
}

std::string Radio::message() const {
    std::lock_guard<std::mutex> l(msgMutex_);
    return message_;
}

bool Radio::start() {
    if (radioOn_) return true;
    std::string report = eng::openDongle(cfg_.device);
    RLOG("%s", report.c_str());
    if (!eng::isDongleOpen()) {
        std::lock_guard<std::mutex> l(msgMutex_);
        message_ = "No RTL-SDR receiver found. Plug it in (it needs the WinUSB driver, e.g. from Zadig), then press Start.";
        if (report.find("FAILED") != std::string::npos)
            message_ = "The RTL-SDR could not be opened (" + trim(report) +
                       "). Is another program using it, and does it have the WinUSB driver (Zadig)?";
        return false;
    }
    {
        std::lock_guard<std::mutex> l(msgMutex_);
        message_.clear();
    }
    keeper_.reset(new GainKeeper(eng::gainSteps()));
    radioOn_ = true;
    player_.volume = cfg_.volume / 100.0f;
    freqHz_ = cfg_.freqHz;
    restartStream(cfg_.program);
    wantProgram(cfg_.program);
    long long now = nowMs();
    nextGainMs_ = now + 1000;
    nextAlignMs_ = now + 2000;
    nextMapMs_ = now + 2000;
    nextLogoMs_ = now + 2000;
    lostSinceMs_ = 0;
    return true;
}

void Radio::stop() {
    if (!radioOn_) return;
    recorder_.stop();                             // a recording is one station - saved now
    radioOn_ = false;
    retuneSeq_++;
    wantedProgram_ = -1;
    std::mutex done;
    std::condition_variable cv;
    bool finished = false;
    post([&] {
        player_.stop();
        eng::stopStream();
        eng::closeDongle();
        std::lock_guard<std::mutex> l(done);
        finished = true;
        cv.notify_one();
    });
    std::unique_lock<std::mutex> l(done);
    cv.wait_for(l, std::chrono::seconds(5), [&] { return finished; });
    retuning_ = false;
}

// Tunes to hz: gain, sample rate, a new nrsc5 decoder, streaming thread (on the engine thread).
void Radio::startStream(int hz) {
    int gain = cfg_.gainTenths;
    int hint = gain < 0 ? gainmem::get(hz) : -1;
    eng::setAudioSource(cfg_.audioSource);
    eng::setBlendUnmatched(cfg_.unmatchedPlaysHd);
    eng::setStereoMode(cfg_.stereoMode);
    eng::setDeemphasis(75);                          // the Americas
    eng::setAmBandwidth(cfg_.amBandwidthHz);
    eng::setAmDeemphasis(cfg_.amDeemphasis);
    // Who is licensed on this frequency (the built-in FCC list), FM only.
    std::string calls = band::isAm(hz) ? "" : stations::callsOn(hz);
    eng::setChannelCalls(calls, !band::isAm(hz));
    logocache::Owner owner;
    if (!band::isAm(hz) && logocache::owner(hz, owner))
        eng::setHdOwner(owner.name, owner.pi.empty() ? -1 : (int)strtol(owner.pi.c_str(), nullptr, 16));
    else
        eng::setHdOwner("", -1);
    savedOwnerKey_.clear();
    std::string info = eng::startStream(hz, gain, hint);
    RLOG("%s", info.c_str());
}

void Radio::restartStream(int program) {
    recorder_.stop();                             // a recording is one station - saved now
    int seq = ++retuneSeq_;
    int hz = freqHz_;
    retuning_ = true;
    tuneStartMs_ = nowMs();
    post([this, seq, hz, program] {
        if (seq != retuneSeq_) return;            // a newer tune is waiting: skip this one
        player_.stop();
        eng::stopStream();
        if (seq != retuneSeq_) return;
        startStream(hz);
        if (program > 0) eng::setProgram(program);
        player_.start();
        if (seq == retuneSeq_) retuning_ = false;
    });
}

void Radio::tune(int hz, int program) {
    if (player_.paused) setPaused(false);         // changing station = you want to hear it
    cfg_.freqHz = hz;
    cfg_.program = program;
    bool same = hz == freqHz_ && (retuning_ || eng::isStreaming());
    freqHz_ = hz;
    if (!radioOn_) return;
    if (!same) {
        RLOG("Tuning to %d Hz", hz);
        restartStream(program);
    }
    wantProgram(program);
}

void Radio::wantProgram(int program) {
    wantedProgram_ = program;
    wantedSinceMs_ = nowMs();
    checkWanted();
}

// Is the wanted program on the air yet? Yes -> switch. Gives up (stays on HD1) once the
// station has played WANTED_GRACE_MS without it, or after WANTED_TIMEOUT_MS anyway.
void Radio::checkWanted() {
    int p = wantedProgram_;
    if (p < 0 || retuning_) return;
    for (const Program &pr : programs())
        if (pr.number == p) {
            eng::setProgram(p);
            wantedProgram_ = -1;
            RLOG("Wanted program HD%d is on the air - switched", p + 1);
            return;
        }
    Sig s = signal();
    long long waited = nowMs() - wantedSinceMs_;
    long long audioMs = s.l("audioMs");
    long long playingFor = audioMs >= 0 ? s.l("tunedMs", 0) - audioMs : 0;
    if (playingFor > WANTED_GRACE_MS || waited > WANTED_TIMEOUT_MS) {
        if (eng::program() == p) eng::setProgram(0);
        RLOG("HD%d not on the air - staying on HD%d", p + 1, eng::program() + 1);
        wantedProgram_ = -1;
    }
}

void Radio::selectProgram(int program) {
    if (!radioOn_ || program == currentProgram()) return;
    if (player_.paused) setPaused(false);
    wantedProgram_ = -1;
    eng::setProgram(program);
    cfg_.program = program;
}

int Radio::currentProgram() const {
    return wantedProgram_ >= 0 ? wantedProgram_ : eng::program();
}

void Radio::setPaused(bool p) { player_.paused = p; }
void Radio::setVolume(int percent) {
    cfg_.volume = percent;
    player_.volume = percent / 100.0f;
}

void Radio::applyAudioSettings() {
    if (!radioOn_) return;
    Settings c = cfg_;
    post([c] {
        eng::setAudioSource(c.audioSource);
        eng::setBlendUnmatched(c.unmatchedPlaysHd);
        eng::setStereoMode(c.stereoMode);
        eng::setAmBandwidth(c.amBandwidthHz);
        eng::setAmDeemphasis(c.amDeemphasis);
    });
}

void Radio::applyGain() {
    if (!radioOn_) return;
    if (cfg_.gainTenths < 0) restartStream(currentProgram());   // back to auto: measure again
    else {
        int g = cfg_.gainTenths;
        post([g] { eng::setGain(g); });
    }
}

// The HD here is another station's: play the other one of the two until the next tune.
void Radio::listenToOtherStation() {
    if (!radioOn_ || retuning_) return;
    int source = signal().analog() ? 0 : 1;
    post([source] { eng::setAudioSource(source); });
}

void Radio::tick() {
    if (!radioOn_) return;
    long long now = nowMs();
    if (wantedProgram_ >= 0 && now >= nextWantedMs_) {
        nextWantedMs_ = now + 200;
        checkWanted();
    }
    if (now >= nextGainMs_) {
        nextGainMs_ = now + 1000;
        watchGain();
        recorder_.tick();
    }
    if (now >= nextAlignMs_) {
        nextAlignMs_ = now + 2000;
        if (!retuning_ && eng::isStreaming()) {
            int seq = retuneSeq_;
            post([this, seq] { if (seq == retuneSeq_) eng::measureAlignment(); });
        }
    }
    if (now >= nextMapMs_) {
        nextMapMs_ = now + 2000;
        maps_.poll();
    }
    if (now >= nextLogoMs_) {
        nextLogoMs_ = now + 2000;
        if (!retuning_) saveNewLogos();
    }
    // The dongle went away (unplugged, or the USB connection dropped): the stream stops by
    // itself. Say so and stop - no automatic retries (a flapping USB port must not be hammered).
    if (!retuning_ && !eng::isStreaming()) {
        if (lostSinceMs_ == 0) lostSinceMs_ = now;
        else if (now - lostSinceMs_ > 3000) {
            RLOG("Stream stopped by itself - receiver lost");
            stop();
            std::lock_guard<std::mutex> l(msgMutex_);
            message_ = "The receiver stopped sending (unplugged?). Press Start to try again.";
        }
    } else lostSinceMs_ = 0;
}

void Radio::watchGain() {
    float peak = eng::takePeakMax();             // always take it, so each reading = 1 second
    if (!keeper_ || gainBusy_) return;
    long long now = nowMs();
    Sig s = signal();
    float gainDb = s.f("gainDb");
    if (retuning_ || !s.b("gainAuto") || gainDb < 0 || !eng::isStreaming()) {
        keeper_->restart(now);
        return;
    }
    int gain = (int)lround(gainDb * 10);
    int newGain = keeper_->tick(now, gain, peak, s.locked(), (int)s.l("merCount", 0), s.f("merLower", 0), s.f("merUpper", 0));
    if (newGain != GainKeeper::NO_CHANGE) {
        RLOG("Gain watchdog: peak %.1f dBFS, MER %.1f / %.1f -> %.1f dB%s", peak, s.f("merLower", 0),
             s.f("merUpper", 0), newGain / 10.0, keeper_->tryingTo() >= 0 ? " (trying)" : "");
        gainBusy_ = true;
        int seq = retuneSeq_;
        post([this, seq, newGain] {
            if (seq == retuneSeq_) eng::setAutoGain(newGain);
            gainBusy_ = false;
        });
        return;
    }
    if (s.locked() && keeper_->stableForMs(now) >= REMEMBER_GAIN_AFTER_MS) gainmem::set(freqHz_, gain);
}

// Every program's logo goes into the logo cache with its owner, so it shows at once next time.
void Radio::saveNewLogos() {
    Sig s = signal();
    std::string owner = trim(s.str("hdOtherName"));
    if (owner.empty()) owner = s.station();
    std::string ownPi = s.am() ? "" : trim(s.str("hdOwnPi"));
    int gen = pictureGen();
    if (gen != savedPictureGen_) {
        savedPictureGen_ = gen;
        for (const Program &p : programs()) {
            std::string bytes = logo(p.number);
            if (!bytes.empty()) logocache::save(freqHz_, p.number, bytes, owner, ownPi);
        }
    }
    std::string key = fmt("%d/", (int)freqHz_) + owner + "/" + ownPi;
    if (!owner.empty() && key != savedOwnerKey_) {
        savedOwnerKey_ = key;
        logocache::setOwner(freqHz_, owner, ownPi);
    }
}

Sig Radio::signal() const {
    if (!radioOn_ || retuning_) return Sig();
    return Sig::parse(eng::signal());
}
std::vector<Program> Radio::programs() const {
    if (!radioOn_ || retuning_) return {};
    return Program::parseList(eng::programs());
}
int Radio::pictureGen() const { return eng::pictureGen(); }
std::string Radio::logo(int program) const {
    if (!radioOn_ || retuning_) return {};
    return eng::logo(program);
}
std::string Radio::art(int program) const {
    if (!radioOn_ || retuning_) return {};
    std::string a = eng::art(program);
    if (!a.empty() && artblock::isBlocked(a)) return {};
    return a;
}
std::string Radio::startRecording(const std::string &station) {
    if (!radioOn_ || retuning_ || !eng::isStreaming()) return "Recording starts once the station is playing.";
    std::wstring folder = cfg_.recFolder.empty() ? recordings::defaultFolder() : cfg_.recFolder;
    return recorder_.start(cfg_.recWhat, folder, cfg_.recIqLimitS, freqHz_, station, currentProgram());
}

long long Radio::soundMs() const {
    long long started = player_.startedAtMs;
    return !retuning_ && started > 0 && started >= tuneStartMs_ ? started - tuneStartMs_ : -1;
}
