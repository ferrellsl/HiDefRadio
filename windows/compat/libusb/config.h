/* libusb config.h for MinGW-w64 (Windows backend: WinUSB via Zadig). */
#define DEFAULT_VISIBILITY __attribute__((visibility("default")))
#define ENABLE_LOGGING 1
#define PLATFORM_WINDOWS 1
#define PRINTF_FORMAT(a, b) __attribute__((format(__MINGW_PRINTF_FORMAT, a, b)))
#define HAVE_STRUCT_TIMESPEC 1
#define _WIN32_WINNT 0x0601
