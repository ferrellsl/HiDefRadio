#include "audio.hpp"
#include "engine.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <vector>
#include <algorithm>

extern "C" int __android_log_print(int, const char *, const char *, ...);
#define ALOG(...) __android_log_print(4, "Audio", __VA_ARGS__)

static const int PREBUFFER_VALUES = AudioPlayer::SAMPLE_RATE * 2 / 4;   // 0.25 s, as on Android

void AudioPlayer::start() {
    if (running_) return;
    running_ = true;
    underruns = 0;
    startedAtMs = 0;
    thread_ = std::thread([this] { loop(); });
}

void AudioPlayer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    playing = false;
}

void AudioPlayer::loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

    IMMDeviceEnumerator *en = nullptr;
    IMMDevice *dev = nullptr;
    IAudioClient *client = nullptr;
    IAudioRenderClient *render = nullptr;
    UINT32 bufferFrames = 0;
    bool started = false;
    std::vector<int16_t> chunk;

    // (Re)opens the default device; false = no device now (try again later).
    auto open = [&]() -> bool {
        if (render) { render->Release(); render = nullptr; }
        if (client) { client->Stop(); client->Release(); client = nullptr; }
        if (dev) { dev->Release(); dev = nullptr; }
        started = false;
        if (!en && FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           __uuidof(IMMDeviceEnumerator), (void **)&en))) return false;
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return false;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client))) return false;
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 2;
        wf.nSamplesPerSec = SAMPLE_RATE;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = 4;
        wf.nAvgBytesPerSec = SAMPLE_RATE * 4;
        const REFERENCE_TIME quarterSecond = 2500000;   // 100 ns units
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                        quarterSecond, 0, &wf, nullptr);
        if (FAILED(hr)) { ALOG("IAudioClient::Initialize failed 0x%08lx", (unsigned long)hr); return false; }
        client->GetBufferSize(&bufferFrames);
        if (FAILED(client->GetService(__uuidof(IAudioRenderClient), (void **)&render))) return false;
        ALOG("WASAPI open: buffer %u frames", bufferFrames);
        return true;
    };

    bool ok = open();
    // Pre-buffer, like Android: wait for 0.25 s of audio before starting.
    while (running_ && eng::audioBuffered() < PREBUFFER_VALUES) Sleep(20);

    while (running_) {
        if (!ok) {
            Sleep(500);
            ok = open();
            continue;
        }
        UINT32 padding = 0;
        HRESULT hr = client->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) { ok = open(); continue; }   // headphones unplugged etc.
        UINT32 room = bufferFrames - padding;
        // Keep about 100 ms queued in the device; the engine's ring holds the rest.
        const UINT32 target = SAMPLE_RATE / 10;
        if (padding >= target || room == 0) { Sleep(10); continue; }
        UINT32 want = std::min(room, target - padding);
        chunk.resize(want * 2);
        int n = eng::readAudio(chunk.data(), (int)want * 2, started ? 30 : 200);
        if (n <= 0) {
            underruns++;
            continue;          // the device plays silence by itself when it runs dry
        }
        UINT32 frames = (UINT32)n / 2;
        BYTE *out = nullptr;
        if (FAILED(render->GetBuffer(frames, &out))) { ok = open(); continue; }
        float v = paused ? 0.0f : volume.load();
        int16_t *dst = reinterpret_cast<int16_t *>(out);
        if (v >= 0.999f) memcpy(dst, chunk.data(), frames * 4);
        else for (UINT32 i = 0; i < frames * 2; i++) dst[i] = (int16_t)(chunk[i] * v);
        render->ReleaseBuffer(frames, 0);
        if (!started) {
            client->Start();
            started = true;
            playing = true;
            startedAtMs = (long long)GetTickCount64();
            ALOG("Audio playback started");
        }
    }
    if (render) render->Release();
    if (client) { client->Stop(); client->Release(); }
    if (dev) dev->Release();
    if (en) en->Release();
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}
