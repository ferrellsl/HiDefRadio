// The gain watchdog: a line-for-line port of GainKeeper.kt (see that file for the reasons).
// Rule 1: overload -> one step down, and no going back up for 2 minutes.
// Rule 2: 10 s quiet enough -> one step up.
// Rule 3: MER tries - a step up or down, kept only if the MER gets better.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <vector>

class GainKeeper {
public:
    static const int NO_CHANGE = -1;
    static constexpr float OVERLOAD_DBFS = -1.5f;
    static constexpr float TARGET_DBFS = -4.0f;
    static const int QUIET_TICKS = 10;
    static const long long UP_BLOCK_MS = 120000;
    static const int SETTLE_TICKS = 1;
    static const long long FIRST_TRY_MS = 30000;
    static const long long TRY_AGAIN_MS = 20000;
    static const long long TRY_FAIL_WAIT_MS = 60000;
    static const long long TRY_FAIL_WAIT_MAX_MS = 480000;
    static const size_t BASE_READINGS = 3;
    static const size_t TRY_READINGS = 2;
    static constexpr float KEEP_MARGIN_DB = 0.5f;
    static const long long TRY_TIMEOUT_MS = 10000;
    static constexpr float TRY_PEAK_MAX_DBFS = -2.5f;
    static const long long CEILING_MS = 600000;
    static constexpr float NO_DATA_DBFS = -90.0f;

    explicit GainKeeper(std::vector<int> steps) : steps_(std::move(steps)) { std::sort(steps_.begin(), steps_.end()); }

    int downs = 0, ups = 0, lastFrom = -1, lastTo = -1, tries = 0, kept = 0;
    long long lastAtMs = 0;
    int tryingTo() const { return tryTo_ >= 0 ? steps_[tryTo_] : -1; }

    void restart(long long nowMs) {
        downs = ups = 0;
        lastFrom = lastTo = -1;
        lastAtMs = 0;
        tries = kept = 0;
        stableSinceMs_ = nowMs;
        settle_ = 0;
        recent_.clear();
        blockedIndex_ = -1;
        blockedUntilMs_ = 0;
        lastMerCount_ = -1;
        skipMer_ = 0;
        merHere_.clear();
        endTry();
        nextTryMs_ = nowMs + FIRST_TRY_MS;
        failWaitMs_ = TRY_FAIL_WAIT_MS;
        preferUp_ = true;
        ceilingIndex_ = -1;
        ceilingUntilMs_ = 0;
        checkPending_ = false;
    }

    long long stableForMs(long long nowMs) const { return tryFrom_ >= 0 ? 0 : nowMs - stableSinceMs_; }

