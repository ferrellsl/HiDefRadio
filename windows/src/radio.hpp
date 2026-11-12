// The radio: the Windows twin of RadioService.kt. Opens the dongle, tunes (on an engine thread,
// so the window never waits for auto-gain), plays the audio, and runs the same timers:
// the wanted program (5x a second), the gain watchdog (1 s), the analog/HD aligner (2 s),
// traffic and weather (2 s) and saving logos (2 s). The window calls tick() 5 times a second.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "audio.hpp"
#include "gainkeeper.hpp"
#include "mapfeed.hpp"
#include "recorder.hpp"
#include "signal.hpp"
#include "store.hpp"

struct Program {
    int number = 0;           // 0 = HD1
    std::string type, sigName, artist, title;
    std::string hdName() const { return "HD" + std::to_string(number + 1); }
    std::string nameFor(const std::string &station) const;   // as Program.kt
    static std::vector<Program> parseList(const std::string &text);
};

class Radio {
public:
    explicit Radio(Settings &settings);
    ~Radio();

    bool start();                         // open the dongle and play; false: see message()
    void stop();                          // stop and close the dongle
    bool on() const { return radioOn_; }
    std::string message() const;          // why the radio is off ("No RTL-SDR found" ...)

    void tune(int hz, int program = 0);
    void selectProgram(int program);
    int currentProgram() const;
    int freqHz() const { return freqHz_; }
    bool retuning() const { return retuning_; }

    void setPaused(bool p);
    bool paused() const { return player_.paused; }
    void setVolume(int percent);
    void applyAudioSettings();            // after the settings changed
    void applyGain();
    void listenToOtherStation();

    void tick();                          // 5 times a second, from the window

    Sig signal() const;                   // empty while off or retuning
    std::vector<Program> programs() const;
    int pictureGen() const;
    std::string logo(int program) const;
    std::string art(int program) const;   // blocked pictures never come out of here
    long long soundMs() const;
    const GainKeeper *gainKeeper() const { return keeper_.get(); }
    MapFeed &maps() { return maps_; }

    // Recording (the ⏺ button): "" = started, else why not.
    std::string startRecording(const std::string &station);
    void stopRecording() { recorder_.stop(); }
    const Recorder &recorder() const { return recorder_; }
    std::string takeRecordingMessage() { return recorder_.takeMessage(); }

private:
    void post(std::function<void()> f);
    void engineLoop();
    void restartStream(int program);
    void startStream(int hz);
    void wantProgram(int program);
    void checkWanted();
    void watchGain();
    void saveNewLogos();

    Settings &cfg_;
    AudioPlayer player_;
    Recorder recorder_{player_};
    MapFeed maps_;
    std::unique_ptr<GainKeeper> keeper_;

    std::atomic<bool> radioOn_{false};
    std::atomic<bool> retuning_{false};
    std::atomic<int> freqHz_{0};
    std::atomic<int> retuneSeq_{0};
    std::atomic<bool> gainBusy_{false};
    std::atomic<long long> tuneStartMs_{0};
    mutable std::mutex msgMutex_;
    std::string message_;

    int wantedProgram_ = -1;
    long long wantedSinceMs_ = 0;
    long long nextWantedMs_ = 0, nextGainMs_ = 0, nextAlignMs_ = 0, nextMapMs_ = 0, nextLogoMs_ = 0;
    long long lostSinceMs_ = 0;
    int savedPictureGen_ = -1;
    std::string savedOwnerKey_;

    std::thread engine_;
    std::mutex qm_;
    std::condition_variable qcv_;
    std::deque<std::function<void()>> queue_;
    bool stopEngine_ = false;
};
