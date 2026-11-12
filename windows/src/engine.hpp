// The Android app's radio engine (native-lib.cpp), as plain C++ calls.
// One engine per process, like on Android (RadioEngine is a Kotlin object).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace eng {

enum AudioSource { SOURCE_HD = 0, SOURCE_ANALOG = 1, SOURCE_AUTO = 2 };

std::string openDongle(int index);          // index 0 = the first RTL-SDR
std::string openTcp(const std::string &host, int port);
void closeDongle();
bool isDongleOpen();
std::string startStream(int freqHz, int gainTenthsDb, int hintTenthsDb);
void stopStream();
bool isStreaming();
long long streamBytes();
float signalLevel();
std::string status();

void setProgram(int program);
int program();
std::string programs();                     // one line per program, TAB-separated
std::string signal();                       // key=value lines

int setGain(int tenths);
float takePeakMax();
std::vector<int> gainSteps();
void setAutoGain(int tenths);

int pictureGen();
int mapEpoch();
bool takeMapItem(std::string &out);
std::string logo(int program);              // empty = none
std::string art(int program);

int readAudio(int16_t *buf, int capacity, int timeoutMs);
int audioBuffered();
long long audioDropped();

void setAudioSource(int source);
void setBlendUnmatched(bool playHd);
void setStereoMode(int mode);
void setDeemphasis(int us);
void setAmBandwidth(int hz);
void setAmDeemphasis(bool on);
void setChannelCalls(const std::string &calls, bool known);
void setHdOwner(const std::string &name, int pi);
void measureAlignment();
void setClockCorrection(bool on);
void setHdRest(bool on);

// IQ recording (iqrecord.hpp): the engine owns [fd] from here on and closes it.
// 0 = recording, 1 = not streaming, 2 = one is going on, 3 = no memory.
int startIqRecord(int fd, int maxSeconds);
void stopIqRecord();                        // waits until the file is complete
bool iqRecordState(long long &state, long long &written);   // state 0 idle, 1 recording, 2 ended
bool takeIqRecord(long long &bytes, int &reason);           // false = nothing ended

}  // namespace eng