    int tick(long long nowMs, int gainTenths, float peakDbfs, bool locked, int merCount, float merLower, float merUpper) {
        if (steps_.empty()) return NO_CHANGE;
        if (merCount != lastMerCount_) {
            bool isNew = lastMerCount_ >= 0 && merCount > lastMerCount_;
            lastMerCount_ = merCount;
            if (isNew && locked) {
                if (skipMer_ > 0) skipMer_--;
                else merHere_.push_back(std::max(merLower, merUpper));
            }
        }
        if (peakDbfs < NO_DATA_DBFS) return NO_CHANGE;
        if (settle_ > 0) { settle_--; return NO_CHANGE; }
        int i = nearestIndex(gainTenths);
        if (peakDbfs > OVERLOAD_DBFS) {
            endTry();
            if (i == 0) return NO_CHANGE;
            blockedIndex_ = i;
            blockedUntilMs_ = nowMs + UP_BLOCK_MS;
            return change(nowMs, i, i - 1, true);
        }
        recent_.push_back(peakDbfs);
        while ((int)recent_.size() > QUIET_TICKS) recent_.pop_front();
        if (tryFrom_ >= 0) return judgeTry(nowMs, i);
        int last = (int)steps_.size() - 1;
        if ((int)recent_.size() == QUIET_TICKS && i < last && mayGoUpTo(i + 1, nowMs) && !(locked && checkPending_)) {
            float loudest = *std::max_element(recent_.begin(), recent_.end());
            if (loudest + rise(i) < TARGET_DBFS) {
                preferUp_ = false;
                nextTryMs_ = std::min(nextTryMs_, nowMs + TRY_AGAIN_MS);
                checkPending_ = true;
                return change(nowMs, i, i + 1, true);
            }
        }
        if (locked && nowMs >= nextTryMs_ && merHere_.size() >= BASE_READINGS) {
            bool haveLoudest = !recent_.empty();
            float loudest = haveLoudest ? *std::max_element(recent_.begin(), recent_.end()) : 0;
            bool canUp = i < last && haveLoudest && loudest + rise(i) < TRY_PEAK_MAX_DBFS && mayGoUpTo(i + 1, nowMs);
            bool canDown = i > 0;
            bool up = preferUp_ ? canUp : (canUp && !canDown);
            int target = up ? i + 1 : canDown ? i - 1 : -1;
            if (target < 0) {
                nextTryMs_ = nowMs + failWaitMs_;
                return NO_CHANGE;
            }
            tries++;
            checkPending_ = false;
            float sum = 0;
            for (size_t k = merHere_.size() - BASE_READINGS; k < merHere_.size(); k++) sum += merHere_[k];
            tryBase_ = sum / BASE_READINGS;
            tryFrom_ = i;
            tryTo_ = target;
            tryStartMs_ = nowMs;
            return change(nowMs, i, target, false);
        }
        return NO_CHANGE;
    }

private:
    int judgeTry(long long nowMs, int i) {
        int from = tryFrom_, to = tryTo_;
        bool up = to > from;
        if (merHere_.size() >= TRY_READINGS) {
            float sum = 0;
            for (float m : merHere_) sum += m;
            float mer = sum / merHere_.size();
            if (mer >= tryBase_ + KEEP_MARGIN_DB) {
                kept++;
                record(nowMs, from, to);
                if (!up) setCeiling(from, nowMs);
                preferUp_ = up;
                failWaitMs_ = TRY_FAIL_WAIT_MS;
                nextTryMs_ = nowMs + TRY_AGAIN_MS;
                endTry();
                return NO_CHANGE;
            }
            if (up) setCeiling(to, nowMs);
            preferUp_ = !up;
            nextTryMs_ = nowMs + failWaitMs_;
            failWaitMs_ = std::min(failWaitMs_ * 2, TRY_FAIL_WAIT_MAX_MS);
            endTry();
            return change(nowMs, i, from, false);
        }
        if (nowMs - tryStartMs_ > TRY_TIMEOUT_MS) {
            nextTryMs_ = nowMs + failWaitMs_;
            endTry();
            return change(nowMs, i, from, false);
        }
        return NO_CHANGE;
    }
    bool mayGoUpTo(int index, long long nowMs) const {
        bool blocked = blockedIndex_ >= 0 && blockedIndex_ <= index && nowMs < blockedUntilMs_;
        bool capped = ceilingIndex_ >= 0 && ceilingIndex_ <= index && nowMs < ceilingUntilMs_;
        return !blocked && !capped;
    }
    void setCeiling(int index, long long nowMs) { ceilingIndex_ = index; ceilingUntilMs_ = nowMs + CEILING_MS; }
    void endTry() { tryFrom_ = tryTo_ = -1; }
    float rise(int i) const { return (steps_[i + 1] - steps_[i]) / 10.0f; }
    void record(long long nowMs, int from, int to) {
        if (to < from) downs++; else ups++;
        lastFrom = steps_[from];
        lastTo = steps_[to];
        lastAtMs = nowMs;
        stableSinceMs_ = nowMs;
    }
    int change(long long nowMs, int from, int to, bool stays) {
        if (stays) record(nowMs, from, to);
        settle_ = SETTLE_TICKS;
        recent_.clear();
        merHere_.clear();
        skipMer_ = 1;
        return steps_[to];
    }
    int nearestIndex(int g) const {
        int best = 0;
        for (int k = 0; k < (int)steps_.size(); k++)
            if (std::abs(steps_[k] - g) < std::abs(steps_[best] - g)) best = k;
        return best;
    }

    std::vector<int> steps_;
    long long stableSinceMs_ = 0;
    int settle_ = 0;
    std::deque<float> recent_;
    int blockedIndex_ = -1;
    long long blockedUntilMs_ = 0;
    int lastMerCount_ = -1;
    int skipMer_ = 0;
    std::vector<float> merHere_;
    int tryFrom_ = -1, tryTo_ = -1;
    float tryBase_ = 0;
    long long tryStartMs_ = 0, nextTryMs_ = 0;
    long long failWaitMs_ = TRY_FAIL_WAIT_MS;
    bool preferUp_ = true;
    int ceilingIndex_ = -1;
    long long ceilingUntilMs_ = 0;
    bool checkPending_ = false;
};
