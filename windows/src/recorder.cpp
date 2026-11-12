#include "recorder.hpp"
#include "engine.hpp"
#include "util.hpp"
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <condition_variable>
#include <deque>
#include <regex>
#include <thread>
#include <vector>

extern "C" int __android_log_print(int, const char *, const char *, ...);
#define RECLOG(...) __android_log_print(4, "Recorder", __VA_ARGS__)

static const int RATE = 44100;
static const int IQ_RATE = 1488375;                // samples a second (native-lib.cpp SAMPLE_RATE)
static const int REASON_BY_YOU = 1, REASON_LIMIT = 2, REASON_WRITE_FAILED = 3, REASON_TOO_SLOW = 4;

// ------------------------------------------------------------------ the AAC file (AacWriter.kt)

// The audio thread hands chunks over (feed: a copy, never waits); a thread of its own gives
// them to Windows' AAC encoder and the MPEG-4 file writer.
class AacFile {
public:
    explicit AacFile(const std::wstring &path) : path_(path) {}
    ~AacFile() { finish(); }

    bool start() {
        thread_ = std::thread([this] { run(); });
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [this] { return ready_ || failed_; });
        return !failed_;
    }
    void feed(const int16_t *v, int n) {
        std::lock_guard<std::mutex> l(m_);
        if (failed_ || closing_) return;
        if (queued_ > (size_t)RATE * 2 * 10) { failed_ = true; cv_.notify_all(); return; }   // 10 s behind: give up
        queue_.emplace_back(v, v + n);
        queued_ += n;
        cv_.notify_all();
    }
    void finish() {
        {
            std::lock_guard<std::mutex> l(m_);
            closing_ = true;
            cv_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
    }
    bool failed() const { return failed_.load(); }
    long long bytes() const {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &a)) return 0;
        return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    }

private:
    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION, MFSTARTUP_LITE);
        IMFSinkWriter *writer = nullptr;
        DWORD stream = 0;
        bool ok = open(&writer, &stream);
        {
            std::lock_guard<std::mutex> l(m_);
            if (ok) ready_ = true;
            else failed_ = true;
            cv_.notify_all();
        }
        long long frames = 0;
        while (ok) {
            std::vector<int16_t> chunk;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [this] { return closing_ || !queue_.empty(); });
                if (queue_.empty()) break;            // closing, all written
                chunk = std::move(queue_.front());
                queue_.pop_front();
                queued_ -= chunk.size();
            }
            if (!write(writer, stream, chunk, frames)) {
                failed_ = true;
                ok = false;
            }
        }
        if (writer) {
            if (FAILED(writer->Finalize())) failed_ = true;
            writer->Release();
        }
        MFShutdown();
        CoUninitialize();
    }

    bool open(IMFSinkWriter **writer, DWORD *stream) {
        IMFAttributes *attr = nullptr;
        MFCreateAttributes(&attr, 1);
        attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        HRESULT hr = MFCreateSinkWriterFromURL(path_.c_str(), nullptr, attr, writer);
        attr->Release();
        if (FAILED(hr)) { RECLOG("MFCreateSinkWriterFromURL failed 0x%08lx", (unsigned long)hr); return false; }
        IMFMediaType *out = nullptr, *in = nullptr;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, RATE);
        out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);   // 128 kbit/s, as on Android
        out->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
        hr = (*writer)->AddStream(out, stream);
        MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, RATE);
        in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, RATE * 4);
        if (SUCCEEDED(hr)) hr = (*writer)->SetInputMediaType(*stream, in, nullptr);
        if (SUCCEEDED(hr)) hr = (*writer)->BeginWriting();
        out->Release();
        in->Release();
        if (FAILED(hr)) {
            RECLOG("AAC encoder setup failed 0x%08lx", (unsigned long)hr);
            (*writer)->Release();
            *writer = nullptr;
            DeleteFileW(path_.c_str());
            return false;
        }
        return true;
    }

    bool write(IMFSinkWriter *writer, DWORD stream, const std::vector<int16_t> &chunk, long long &frames) {
        DWORD bytes = (DWORD)(chunk.size() * 2);
        IMFMediaBuffer *buf = nullptr;
        IMFSample *sample = nullptr;
        if (FAILED(MFCreateMemoryBuffer(bytes, &buf))) return false;
        BYTE *p = nullptr;
        buf->Lock(&p, nullptr, nullptr);
        memcpy(p, chunk.data(), bytes);
        buf->Unlock();
        buf->SetCurrentLength(bytes);
        MFCreateSample(&sample);
        sample->AddBuffer(buf);
        long long n = (long long)chunk.size() / 2;
        sample->SetSampleTime(frames * 10000000LL / RATE);
        sample->SetSampleDuration(n * 10000000LL / RATE);
        frames += n;
        HRESULT hr = writer->WriteSample(stream, sample);
        sample->Release();
        buf->Release();
        return SUCCEEDED(hr);
    }

    std::wstring path_;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::vector<int16_t>> queue_;
    size_t queued_ = 0;
    bool ready_ = false, closing_ = false;
    std::atomic<bool> failed_{false};
};

