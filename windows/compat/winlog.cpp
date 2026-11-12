// The engine's log (Logcat on Android) -> %LOCALAPPDATA%\HiDefRadio\hidefradio.log
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include "android/log.h"

static std::mutex g_logMutex;
static FILE *g_log = nullptr;

extern "C" void hidef_log_open(const char *path) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_log) fclose(g_log);
    g_log = fopen(path, "w");
}

extern "C" int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    FILE *f = g_log ? g_log : stderr;
    time_t t = time(nullptr);
    char when[16];
    strftime(when, sizeof when, "%H:%M:%S", localtime(&t));
    static const char lv[] = "??VDIWEF";
    fprintf(f, "%s %c %s: ", when, prio >= 0 && prio < 8 ? lv[prio] : '?', tag ? tag : "");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
    return 0;
}
