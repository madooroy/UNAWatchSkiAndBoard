/**
 ******************************************************************************
 * @file    HeartRateLog.cpp
 * @brief   See HeartRateLog.hpp.
 ******************************************************************************
 */

#include "Ski/HeartRateLog.hpp"

namespace Ski
{

void HeartRateLog::reset(uint32_t t)
{
    for (uint32_t i = 0; i < kRing; ++i) {
        mRing[i] = 0;
    }
    mFirst = mNext = mLapFrom = t;
    mFolded = Acc{};
    mDay    = Acc{};
}

void HeartRateLog::add(uint32_t t, uint8_t bpm)
{
    if (t < mNext) {
        return;   // already have this second
    }
    // Seconds skipped (pause, a late tick) count as no reading.
    while (mNext <= t) {
        if (mNext - mFirst >= kRing) {
            const uint8_t old = mRing[mFirst % kRing];
            if (mFirst >= mLapFrom) {
                mFolded.add(old);
            }
            ++mFirst;
        }
        const uint8_t b = (mNext == t) ? bpm : 0;
        mRing[mNext % kRing] = b;
        ++mNext;
    }
    mDay.add(bpm);
}

HeartRateLog::Stats HeartRateLog::closeLap(uint32_t start, uint32_t end)
{
    Acc a = mFolded;   // folded readings are all >= mLapFrom >= start
    for (uint32_t s = (start > mFirst ? start : mFirst); s < end && s < mNext; ++s) {
        a.add(mRing[s % kRing]);
    }
    mFolded  = Acc{};
    mLapFrom = end;
    return a.stats();
}

HeartRateLog::Stats HeartRateLog::day() const
{
    return mDay.stats();
}

} // namespace Ski