// ------------------------------------------------------------------ helpers

namespace recordings {
std::wstring defaultFolder() {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &p))) r = std::wstring(p) + L"\\HiDef Radio";
    CoTaskMemFree(p);
    return r;
}
std::string sizeText(long long b) {
    if (b >= 1000000000LL) return fmt("%.2f GB", b / 1e9);
    if (b >= 1000000) return fmt("%.1f MB", b / 1e6);
    if (b >= 1000) return fmt("%.0f kB", b / 1e3);
    return fmt("%lld B", b);
}
std::string limitText(int s) { return s % 60 == 0 && s >= 120 ? fmt("%d min", s / 60) : fmt("%d s", s); }
}  // namespace recordings

// "KRTH-FM" stays, "K-Love 99.5/HD" -> "K-Love_99.5_HD": safe in a file name everywhere.
static std::string clean(const std::string &text) {
    static const std::regex bad("[^A-Za-z0-9.+-]+");
    std::string s = std::regex_replace(trim(text), bad, "_");
    while (!s.empty() && s.front() == '_') s.erase(s.begin());
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s.substr(0, 40);
}

static std::wstring fileName(const std::wstring &path) { return path.substr(path.find_last_of(L"\\/") + 1); }

static long long fileSize(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return 0;
    return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

// ------------------------------------------------------------------ Recorder

Recorder::~Recorder() {
    stop(true);
    for (auto &t : finishers_) if (t.joinable()) t.join();
}

void Recorder::finishLater(std::function<void()> job) {
    // drop the threads that are done (all of them are short: a file being closed)
    for (auto &t : finishers_) if (t.joinable()) t.join();
    finishers_.clear();
    finishers_.emplace_back(std::move(job));
}

std::string Recorder::start(int what, const std::wstring &folder, int iqLimitS, int hz,
                            const std::string &station, int program) {
    if (recording()) return "";
    if (folder.empty()) return "Choose a folder for recordings first (Settings > Recording).";
    makeDirs(folder);
    DWORD attr = GetFileAttributesW(folder.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        return "Couldn't create the recordings folder. Please choose it again in Settings > Recording.";
    long long dummy;
    int dummyReason;
    eng::takeIqRecord(dummy, dummyReason);   // one that ended while nobody looked: free it

    SYSTEMTIME t;
    GetLocalTime(&t);
    std::string time = fmt("%04d-%02d-%02d_%02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string freq = band::isAm(hz) ? band::freqText(hz) + "kHz" : band::freqText(hz) + "MHz";
    bool wantAudio = what != IQ, wantIq = what != AUDIO;
    std::string problem;

    if (wantIq) {
        iqLimitS_ = iqLimitS;
        std::wstring path = folder + L"\\" + widen(fmt("HiDefRadio_%s_%s_%dsps.cu8", time.c_str(), freq.c_str(), IQ_RATE));
        int fd = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY | _O_SEQUENTIAL, _S_IREAD | _S_IWRITE);
        if (fd < 0) problem = "Couldn't open the recording file for writing.";
        else {
            int r = eng::startIqRecord(fd, iqLimitS);    // the engine owns the fd now
            if (r == 0) {
                iqOn_ = true;
                iqPath_ = path;
                iqStartMs_ = nowMs();
            } else {
                DeleteFileW(path.c_str());               // an empty file helps nobody
                problem = r == 1 ? "Recording starts once the station is playing."
                        : r == 3 ? "Not enough free memory for an IQ recording right now."
                                 : "Couldn't open the recording file for writing.";
            }
        }
    }
    if (wantAudio && problem.empty()) {
        std::string tag = clean(station);
        tag = (tag.empty() ? "" : "_" + tag) + (program > 0 ? fmt("_HD%d", program + 1) : "");
        std::wstring path = folder + L"\\" + widen("HiDefRadio_" + time + "_" + freq + tag + ".m4a");
        auto file = std::make_shared<AacFile>(path);
        if (!file->start()) problem = "Couldn't start the AAC encoder for the audio recording.";
        else {
            audio_ = file;
            audioPath_ = path;
            audioStartMs_ = nowMs();
            std::weak_ptr<AacFile> weak = file;
            player_.setTap([weak](const int16_t *v, int n) {
                if (auto f = weak.lock()) f->feed(v, n);
            });
        }
    }
    if (!problem.empty()) {
        stop(true);                                      // the half that did start: removed again
        return problem;
    }
    RECLOG("Recording started: %s %s", narrow(audioPath_).c_str(), iqOn_ ? narrow(iqPath_).c_str() : "");
    return "";
}

std::string Recorder::iqLine(long long bytes, int reason) {
    std::string name = narrow(fileName(iqPath_));
    if (bytes == 0) {
        DeleteFileW(iqPath_.c_str());
        return "The IQ recording couldn't be saved.";
    }
    std::string size = recordings::sizeText(bytes);
    if (reason == REASON_LIMIT) return "IQ recording reached the " + recordings::limitText(iqLimitS_) + " limit. Saved " + size + ": " + name;
    if (reason == REASON_TOO_SLOW) return "IQ recording stopped: the disk couldn't keep up. Saved " + size + ": " + name;
    if (reason == REASON_WRITE_FAILED) return "IQ recording stopped: couldn't write any more (is the disk full?). Saved " + size + ": " + name;
    return "Saved " + size + ": " + name;
}

void Recorder::stop(bool quiet) {
    std::shared_ptr<AacFile> writer = audio_;
    bool wasIq = iqOn_;
    if (!writer && !wasIq) return;
    player_.setTap(nullptr);
    audio_.reset();
    iqOn_ = false;
    std::wstring audioPath = audioPath_, iqPath = iqPath_;
    // Finishing (the last AAC frames, the rest of the IQ ring) takes a moment: not on the
    // window's thread. The message comes through takeMessage().
    finishLater([this, writer, wasIq, quiet, audioPath, iqPath] {
        std::string lines;
        if (writer) {
            writer->finish();
            long long b = writer->bytes();
            if (quiet) DeleteFileW(audioPath.c_str());
            else if (b == 0) { DeleteFileW(audioPath.c_str()); lines += "The audio recording couldn't be saved."; }
            else lines += (writer->failed() ? "Audio recording stopped: couldn't encode or write any more. Saved "
                                            : "Saved ") + recordings::sizeText(b) + ": " + narrow(fileName(audioPath));
        }
        if (wasIq) {
            eng::stopIqRecord();                         // waits until the file is complete
            long long bytes = 0;
            int reason = REASON_BY_YOU;
            if (!eng::takeIqRecord(bytes, reason)) bytes = fileSize(iqPath);
            if (quiet) DeleteFileW(iqPath.c_str());
            else {
                std::string l = iqLine(bytes, reason);
                lines += (lines.empty() ? "" : "\n") + l;
            }
        }
        if (!quiet && !lines.empty()) say(lines);
    });
}

void Recorder::tick() {
    if (iqOn_) {
        long long state = 0, written = 0;
        if (eng::iqRecordState(state, written) && state == 2) {   // ended by itself
            long long bytes = 0;
            int reason = 0;
            if (eng::takeIqRecord(bytes, reason)) {
                iqOn_ = false;
                say(iqLine(bytes, reason) + (audio_ ? "\nAudio keeps recording." : ""));
            }
        }
    }
    if (audio_ && audio_->failed()) {
        std::shared_ptr<AacFile> writer = audio_;
        player_.setTap(nullptr);
        audio_.reset();
        std::wstring path = audioPath_;
        finishLater([this, writer, path] {
            writer->finish();
            say("Audio recording stopped: couldn't encode or write any more. Saved " +
                recordings::sizeText(writer->bytes()) + ": " + narrow(fileName(path)));
        });
    }
}

long long Recorder::elapsedMs() const {
    long long now = nowMs(), start = 0;
    if (audio_) start = audioStartMs_;
    if (iqOn_ && (start == 0 || iqStartMs_ < start)) start = iqStartMs_;
    return start ? now - start : 0;
}

void Recorder::say(const std::string &text) {
    std::lock_guard<std::mutex> l(msgMutex_);
    message_ = message_.empty() ? text : message_ + "\n" + text;
}

std::string Recorder::takeMessage() {
    std::lock_guard<std::mutex> l(msgMutex_);
    std::string m = std::move(message_);
    message_.clear();
    return m;
}
