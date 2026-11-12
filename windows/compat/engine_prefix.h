// Force-included ahead of the Android app's native-lib.cpp on Windows (see CMakeLists.txt).
#pragma once
#ifdef __cplusplus
#include "win_posix.h"
#include <windows.h>
#undef far
#undef near
// sched_getcpu() is Linux's; Windows has the same thing under another name.
static inline int sched_getcpu(void) { return (int)GetCurrentProcessorNumber(); }
#include "rtl-sdr.h"
// Android hands the app an open USB file descriptor; on Windows the dongle is opened by
// its index (0 = the first RTL-SDR), and librtlsdr's own rtlsdr_open() does that.
#define rtlsdr_open_fd(dev, fd) rtlsdr_open((dev), (uint32_t)(fd))
#endif
