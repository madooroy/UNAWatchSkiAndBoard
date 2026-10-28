/**
 ******************************************************************************
 * @file    HeartRateLog.hpp
 * @brief   Heart rate for each FIT lap and for the whole day.
 *
 * Runs and lift rides are confirmed after the fact and dated back (a run
 * starts when you pushed off, which is known only once you are 25 m down), so
 * a lap's start and end are often minutes in the past by the time it is
 * written. This keeps the last hour of readings, one per second, so a lap's
 * average and maximum can be worked out exactly once its bounds are known.
 * Older readings are folded into a running total for the lap in progress.
 *
 * Plain C++, no SDK dependency.
 ******************************************************************************
 */

#ifndef SKI_HEART_RATE_LOG_HPP
#define SKI_HEART_RATE_LOG_HPP

#include <cstdint>

namespace Ski
{

class HeartRateLog
{
public:
    struct Stats {
        uint8_t avg = 0;   ///< bpm, 0 = no readings
        uint8_t max = 0;
    };

    /// Start of the day (or of the first lap).
    void reset(uint32_t t);

    /// One second. @p bpm 0 means no reading this second.
    void add(uint32_t t, uint8_t bpm);

    /// Stats for the lap [start, end), which must not start before the end
    /// of the previous lap. Readings before @p end are then forgotten.
    Stats closeLap(uint32_t start, uint32_t end);

    Stats day() const;

private:
    static constexpr uint32_t kRing = 3600;   ///< seconds kept second by second

    struct Acc {
        uint32_t sum = 0, n = 0;
        uint8_t  max = 0;
        void add(uint8_t b)
        {
            if (b) { sum += b; ++n; if (b > max) max = b; }
        }
        Stats stats() const
        {
            Stats s;
            if (n) { s.avg = static_cast<uint8_t>((sum + n / 2) / n); s.max = max; }
            return s;
        }
    };

    uint8_t  mRing[kRing] = {};
    uint32_t mFirst = 0;      ///< oldest second held in the ring
    uint32_t mNext  = 0;      ///< next second expected
    uint32_t mLapFrom = 0;    ///< readings before this are already counted
    Acc      mFolded;         ///< readings that left the ring, >= mLapFrom
    Acc      mDay;
};

} // namespace Ski

#endif // SKI_HEART_RATE_LOG_HPP
