// Traffic and weather maps, for the stations that send them over the air: a port of the
// Android fork's MapFeed.kt (the same rules, the same base map, the same frame cache layout).
//
// TTN files: TMT_<area>_<row>_<col>_<date>_<time>_<id>.png = the nine traffic tiles,
// DWRO_* = a radar overlay, DWRI_* = the text that says where it belongs.
// HERE: nrsc5 hands over traffic tiles (trafficMap_<row>_<col>_...) and weather images
// with their corners.
// Times are when the station made the map (UTC in TTN names, HERE's own time), not arrival.
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "imaging.hpp"

class MapFeed {
public:
    struct Frame {
        std::shared_ptr<const std::string> jpeg;
        long long timeMs = 0;
    };

    MapFeed();
    ~MapFeed();
    void poll();                          // takes what the engine collected (every 2 s)

    int generation() const;
    std::shared_ptr<const Image> traffic(long long &timeMs) const;
    std::vector<Frame> frames() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
