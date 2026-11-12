// Recording - the ⏺ button. A port of the Android app's Recorder.kt (+ AacWriter.kt):
//  - AUDIO: what you hear (HD, analog or the blend), as AAC-LC 128 kbit/s, 44,100 Hz stereo
//    (the radio's own rate, nothing resampled), about 1 MB a minute. Android writes raw .aac;
//    on Windows it goes into an .m4a file (Media Foundation), which Windows players show with
//    its length and can seek in. The copy is taken where the audio goes to the speaker, so it
//    keeps recording while the sound is paused.
//  - IQ: the raw samples from the dongle, untouched (the engine's iqrecord.hpp), as .cu8 -
//    about 180 MB a minute, so it stops by itself after the IQ limit (Settings).
//  - or both at once.
// A recording ends with ⏺ again, another station, or the radio stopped (and IQ by itself at
// the limit, a full disk, or a disk too slow for 3 MB a second). HD1 -> HD2 does not end it.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "audio.hpp"

class AacFile;

class Recorder {
public:
    enum What { AUDIO = 0, IQ = 1, BOTH = 2 };
    explicit Recorder(AudioPlayer &player) : player_(player) {}
    ~Recorder();

    // Starts recording; "" = started, else the message to show. [station] and [program]
    // (0 = HD1) only go into the file names.
    std::string start(int what, const std::wstring &folder, int iqLimitS, int hz,
                      const std::string &station, int program);
    void stop(bool quiet = false);       // ends everything; the "Saved ..." message comes later
    void tick();                         // once a second: did the IQ end by itself, did the audio fail?
    bool recording() const { return audio_ != nullptr || iqOn_; }
    long long elapsedMs() const;
    bool iqOnly() const { return iqOn_ && !audio_; }
    int iqLimitS() const { return iqLimitS_; }
    std::string takeMessage();           // what was saved, once ("" = nothing new)

private:
    void say(const std::string &text);
    std::string iqLine(long long bytes, int reason);

    AudioPlayer &player_;
    std::shared_ptr<AacFile> audio_;
    std::wstring audioPath_;
    long long audioStartMs_ = 0;
    bool iqOn_ = false;
    std::wstring iqPath_;
    long long iqStartMs_ = 0;
    int iqLimitS_ = 0;
    std::mutex msgMutex_;
    std::string message_;
    std::vector<std::thread> finishers_;   // files being finished; joined before the next and at the end
    void finishLater(std::function<void()> job);
};

namespace recordings {
std::wstring defaultFolder();            // Music\HiDef Radio
std::string sizeText(long long bytes);   // "3.2 MB"
std::string limitText(int seconds);      // "60 s", "5 min"
}
