#include "mapfeed.hpp"
#include "engine.hpp"
#include "util.hpp"
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <regex>
#include <thread>

extern "C" int __android_log_print(int, const char *, const char *, ...);
#define MLOG(...) __android_log_print(4, "MapFeed", __VA_ARGS__)

namespace {
const int MAX_FRAMES = 12;                       // a new radar picture comes every 8 minutes or so
const int MIN_FRAME = 981;                       // radar pictures: at least this many pixels
const int MAX_FRAME = 2000;                      // and at most this, or as sharp as the map is
const int JPEG_QUALITY = 90;
const long long MAX_FRAME_AGE_MS = 3 * 3600000LL;   // older radar is history, not weather

std::wstring radarDir() { return localDir() + L"\\radar"; }
std::wstring baseMapPath() { return dataDir() + L"\\usa_basemap.jpg"; }

int javaHash(const std::string &data) {          // Arrays.hashCode(byte[]), as on Android
    uint32_t h = 1;
    for (char c : data) h = 31 * h + (uint32_t)(int32_t)(signed char)c;
    return (int32_t)h;
}

long long fileTimeMs(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return 0;
    ULARGE_INTEGER u;
    u.LowPart = a.ftLastWriteTime.dwLowDateTime;
    u.HighPart = a.ftLastWriteTime.dwHighDateTime;
    return (long long)(u.QuadPart / 10000ULL);
}

// "TMT_02qa5k_3_2_20220702_0654_03c2.png": 2 July 2022, 06:54 UTC. 0 = no time in it.
long long timeInName(const std::string &name) {
    static const std::regex re("_(\\d{8})_(\\d{4})_");
    std::smatch m;
    if (!std::regex_search(name, m, re)) return 0;
    std::string d = m[1], t = m[2];
    struct tm tmv {};
    tmv.tm_year = atoi(d.substr(0, 4).c_str()) - 1900;
    tmv.tm_mon = atoi(d.substr(4, 2).c_str()) - 1;
    tmv.tm_mday = atoi(d.substr(6, 2).c_str());
    tmv.tm_hour = atoi(t.substr(0, 2).c_str());
    tmv.tm_min = atoi(t.substr(2, 2).c_str());
    if (tmv.tm_mon < 0 || tmv.tm_mon > 11 || tmv.tm_mday < 1 || tmv.tm_mday > 31 || tmv.tm_hour > 23 || tmv.tm_min > 59)
        return 0;
    long long s = _mkgmtime(&tmv);
    return s > 0 ? s * 1000LL : 0;
}

int ttnTilePosition(const std::string &name) {
    std::vector<std::string> p = split(name, '_');
    if (p.size() < 4) return -1;
    int row = atoi(p[2].c_str()), col = atoi(p[3].c_str());
    return row >= 1 && row <= 3 && col >= 1 && col <= 3 ? (row - 1) * 3 + (col - 1) : -1;
}

int hereTilePosition(std::string name) {
    if (endsWith(name, ".png")) name.resize(name.size() - 4);
    std::vector<std::string> p = split(name, '_');
    size_t at = std::find(p.begin(), p.end(), std::string("trafficMap")) - p.begin();
    if (at >= p.size() || p.size() <= at + 2) return -1;
    if (p[at + 1].empty() || p[at + 2].empty()) return -1;
    int row = atoi(p[at + 1].c_str()), col = atoi(p[at + 2].c_str());
    bool fromOne = row == 3 || col == 3 || (row == 1 && col == 1);   // (seen counted from 1 and from 0)
    int r = fromOne ? row - 1 : row, c = fromOne ? col - 1 : col;
    return r >= 0 && r <= 2 && c >= 0 && c <= 2 ? r * 3 + c : -1;
}

struct Rect { int l, t, r, b; bool empty() const { return r <= l || b <= t; } };

// Corners to pixels on the base map, a Web Mercator projection (the numbers belong to that
// picture: top left 52.48278 N, 130.78125 W; see MapFeed.kt).
Rect mapArea(const double box[4], int width, int height) {
    auto mercator = [](double deg) { return std::asinh(std::tan(deg * M_PI / 180.0)); };
    double top = mercator(52.482780);
    double span = top - mercator(38.898);
    double xScale = 7162.0 * width / 12032.0;
    double yScale = 3565.0 * height / 6912.0;
    int x1 = (int)std::lround((box[1] + 130.781250) * xScale / 39.34135);
    int x2 = (int)std::lround((box[3] + 130.781250) * xScale / 39.34135);
    int y1 = (int)std::lround((top - mercator(box[0])) * yScale / span);
    int y2 = (int)std::lround((top - mercator(box[2])) * yScale / span);
    return Rect{std::max(x1, 0), std::max(y1, 0), std::min(x2, width), std::min(y2, height)};
}

// The radar over the map: black is "nothing here", the brighter the more solid.
Image compose(const Image &base, const Image &overlay) {
    int size = std::min(std::max(base.w, MIN_FRAME), MAX_FRAME);
    Image map = img::scale(base, size, size);
    Image radar = img::scale(overlay, size, size);
    if (map.empty() || radar.empty()) return overlay;
    for (size_t i = 0; i < map.px.size(); i++) {
        uint32_t p = radar.px[i];
        int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        int brightness = (r + g + b) / 3;
        if (brightness <= 8) continue;
        int alpha = std::max(brightness, (int)(p >> 24));
        uint32_t m = map.px[i];
        int mr = (m >> 16) & 255, mg = (m >> 8) & 255, mb = m & 255;
        map.px[i] = 0xFF000000u | (uint32_t)((r * alpha + mr * (255 - alpha)) / 255) << 16 |
                    (uint32_t)((g * alpha + mg * (255 - alpha)) / 255) << 8 |
                    (uint32_t)((b * alpha + mb * (255 - alpha)) / 255);
    }
    return map;
}
}  // namespace

