#include "imaging.hpp"
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>

namespace {
// WIC's factory is free-threaded; each thread that uses it has COM initialized already.
IWICImagingFactory *factory() {
    static IWICImagingFactory *f = [] {
        IWICImagingFactory *p = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&p));
        return p;
    }();
    return f;
}
template <typename T> void release(T *&p) { if (p) { p->Release(); p = nullptr; } }

bool readSource(IWICBitmapSource *src, const WICRect *rect, Image &out) {
    IWICFormatConverter *conv = nullptr;
    if (FAILED(factory()->CreateFormatConverter(&conv))) return false;
    bool ok = false;
    if (SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (rect) { w = rect->Width; h = rect->Height; }
        if (w > 0 && h > 0 && (unsigned long long)w * h < 400000000ULL) {
            out.w = (int)w;
            out.h = (int)h;
            out.px.assign((size_t)w * h, 0);
            ok = SUCCEEDED(conv->CopyPixels(rect, w * 4, w * h * 4, reinterpret_cast<BYTE *>(out.px.data())));
        }
    }
    conv->Release();
    return ok;
}
}  // namespace

namespace img {
bool decode(const std::string &bytes, Image &out) {
    if (bytes.empty() || !factory()) return false;
    IStream *stream = SHCreateMemStream(reinterpret_cast<const BYTE *>(bytes.data()), (UINT)bytes.size());
    if (!stream) return false;
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    bool ok = SUCCEEDED(factory()->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && readSource(frame, nullptr, out);
    release(frame);
    release(dec);
    stream->Release();
    return ok;
}

bool size(const std::wstring &path, int &w, int &h) {
    if (!factory()) return false;
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    UINT uw = 0, uh = 0;
    bool ok = SUCCEEDED(factory()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                             WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&uw, &uh));
    release(frame);
    release(dec);
    w = (int)uw;
    h = (int)uh;
    return ok;
}

bool decodeRegion(const std::wstring &path, int x, int y, int w, int h, Image &out) {
    if (!factory() || w <= 0 || h <= 0) return false;
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    WICRect r{x, y, w, h};
    bool ok = SUCCEEDED(factory()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                             WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && readSource(frame, &r, out);
    release(frame);
    release(dec);
    return ok;
}

Image scale(const Image &in, int w, int h) {
    Image out;
    if (in.empty() || w <= 0 || h <= 0) return out;
    if (in.w == w && in.h == h) return in;
    IWICBitmap *bmp = nullptr;
    IWICBitmapScaler *sc = nullptr;
    if (SUCCEEDED(factory()->CreateBitmapFromMemory(in.w, in.h, GUID_WICPixelFormat32bppBGRA, in.w * 4,
                                                    (UINT)in.px.size() * 4, (BYTE *)in.px.data(), &bmp)) &&
        SUCCEEDED(factory()->CreateBitmapScaler(&sc)) &&
        SUCCEEDED(sc->Initialize(bmp, w, h, WICBitmapInterpolationModeFant))) {
        out.w = w;
        out.h = h;
        out.px.assign((size_t)w * h, 0);
        if (FAILED(sc->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE *>(out.px.data())))) out = Image();
    }
    release(sc);
    release(bmp);
    return out;
}

bool encodeJpeg(const Image &in, int quality, std::string &out) {
    if (in.empty() || !factory()) return false;
    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
    IWICBitmapEncoder *enc = nullptr;
    IWICBitmapFrameEncode *frame = nullptr;
    IPropertyBag2 *props = nullptr;
    bool ok = false;
    if (SUCCEEDED(factory()->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, &props))) {
        PROPBAG2 opt{};
        opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = quality / 100.0f;
        props->Write(1, &opt, &v);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
        // JPEG has no alpha: send the opaque pixels as 24-bit.
        std::vector<BYTE> rgb((size_t)in.w * in.h * 3);
        for (size_t i = 0; i < in.px.size(); i++) {
            uint32_t p = in.px[i];
            rgb[i * 3] = (BYTE)(p & 255);
            rgb[i * 3 + 1] = (BYTE)((p >> 8) & 255);
            rgb[i * 3 + 2] = (BYTE)((p >> 16) & 255);
        }
        ok = SUCCEEDED(frame->Initialize(props)) && SUCCEEDED(frame->SetSize(in.w, in.h)) &&
             SUCCEEDED(frame->SetPixelFormat(&fmt)) && fmt == GUID_WICPixelFormat24bppBGR &&
             SUCCEEDED(frame->WritePixels(in.h, in.w * 3, (UINT)rgb.size(), rgb.data())) &&
             SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    }
    if (ok) {
        HGLOBAL hg = nullptr;
        GetHGlobalFromStream(stream, &hg);
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        void *p = GlobalLock(hg);
        out.assign(static_cast<const char *>(p), (size_t)st.cbSize.QuadPart);
        GlobalUnlock(hg);
    }
    release(props);
    release(frame);
    release(enc);
    stream->Release();
    return ok;
}
}  // namespace img
