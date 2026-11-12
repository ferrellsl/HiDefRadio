// android/log.h stand-in: the engine's messages go to hidefradio.log (see winlog.cpp).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { ANDROID_LOG_VERBOSE = 2, ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR };
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#ifdef __cplusplus
}
#endif
