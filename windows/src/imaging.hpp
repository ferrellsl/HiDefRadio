// Pictures through WIC (Windows Imaging Component): PNG/JPEG/GIF in, JPEG out, scaling and
// cutting a piece out of a big file without decoding all of it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;        // 0xAARRGGBB, straight (not premultiplied) alpha
    bool empty() const { return w <= 0 || h <= 0; }
};

namespace img {
bool decode(const std::string &bytes, Image &out);
bool size(const std::wstring &path, int &w, int &h);
bool decodeRegion(const std::wstring &path, int x, int y, int w, int h, Image &out);
Image scale(const Image &in, int w, int h);                 // smooth (Fant)
bool encodeJpeg(const Image &in, int quality, std::string &out);
}
