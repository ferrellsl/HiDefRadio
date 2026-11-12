// POSIX bits the Android engine uses, mapped onto Windows. Force-included (via engine_prefix.h)
// ahead of native-lib.cpp only. Sockets are used by the rtl_tcp source (iqsource_tcp.hpp).
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#include <cerrno>
#include <ctime>
#include <mutex>
#include <set>
#undef far     // windef.h's 16-bit leftovers clash with the engine's variable names
#undef near

#define timegm _mkgmtime
#define MSG_NOSIGNAL 0
#define SHUT_RDWR SD_BOTH
#ifndef F_GETFL
#define F_GETFL 3
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif

namespace hidefwin {
inline std::mutex &sockMutex() { static std::mutex m; return m; }
inline std::set<int> &sockets() { static std::set<int> s; return s; }
inline bool isSocket(int fd) { std::lock_guard<std::mutex> l(sockMutex()); return sockets().count(fd) != 0; }
inline void setErrno() {
    switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK: case WSAEINPROGRESS: errno = EINPROGRESS; break;
    case WSAEINTR: errno = EINTR; break;
    case WSAECONNREFUSED: errno = ECONNREFUSED; break;
    case WSAETIMEDOUT: errno = ETIMEDOUT; break;
    case WSAEHOSTUNREACH: case WSAENETUNREACH: errno = EHOSTUNREACH; break;
    default: errno = EIO; break;
    }
}
inline void startup() { static bool done = false; if (!done) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); done = true; } }
}  // namespace hidefwin

inline int hidef_socket(int af, int type, int proto) {
    hidefwin::startup();
    SOCKET s = ::socket(af, type, proto);
    if (s == INVALID_SOCKET) { hidefwin::setErrno(); return -1; }
    std::lock_guard<std::mutex> l(hidefwin::sockMutex());
    hidefwin::sockets().insert((int)s);
    return (int)s;
}
inline int hidef_close(int fd) {
    if (hidefwin::isSocket(fd)) {
        { std::lock_guard<std::mutex> l(hidefwin::sockMutex()); hidefwin::sockets().erase(fd); }
        return closesocket((SOCKET)fd);
    }
    return _close(fd);
}
inline int hidef_connect(int s, const sockaddr *a, socklen_t n) {
    int r = ::connect((SOCKET)s, a, n);
    if (r != 0) hidefwin::setErrno();
    return r;
}
inline int hidef_poll(pollfd *p, unsigned long n, int ms) {
    int r = WSAPoll(p, n, ms);
    if (r < 0) hidefwin::setErrno();
    return r;
}
inline int hidef_fcntl(int s, int cmd, int v = 0) {
    static thread_local int last = 0;    // Winsock can't report the mode; remember what was set
    if (cmd == F_GETFL) return last;
    u_long nb = (v & O_NONBLOCK) ? 1 : 0;
    last = v;
    return ioctlsocket((SOCKET)s, FIONBIO, &nb);
}
inline ssize_t send(int s, const void *b, size_t n, int f) {
    int r = ::send((SOCKET)s, (const char *)b, (int)n, f);
    if (r < 0) hidefwin::setErrno();
    return r;
}
inline ssize_t recv(int s, void *b, size_t n, int f) {
    int r = ::recv((SOCKET)s, (char *)b, (int)n, f);
    if (r < 0) hidefwin::setErrno();
    return r;
}
inline int setsockopt(int s, int l, int o, const void *v, socklen_t n) { return ::setsockopt((SOCKET)s, l, o, (const char *)v, n); }
inline int getsockopt(int s, int l, int o, void *v, socklen_t *n) { return ::getsockopt((SOCKET)s, l, o, (char *)v, n); }
inline int shutdown(int s, int how) { return ::shutdown((SOCKET)s, how); }

#define socket hidef_socket
#define close hidef_close
#define connect hidef_connect
#define poll hidef_poll
#define fcntl hidef_fcntl