struct MapFeed::Impl {
    // ---- published (UI thread reads) ----
    mutable std::mutex pub;
    std::atomic<int> generation{0};
    std::shared_ptr<const Image> traffic;
    long long trafficTimeMs = 0;
    std::vector<Frame> frames;

    // ---- the worker thread's own ----
    int epoch = -1;
    std::shared_ptr<Image> tiles[9];
    long long tileTimes[9] = {0};
    int trafficHere = -1;                 // which feed the tiles are from: 1 HERE, 0 TTN, -1 none yet
    bool haveCorners = false;
    double corners[4] = {0, 0, 0, 0};     // north, west, south, east
    std::shared_ptr<Image> baseMap;
    std::string waitingOverlay;           // a radar picture that came before its corners
    long long waitingTime = 0;
    int lastOverlay = 0;
    std::string area;                     // the folder of the frames in use

    // ---- the worker ----
    std::thread thread;
    std::mutex qm;
    std::condition_variable qcv;
    std::deque<std::function<void()>> queue;
    bool stop = false;

    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> l(qm);
                qcv.wait(l, [&] { return stop || !queue.empty(); });
                if (stop && queue.empty()) break;
                job = std::move(queue.front());
                queue.pop_front();
            }
            try { job(); } catch (...) { MLOG("map file: failed"); }
        }
        CoUninitialize();
    }
    void post(std::function<void()> f) {
        { std::lock_guard<std::mutex> l(qm); queue.push_back(std::move(f)); }
        qcv.notify_one();
    }
    void setFrames(std::vector<Frame> f) {
        std::lock_guard<std::mutex> l(pub);
        frames = std::move(f);
    }

    void forgetStation() {
        for (auto &t : tiles) t.reset();
        trafficHere = -1;
        { std::lock_guard<std::mutex> l(pub); traffic.reset(); }
        haveCorners = false;
        baseMap.reset();
        waitingOverlay.clear();
        // The last radar stays until this station says it covers somewhere else.
        std::wstring newest;
        long long newestMs = -1;
        for (const std::wstring &dname : listDirs(radarDir())) {
            long long t = fileTimeMs(radarDir() + L"\\" + dname);
            if (t > newestMs) { newestMs = t; newest = dname; }
        }
        if (!newest.empty()) restoreFrames(newest);
        else {
            setFrames({});
            lastOverlay = 0;
            area.clear();
        }
        generation++;
    }

    // The frames in a folder: <time>_<hash of the radar picture>.jpg
    void restoreFrames(const std::wstring &folderName) {
        std::wstring folder = radarDir() + L"\\" + folderName;
        long long oldest = wallMs() - MAX_FRAME_AGE_MS;
        std::vector<std::pair<long long, std::wstring>> kept;
        for (const std::wstring &f : listFiles(folder)) {
            std::string n = narrow(f);
            long long t = atoll(n.substr(0, n.find('_')).c_str());
            if (t <= 0 || t < oldest) DeleteFileW((folder + L"\\" + f).c_str());
            else kept.push_back({t, f});
        }
        std::sort(kept.begin(), kept.end());
        std::vector<Frame> fs;
        size_t from = kept.size() > (size_t)MAX_FRAMES ? kept.size() - MAX_FRAMES : 0;
        for (size_t i = from; i < kept.size(); i++) {
            auto bytes = std::make_shared<std::string>();
            if (readFile(folder + L"\\" + kept[i].second, *bytes)) fs.push_back(Frame{bytes, kept[i].first});
        }
        setFrames(std::move(fs));
        lastOverlay = 0;
        if (!kept.empty()) {
            std::string n = narrow(kept.back().second);
            size_t us = n.find('_'), dot = n.rfind(".jpg");
            if (us != std::string::npos && dot != std::string::npos) lastOverlay = atoi(n.substr(us + 1, dot - us - 1).c_str());
        }
        area = narrow(folderName);
    }

    void saveFrame(const Frame &frame, int hash) {
        if (area.empty()) return;
        std::wstring folder = radarDir() + L"\\" + widen(area);
        makeDirs(folder);
        writeFile(folder + L"\\" + widen(fmt("%lld_%d.jpg", frame.timeMs, hash)), *frame.jpeg);
        std::vector<std::pair<long long, std::wstring>> files;
        for (const std::wstring &f : listFiles(folder)) {
            std::string n = narrow(f);
            files.push_back({atoll(n.substr(0, n.find('_')).c_str()), f});
        }
        std::sort(files.begin(), files.end());
        if (files.size() > (size_t)MAX_FRAMES)
            for (size_t i = 0; i < files.size() - MAX_FRAMES; i++) DeleteFileW((folder + L"\\" + files[i].second).c_str());
    }

    void handle(const std::string &item) {
        size_t end = item.find('\n');
        if (end == std::string::npos) return;
        std::vector<std::string> head = split(item.substr(0, end), '\t');
        if (head.size() < 7) return;
        std::string data = item.substr(end + 1);
        const std::string &name = head[1];
        double box[4];
        for (int i = 0; i < 4; i++) box[i] = strtod(head[2 + i].c_str(), nullptr);
        long long made = atoll(head[6].c_str());
        if (made <= 0) made = timeInName(name);
        if (made <= 0) made = wallMs();
        if (head[0] == "T") trafficTile(data, hereTilePosition(name), true, made);
        else if (head[0] == "W") {
            setCorners(box);
            weather(data, made);
        } else if (startsWith(name, "TMT_")) trafficTile(data, ttnTilePosition(name), false, made);
        else if (startsWith(name, "DWRI_")) weatherInfo(data);
        else if (startsWith(name, "DWRO_")) weather(data, made);
    }

    void trafficTile(const std::string &data, int position, bool here, long long made) {
        if (position < 0) return;
        if (trafficHere >= 0 && trafficHere != (here ? 1 : 0)) for (auto &t : tiles) t.reset();
        trafficHere = here ? 1 : 0;
        auto tile = std::make_shared<Image>();
        if (!img::decode(data, *tile)) return;
        tiles[position] = tile;
        tileTimes[position] = made;
        for (auto &t : tiles) if (!t) return;
        int w = tiles[0]->w, h = tiles[0]->h;
        auto map = std::make_shared<Image>();
        map->w = w * 3;
        map->h = h * 3;
        map->px.assign((size_t)map->w * map->h, 0xFF000000u);
        for (int i = 0; i < 9; i++) {
            const Image &t = *tiles[i];
            int ox = (i % 3) * w, oy = (i / 3) * h;
            for (int y = 0; y < std::min(t.h, h); y++)
                for (int x = 0; x < std::min(t.w, w); x++) map->px[(size_t)(oy + y) * map->w + ox + x] = t.px[(size_t)y * t.w + x];
        }
        long long newest = *std::max_element(tileTimes, tileTimes + 9);   // tiles are renewed one by one
        {
            std::lock_guard<std::mutex> l(pub);
            traffic = map;
            trafficTimeMs = newest;
        }
        generation++;
        MLOG("Traffic map complete (%dx%d)", map->w, map->h);
    }

    // The DWRI text: Coordinates="(35.63475,-114.6349)";"(31.13642,-109.2450)"
    void weatherInfo(const std::string &text) {
        for (const std::string &raw : split(text, '\n')) {
            std::string line = trim(raw);
            if (!startsWith(line, "Coordinates=")) continue;
            static const std::regex num("-?\\d+(?:\\.\\d+)?");
            std::vector<double> n;
            for (auto it = std::sregex_iterator(line.begin(), line.end(), num); it != std::sregex_iterator(); ++it)
                n.push_back(strtod(it->str().c_str(), nullptr));
            if (n.size() < 4) return;
            double box[4] = {n[0], n[1], n[2], n[3]};
            setCorners(box);
            if (!waitingOverlay.empty()) {
                std::string w = std::move(waitingOverlay);
                waitingOverlay.clear();
                weather(w, waitingTime);
            }
            return;
        }
    }

    void setCorners(const double box[4]) {
        if (haveCorners && std::equal(box, box + 4, corners) && baseMap) return;
        std::copy(box, box + 4, corners);
        haveCorners = true;
        baseMap = cutBaseMap(box);
        std::string name = fmt("%.3f_%.3f_%.3f_%.3f", box[0], box[1], box[2], box[3]);
        if (name == area) return;
        // the frames so far belong to another area: this one's, if any were kept
        area = name;
        setFrames({});
        lastOverlay = 0;
        std::wstring folder = radarDir() + L"\\" + widen(name);
        if (GetFileAttributesW(folder.c_str()) != INVALID_FILE_ATTRIBUTES) restoreFrames(widen(name));
        generation++;
    }

    std::shared_ptr<Image> cutBaseMap(const double box[4]) {
        int w = 0, h = 0;
        if (!img::size(baseMapPath(), w, h)) { MLOG("No base map"); return nullptr; }
        Rect r = mapArea(box, w, h);
        if (r.empty()) return nullptr;
        auto out = std::make_shared<Image>();
        if (!img::decodeRegion(baseMapPath(), r.l, r.t, r.r - r.l, r.b - r.t, *out)) return nullptr;
        MLOG("Base map cut: %d,%d %dx%d", r.l, r.t, out->w, out->h);
        return out;
    }

    void weather(const std::string &data, long long made) {
        if (!haveCorners) {
            waitingOverlay = data;        // its corners are on their way (the DWRI file)
            waitingTime = made;
            return;
        }
        int hash = javaHash(data);
        if (hash == lastOverlay) { renewNewest(hash, made); return; }
        Image overlay;
        if (!img::decode(data, overlay)) return;
        lastOverlay = hash;
        Image picture = baseMap ? compose(*baseMap, overlay) : overlay;
        auto jpeg = std::make_shared<std::string>();
        if (!img::encodeJpeg(picture, JPEG_QUALITY, *jpeg)) return;
        Frame frame{jpeg, made};
        std::vector<Frame> fs;
        {
            std::lock_guard<std::mutex> l(pub);
            fs = frames;
        }
        fs.push_back(frame);
        std::stable_sort(fs.begin(), fs.end(), [](const Frame &a, const Frame &b) { return a.timeMs < b.timeMs; });
        if (fs.size() > (size_t)MAX_FRAMES) fs.erase(fs.begin(), fs.end() - MAX_FRAMES);
        setFrames(std::move(fs));
        saveFrame(frame, hash);
        generation++;
        MLOG("Radar frame %dx%d", picture.w, picture.h);
    }

    // The same picture, made again: no new frame, but the newest one takes the new time.
    void renewNewest(int hash, long long made) {
        std::vector<Frame> fs;
        {
            std::lock_guard<std::mutex> l(pub);
            fs = frames;
        }
        if (fs.empty() || made <= fs.back().timeMs) return;
        long long old = fs.back().timeMs;
        fs.back().timeMs = made;
        setFrames(std::move(fs));
        if (!area.empty()) {
            std::wstring folder = radarDir() + L"\\" + widen(area);
            MoveFileExW((folder + L"\\" + widen(fmt("%lld_%d.jpg", old, hash))).c_str(),
                        (folder + L"\\" + widen(fmt("%lld_%d.jpg", made, hash))).c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        generation++;
    }
};

MapFeed::MapFeed() : d(new Impl) {
    d->thread = std::thread([this] { d->run(); });
}

MapFeed::~MapFeed() {
    { std::lock_guard<std::mutex> l(d->qm); d->stop = true; }
    d->qcv.notify_one();
    if (d->thread.joinable()) d->thread.join();
}

void MapFeed::poll() {
    int now = eng::mapEpoch();
    if (now != d->epoch) {
        d->epoch = now;
        d->post([this] { d->forgetStation(); });
    }
    std::string item;
    while (eng::takeMapItem(item)) {
        auto shared = std::make_shared<std::string>(std::move(item));
        d->post([this, shared] { d->handle(*shared); });
    }
}

int MapFeed::generation() const { return d->generation.load(); }

std::shared_ptr<const Image> MapFeed::traffic(long long &timeMs) const {
    std::lock_guard<std::mutex> l(d->pub);
    timeMs = d->trafficTimeMs;
    return d->traffic;
}

std::vector<MapFeed::Frame> MapFeed::frames() const {
    std::lock_guard<std::mutex> l(d->pub);
    return d->frames;
}
