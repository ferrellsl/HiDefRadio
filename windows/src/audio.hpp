// Plays the engine's audio through WASAPI (shared mode): the Windows twin of AudioPlayer.kt.
// A thread pulls 44,100 Hz 16-bit stereo out of the engine's ring and hands it to the
// default output device; Windows converts it to the device's own rate.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

class AudioPlayer {
public:
    static const int SAMPLE_RATE = 44100;
    void start();
    void stop();
    std::atomic<float> volume{1.0f};
    std::atomic<bool> paused{false};        // keeps decoding, plays silence (instantly live again)
    std::atomic<bool> playing{false};       // pre-buffering done, sound going out
    std::atomic<int> underruns{0};
    std::atomic<long long> startedAtMs{0};  // GetTickCount64() when sound started, 0 = not yet
    // The recording's tap: every chunk of the radio's audio (values = 2 per stereo frame) as it
    // goes to the speaker, before the volume, paused or not. It must copy and return at once.
    void setTap(std::function<void(const int16_t *, int)> tap) {
        std::lock_guard<std::mutex> l(tapMutex_);
        tap_ = std::move(tap);
    }
    ~AudioPlayer() { stop(); }
private:
    void loop();
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex tapMutex_;
    std::function<void(const int16_t *, int)> tap_;
};
