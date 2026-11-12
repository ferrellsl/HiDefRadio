#include "engine.hpp"
#include "engine_jni.h"

#define J(name) Java_io_github_derek20la_hidefradio_RadioEngine_##name

namespace eng {
namespace {
JNIEnv env;
std::string str(jstring s) {
    if (s == nullptr) return {};
    std::string r = s->utf8;
    jni_free(s);
    return r;
}
std::string bytes(jbyteArray a) {
    if (a == nullptr) return {};
    std::string r(reinterpret_cast<const char *>(a->data()), a->length());
    jni_free(a);
    return r;
}
jstring jstr(const std::string &s) { return env.NewStringUTF(s.c_str()); }
}  // namespace

std::string openDongle(int index) { return str(J(openDongleNative)(&env, nullptr, index)); }
std::string openTcp(const std::string &host, int port) {
    jstring h = jstr(host);
    std::string r = str(J(openTcpNative)(&env, nullptr, h, port));
    jni_free(h);
    return r;
}
void closeDongle() { J(closeDongleNative)(&env, nullptr); }
bool isDongleOpen() { return J(isDongleOpenNative)(&env, nullptr); }
std::string startStream(int f, int g, int h) { return str(J(startStreamNative)(&env, nullptr, f, g, h)); }
void stopStream() { J(stopStreamNative)(&env, nullptr); }
bool isStreaming() { return J(isStreamingNative)(&env, nullptr); }
long long streamBytes() { return J(getStreamBytesNative)(&env, nullptr); }
float signalLevel() { return J(getSignalLevelNative)(&env, nullptr); }
std::string status() { return str(J(getStatusNative)(&env, nullptr)); }

void setProgram(int p) { J(setProgramNative)(&env, nullptr, p); }
int program() { return J(getProgramNative)(&env, nullptr); }
std::string programs() { return str(J(getProgramsNative)(&env, nullptr)); }
std::string signal() { return str(J(getSignalNative)(&env, nullptr)); }

int setGain(int t) { return J(setGainNative)(&env, nullptr, t); }
float takePeakMax() { return J(takePeakMaxNative)(&env, nullptr); }
std::vector<int> gainSteps() {
    jintArray a = J(getGainStepsNative)(&env, nullptr);
    std::vector<int> r;
    if (a) { r.assign(a->data(), a->data() + a->length()); jni_free(a); }
    return r;
}
void setAutoGain(int t) { J(setAutoGainNative)(&env, nullptr, t); }

int pictureGen() { return J(getPictureGenNative)(&env, nullptr); }
int mapEpoch() { return J(getMapEpochNative)(&env, nullptr); }
bool takeMapItem(std::string &out) {
    jbyteArray a = J(takeMapItemNative)(&env, nullptr);
    if (a == nullptr) return false;
    out = bytes(a);
    return true;
}
std::string logo(int p) { return bytes(J(getLogoNative)(&env, nullptr, p)); }
std::string art(int p) { return bytes(J(getArtNative)(&env, nullptr, p)); }

int readAudio(int16_t *buf, int capacity, int timeoutMs) {
    _jarrayT<jshort> a;             // wraps the caller's buffer: no copy
    a.ext = buf;
    a.extLen = capacity;
    return J(readAudioNative)(&env, nullptr, &a, timeoutMs);
}
int audioBuffered() { return J(getAudioBufferedNative)(&env, nullptr); }
long long audioDropped() { return J(getAudioDroppedNative)(&env, nullptr); }

void setAudioSource(int s) { J(setAudioSourceNative)(&env, nullptr, s); }
void setBlendUnmatched(bool b) { J(setBlendUnmatchedNative)(&env, nullptr, b); }
void setStereoMode(int m) { J(setStereoModeNative)(&env, nullptr, m); }
void setDeemphasis(int us) { J(setDeemphasisNative)(&env, nullptr, us); }
void setAmBandwidth(int hz) { J(setAmBandwidthNative)(&env, nullptr, hz); }
void setAmDeemphasis(bool on) { J(setAmDeemphasisNative)(&env, nullptr, on); }
void setChannelCalls(const std::string &calls, bool known) {
    jstring c = jstr(calls);
    J(setChannelCallsNative)(&env, nullptr, c, known);
    jni_free(c);
}
void setHdOwner(const std::string &name, int pi) {
    jstring n = jstr(name);
    J(setHdOwnerNative)(&env, nullptr, n, pi);
    jni_free(n);
}
void measureAlignment() { J(measureAlignmentNative)(&env, nullptr); }
void setClockCorrection(bool on) { J(setClockCorrectionNative)(&env, nullptr, on); }
void setHdRest(bool on) { J(setHdRestNative)(&env, nullptr, on); }

}  // namespace eng
