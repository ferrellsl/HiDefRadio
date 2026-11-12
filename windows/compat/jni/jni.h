// A tiny stand-in for JNI, so the Android app's native-lib.cpp compiles unchanged on Windows.
// Only what native-lib.cpp uses is here. Objects it returns are owned by the caller
// (engine.cpp), which frees them with jni_free().
#pragma once
#include <cstdint>
#include <string>
#include <vector>

typedef int32_t jint;
typedef int64_t jlong;
typedef uint8_t jboolean;
typedef int8_t jbyte;
typedef uint16_t jchar;
typedef int16_t jshort;
typedef float jfloat;
typedef double jdouble;
typedef jint jsize;

#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNIEXPORT
#define JNICALL

struct _jobject { virtual ~_jobject() = default; };
struct _jstring : _jobject { std::string utf8; };
template <typename T> struct _jarrayT : _jobject {
    std::vector<T> own;          // the array's values, unless...
    T *ext = nullptr;            // ...it wraps the caller's own buffer
    jsize extLen = 0;
    T *data() { return ext ? ext : own.data(); }
    jsize length() const { return ext ? extLen : (jsize)own.size(); }
};
typedef _jobject *jobject;
typedef _jstring *jstring;
typedef _jarrayT<jbyte> *jbyteArray;
typedef _jarrayT<jshort> *jshortArray;
typedef _jarrayT<jint> *jintArray;
typedef _jarrayT<jlong> *jlongArray;

inline void jni_free(jobject o) { delete o; }

struct JNIEnv {
    jstring NewStringUTF(const char *s) { auto *r = new _jstring; r->utf8 = s ? s : ""; return r; }
    jstring NewString(const jchar *u, jsize n) {
        auto *r = new _jstring;
        for (jsize i = 0; i < n; i++) {           // UTF-16 -> UTF-8
            uint32_t c = u[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000)
                c = 0x10000 + ((c - 0xD800) << 10) + (u[++i] - 0xDC00);
            std::string &o = r->utf8;
            if (c < 0x80) o += (char)c;
            else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
            else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
            else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        }
        return r;
    }
    const char *GetStringUTFChars(jstring s, jboolean *) { return s ? s->utf8.c_str() : nullptr; }
    void ReleaseStringUTFChars(jstring, const char *) {}
    template <typename A> jsize GetArrayLength(A a) { return a ? a->length() : 0; }
    jbyteArray NewByteArray(jsize n) { auto *a = new _jarrayT<jbyte>; a->own.resize(n); return a; }
    jintArray NewIntArray(jsize n) { auto *a = new _jarrayT<jint>; a->own.resize(n); return a; }
    jlongArray NewLongArray(jsize n) { auto *a = new _jarrayT<jlong>; a->own.resize(n); return a; }
    template <typename A, typename T> void setRegion(A a, jsize at, jsize n, const T *v) {
        for (jsize i = 0; i < n; i++) a->data()[at + i] = v[i];
    }
    void SetByteArrayRegion(jbyteArray a, jsize at, jsize n, const jbyte *v) { setRegion(a, at, n, v); }
    void SetShortArrayRegion(jshortArray a, jsize at, jsize n, const jshort *v) { setRegion(a, at, n, v); }
    void SetIntArrayRegion(jintArray a, jsize at, jsize n, const jint *v) { setRegion(a, at, n, v); }
    void SetLongArrayRegion(jlongArray a, jsize at, jsize n, const jlong *v) { setRegion(a, at, n, v); }
};
